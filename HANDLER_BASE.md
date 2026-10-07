# Handler base — a classe abstrata que todos os handlers partilham

Documentação de estudo do `Handler` (`includes/Handler.hpp` +
`srcs/Handler.cpp`) e das mudanças em `Locations` que o suportam.
Complementa o `REQUEST_RESPONSE.md`: o `Request` chega interpretado, o
`Response` sai montado — o `Handler` é **quem decide o que fazer com o
pedido e preenche a resposta**.

---

## 1. Onde isto vive na arquitetura

```
Server (loop de poll)
  │
  │  Request::isComplete() → Request::parse()          [REQUEST_RESPONSE.md]
  ▼
ROTEAMENTO (a implementar no Server)
  │  1. escolhe o ServerConfig pelo Host
  │  2. escolhe a Locations cujo path é o prefixo mais longo do request
  │  3. escolhe o handler concreto (por método + tipo de location)
  ▼
Handler::handle(req, res, srv, loc)      ← ESTE DOCUMENTO
  │  (StaticHandler | UploadHandler | DeleteHandler |
  │   DirectoryListingHandler | RedirectHandler | CGIHandler)
  ▼
Response preenchida → toString() → send()                [REQUEST_RESPONSE.md]
```

O `Handler` é **abstrato**: nenhum `Handler` é instanciado diretamente.
Os seis handlers concretos implementam `handle()` à sua maneira, mas todos
herdam os três helpers partilhados — para não haver seis cópias da mesma
lógica de erro/caminhos/métodos.

## 2. A assinatura de `handle()` — a decisão mais importante

```cpp
virtual e_status handle(const Request &req,
                        Response &res,
                        const ServerConfig &srv,
                        const Locations &loc) = 0;
```

**Porquê passar a config como argumentos em vez de guardar dentro do
handler?**

| Alternativa rejeitada | Problema |
|---|---|
| handlers vão buscar a config sozinhos | um handler não sabe *qual* servidor/location lhe diz respeito — isso é decisão do roteamento |
| `setContext(srv, loc)` antes de `handle()` | estado escondido, dois passos para uma chamada, fácil de esquecer de atualizar |
| globals/singleton | proibido pelo bom senso (e fonte de bugs com múltiplos `server { }`) |

Com a config **explícita nos argumentos**:

- os handlers são *stateless* por design (a única exceção é o
  `CGIHandler`, que guarda o pid do filho);
- qualquer chamada é autocontida — dá para testar um handler com configs
  diferentes sem resetar nada;
- o compilador obriga o router a fornecer sempre um `ServerConfig` e uma
  `Locations` válidos.

**Contrato do router**: quando `handle()` é chamado, `srv` é o servidor
virtual casado pelo header `Host` e `loc` é a location aplicável ao caminho.
Se a config não tiver location que case (p.ex. só `server { root ... }`),
o router constrói uma `Locations` default (`path "/"`, sem restrições de
método) — o handler não precisa de saber disto.

## 3. `e_status` — o que o handler devolve e o que o Server faz com isso

```cpp
enum e_status
{
    DONE,       // resposta totalmente construída, pronta a enviar
    NEED_CGI,   // manter a ligação aberta: há um processo CGI a correr
    ERROR       // falha interna: fechar a ligação
};
```

| Valor | Quem usa | Reação do Server |
|---|---|---|
| `DONE` | todos | serializa `res` com `toString()` e manda para `_responseBuffer` |
| `NEED_CGI` | só `CGIHandler` | não envia nada ainda; continua a fazer poll nos pipes do processo filho; quando o CGI termina, o handler preenche `res` e aí devolve `DONE`. **Nota: no modelo síncrono atual o `CGIHandler` nunca devolve `NEED_CGI`** — corre tudo dentro de `handle()` e devolve sempre `DONE`/`ERROR` (ver `CGI_HANDLER.md` §1); o valor existe para a integração assíncrona |
| `ERROR` | qualquer um | não confia na resposta: fecha a ligação (erro interno, não é um erro HTTP normal) |

Nota importante: **um erro HTTP normal (404, 403, ...) é um `DONE`**. O
handler preenche `res` com a página de erro e o Server envia normalmente.
`ERROR` é para quando *o servidor* falhou (p.ex. `fork()` falhou) e nem
consegue garantir uma resposta coerente.

## 4. `buildError()` — erros numa linha, sem fugas de estado

```cpp
void Handler::buildError(Response &res, int statusCode) const
{
    res.clear();            // 1. apaga tudo o que a resposta já tinha
    res.setError(statusCode);   // 2. status + frase + corpo HTML padrão
}
```

O `clear()` antes do `setError()` é a parte subtil: se um handler começou
a preencher a resposta (pôs um `Content-Type`, metade do body...) e depois
falhou, os headers velhos **não podem escapar** para a página de erro.
`clear()` repõe `200 OK` vazio e o `setError()` (ver `REQUEST_RESPONSE.md`
§2.3) monta o resto.

**Quando NÃO usar `buildError()`**: se a config tiver uma `error_page`
com corpo customizado, o handler chama diretamente
`res.setError(code, customBody)` — o `buildError()` é só o caminho
mínimo partilhado.

### Padrão para 405 (Method Not Allowed)

O RFC 7231 exige que a resposta a um 405 traga o header `Allow` com os
métodos aceites. **A ordem importa**: `buildError()` começa por
`res.clear()`, portanto um `Allow` escrito ANTES de `buildError()` é
apagado e nunca chega ao cliente. Escreve-se sempre DEPOIS (ou usa-se
o helper `Handler::notAllowed()`, que já faz isto certo):

```cpp
if (!isMethodAllowed(req, loc))
    return (notAllowed(res, loc));      // helper da base: ordem correta

// ... ou, manualmente (repara: Allow DEPOIS de buildError):
if (req.method != "GET" && req.method != "HEAD")
{
    buildError(res, 405);
    res.setHeader("Allow", "GET, HEAD");   // depois do clear()!
    return (DONE);
}
```

## 5. `resolvePath()` — transformar URL em caminho seguro no disco

```cpp
bool resolvePath(const std::string &root,
                 const std::string &requestPath,
                 std::string &outPath) const;
```

É o helper mais importante para a segurança do servidor. Junta a `root`
da config ao caminho pedido **colapsando os segmentos `.` e `..`**, e
falha se o resultado escaparia à root.

### Algoritmo, passo a passo

1. **Rejeita NUL no meio do caminho** — `open()`/`stat()` truncam a string
   no primeiro `\0`; um pedido por `/file\0.html` teria de falhar aqui e
   não dentro da libc.
2. **Normaliza a root** — remove slashes finais (`/www/` → `/www`; a root
   `/` torna-se `""`).
3. **Percorre o requestPath segmento a segmento** (entre `/`):
   - `""` (slashes duplicados ou final) → ignora;
   - `"."` → ignora;
   - `".."` → faz pop do último segmento guardado; **se não houver nada
     para fazer pop → `false`** (estava a tentar sair da root);
   - outro → guarda.
4. **Reconstrói**: `root + "/" + segmento1 + "/" + segmento2...`; se não
   sobrou nenhum segmento, o resultado é a própria root (`"/"` se vazia).

### Exemplos (com `root = "/www"`)

| requestPath | resultado | motivo |
|---|---|---|
| `/index.html` | `/www/index.html` | caso normal |
| `/` | `/www` | raiz da location |
| `/a//b/` | `/www/a/b` | slashes a mais ignorados |
| `/a/./b/../c` | `/www/a/c` | `.` e `..` colapsados |
| `/../etc/passwd` | **`false`** | `..` sem nada atrás → sairia da root |
| `/a/../../etc/passwd` | **`false`** | o segundo `..` esvaziou a pilha |
| `/a/../b` | `/www/b` | `..` que **fica** dentro da root é legítimo |

### Porquê colapsar em vez de só concatenar?

- Se fizesse só `root + requestPath`, o `stat()` seguiria os `..` por
  nós: `/www/../etc` chamaria `stat("/www/../etc")` e o kernel resolve
  para `/etc`. Colapsar *lexicamente* (sem tocar no disco) deixa o
  servidor no controlo.
- O `requestPath` que aqui chega **já foi percent-descodificado** pelo
  `Request::parse()` (`%2e%2e` já virou `..`) — por isso a proteção apanha
  também os pedidos ofuscados. Ver `REQUEST_RESPONSE.md` §1.7.

Os handlers chamam-no no início de `handle()`:

```cpp
std::string filePath;
if (!resolvePath(locRoot, req.path, filePath))
{
    buildError(res, 403);   // ou 400; traversal nunca chega ao disco
    return (DONE);
}
```

## 6. `isMethodAllowed()` — 401 vs 405 vs 501, quem decide o quê

```cpp
bool isMethodAllowed(const Request &req, const Locations &loc) const;
```

Compara `req.method` com os métodos configurados na location
(`loc.getAllowedMethods()`). Duas regras:

1. **Lista vazia = sem restrição** (estilo nginx): se a location não
   configura métodos, tudo é permitido. Devolve `true`.
2. Caso contrário, procura o método na lista (comparação exata,
   case-sensitive — os métodos HTTP são case-sensitive por definição).

### A divisão de responsabilidades entre camadas

| Camada | Código | Exemplo |
|---|---|---|
| **Parser** (`Request::parse`) | método desconhecido do protocolo | `FROBNICATE /a` → **501 Not Implemented** |
| **Handler** (`isMethodAllowed`) | método conhecido, proibido nesta location | `DELETE /index.html` numa location só-GET → **405 Method Not Allowed** (+ header `Allow`) |
| **Handler concreto** | método certo, mas o recurso não o suporta | `POST` num `StaticHandler` → o router nem devia ter chegado aqui |

Esta separação é a mesma do nginx e é uma pergunta clássica de avaliação.

## 7. `Locations` — o que foi acrescentado

A classe existia mas era uma caixa fechada (campos privados sem
acessores). Agora:

```cpp
const std::string&              getPath() const;
const std::vector<std::string>& getAllowedMethods() const;  // por referência: sem cópia
void setPath(const std::string &path);
void addAllowedMethod(const std::string &method);   // um por token
```

`addAllowedMethod` segue o estilo do `ServerConfig::setIndex`
(um `push_back` por chamada) — é o formato que o `ConfigParser` usa nos
blocos `location { }` (um `allow_methods GET POST` gera duas chamadas).

**Já existe na `Locations`** (os handlers usam tudo isto): root/index/
autoindex por location, `upload_store`, `redirect` + `redirect_code`,
`cgi_extensions` + `cgi_interpreter`. **Falta só a `error_page`**: os
handlers respondem erros com o corpo default (`Response::setError(code)`);
quando a config a tiver, o caminho já existe (`setError(code, customBody)`) —
falta decidir quem a aplica (ver `INTEGRATION_ROUTER.md` §3).

## 8. Esqueleto típico de um handler concreto

Para fixar como as peças encaixam, o `StaticHandler::handle()` terá
± esta forma:

```cpp
Handler::e_status StaticHandler::handle(const Request &req, Response &res,
                                        const ServerConfig &srv,
                                        const Locations &loc)
{
    // 1. método
    if (req.method != "GET" && req.method != "HEAD")
        return (notAllowed(res, loc));          // 405 + Allow

    // 2. root efetivo + caminho seguro
    const std::string &root = loc.getRoot().empty() ? srv.getRoot()
                                                    : loc.getRoot();
    std::string filePath;
    if (!resolvePath(root, req.path, filePath))
    {
        buildError(res, 403);                   // 403
        return (DONE);
    }

    // 3. existe? é ficheiro ou diretório?
    if (isFile(filePath))
        return (serveFile(filePath, req, res)); // 200 + corpo
    if (isDirectory(filePath))
        return (serveIndex(...));               // index; senão autoindex

    buildError(res, 404);
    return (DONE);
}
```

Repara que tudo o que não é específico de servir ficheiros — erro, método,
caminho — já está feito na base; o handler concreto só escreve o que lhe
é próprio.

## 9. O que foi verificado

Testes executados (e passados) sobre esta implementação:

- `resolvePath`: casos normais, slashes duplicados, `.`/`..` colapsados,
  traversal rejeitada (`/../etc/passwd`, `/a/../../etc`, `/..`), root `/`,
  root com slashes finais, e o ataque de byte NUL;
- `isMethodAllowed`: permitido/proibido numa location configurada,
  location vazia = tudo permitido;
- `buildError`: status e frase corretos (403 Forbidden), corpo HTML
  presente, `Content-Type: text/html`, e **nenhum header pré-existente
  sobrevive** ao `clear()`;
- `make` limpo com `-Wall -Wextra -Werror -std=c++98`, incluindo os seis
  handlers concretos com a nova assinatura.
