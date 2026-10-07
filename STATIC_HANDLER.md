# StaticHandler — servir ficheiros do disco com segurança

Documentação de estudo do `StaticHandler` (`includes/StaticHandler.hpp` +
`srcs/StaticHandler.cpp`). Assume o `HANDLER_BASE.md` (helpers partilhados,
`resolvePath`, `e_status`) e o `REQUEST_RESPONSE.md` (`Request` entra
preenchido, `Response` sai montada).

Estado: **implementado e testado** — 20 testes próprios + 8 do listing
(numa suite de regressão de 80 em `test/handler_test.cpp`, compilada à
parte, não entra no `make` do webserv).

---

## 1. Decisão de fluxo — a ordem dos checks NÃO é arbitrária

```
handle(req, res, srv, loc)
  │
  ├─ 1. método permitido?          não → 405 (+ Allow)          [policy da location]
  ├─ 2. método é GET/HEAD?         não → 405 (+ Allow GET,HEAD) [capacidade do handler]
  ├─ 3. root = loc.root || srv.root
  ├─ 4. resolvePath() ok?          não → 403 (traversal)
  ├─ 5. é diretório?
  │     ├─ sem "/" final?          → 301 Location: path + "/" (+ query preservada)
  │     ├─ serveIndex() serviu?    → DONE (200 + ficheiro)
  │     ├─ autoindex on?           → serveDirectoryListing()
  │     └─ senão                   → 403
  ├─ 6. é ficheiro?                → serveFile() (200)
  └─ 7. nada disto                 → 404
```

Porquê esta ordem:

| Ordem | Razão |
|---|---|
| método antes de path | um `POST /nao/existe` tem de dar **405**, não 404 — o problema é o método, não o recurso |
| traversal (403) antes de stat() | o caminho malicioso nunca chega ao disco (HANDLER_BASE.md §5) |
| 301 da slash antes do index | sem isto, `GET /sub` serviria `/sub/index.html` e os links relativos da página partir-se-iam |
| index antes de autoindex | `index.html` existente tem prioridade sobre a listagem — igual ao nginx |
| 403 final vs 404 | o diretório **existe** (stat Confirmou); recusar-se a listá-lo é política, não ausência |

## 2. Os dois 405 — policy vs. capacidade (armadilha clássica)

Um `POST /file.txt` pode ser rejeitado por duas razões distintas, e o
header `Allow` tem de ser **verdadeiro** em ambas (RFC 7231 §6.5.5):

| Situação | Quem decide | `Allow` enviado |
|---|---|---|
| location com `allow_methods GET HEAD` e chega `DELETE` | a location | `GET, HEAD` (a lista configurada) |
| location com `allow_methods POST` (ou lista vazia) e chega `POST` num ficheiro estático | o handler (um ficheiro não aceita POST) | `GET, HEAD` |

O nginx faz o mesmo: POST em ficheiro estático dá 405 com
`Allow: GET, HEAD` — mesmo que a location permita POST. Por isso o
`StaticHandler` faz **dois checks** em vez de hardcodar `GET|HEAD`:

```cpp
if (!isMethodAllowed(req, loc))       // 1. policy — Allow = lista da config
    return (notAllowed(res, loc));    //    (helper da base, ordem certa)
if (req.method != "GET" && req.method != "HEAD")   // 2. capacidade
{
    buildError(res, 405);
    res.setHeader("Allow", "GET, HEAD");   // DEPOIS do buildError!
    return (DONE);
}
```

**Armadilha que os testes apanharam**: `buildError()` chama
`res.clear()` — um `Allow` escrito *antes* é apagado. O padrão do
HANDLER_BASE.md §4 estava errado e já foi corrigido lá também.

## 3. Root — override da location com fallback para o server

```cpp
const std::string &root = loc.getRoot().empty() ? srv.getRoot()
                                                : loc.getRoot();
```

Semântica nginx: a location **herda** o root do server a menos que
configure o seu próprio. Detetado nos testes: a primeira versão passava
o `root` efetivo ao `StaticHandler` mas o `DirectoryListingHandler`
re-resolvia `req.path` contra `loc.getRoot()` — com root só no server,
a listagem saía do sítio errado. Correção: o `DirectoryListingHandler`
ganhou um `serve(res, dirPath, urlPath)` que recebe o caminho
**já resolvido**, e o `StaticHandler` passa-lhe o seu.

## 4. serveFile() — leitura e Content-Type

- `std::ifstream` binário + `rdbuf()` para ostringstream: carrega o
  ficheiro inteiro para o corpo. (Streaming para o socket é melhoria
  futura na camada `Server`, não aqui.)
- `file.bad()` após a leitura → 500: existir mas não se conseguir ler
  (corrupção, race com escrita) é problema do servidor.
- `!file.is_open()` → **desambiguação**: `isFile()` outra vez — se ainda
  existe é permissão (**403**), se desapareceu entretanto é **404**.
- `Content-Type` por extensão (`guessContentType()`): mapa mínimo
  html/css/js/png/jpg/gif/svg/ico/txt/pdf/json; extensão em minúsculas
  para `.HTML` funcionar; desconhecida → `application/octet-stream`
  (o browser descarrega em vez de adivinhar).

### HEAD: o detalhe do Content-Length

HEAD tem de espelhar o GET **incluindo o tamanho que o corpo teria**,
mas sem corpo no socket:

```cpp
if (req.method == "HEAD")
{
    std::ostringstream len;
    len << content.size();
    res.setHeader("Content-Length", len.str());   // tamanho do GET
    res.setBody("");                              // corpo vazio
}
```

O `toString()` da `Response` só gera `Content-Length` automático quando
o header não existe — por isso o set explícito sobrevive. Testado: GET
seguido de HEAD no mesmo handler devolve corpo correto e `12` respetivamente.

## 5. serveIndex() — resolução de index

```cpp
bool serveIndex(const std::string &dirPath,
                const std::vector<std::string> &indexes, ...);
```

- Junta `dirPath + "/" + index` e serve o **primeiro que exista**
  (ordem de configuração = prioridade, como no nginx).
- Devolve `bool`: `false` = nenhum candidato existe → o chamador decide
  (autoindex ou 403). Um `e_status` aqui seria ambíguo — "não servi" não
  é erro.
- Candidatos: `loc.getIndexes()` quando a location configura os seus,
  senão `srv.getIndex()` — mesma herança do root.
- NOTA: o index pode ser qualquer ficheiro regular; um `index` que
  aponte para um diretório não é seguido (isFile falha) — fica para o
  autoindex/403, que é o comportamento seguro.

## 6. O 301 da trailing slash

`GET /sub` (diretório, sem slash) → `301` + `Location: /sub/`.

Porquê antes do index: se servisse logo `/sub/index.html`, os links
relativos dessa página (e de qualquer listagem) resolveriam contra `/`
em vez de `/sub/`. O redirect normaliza o URL e mantém os hrefs
corretos. É exatamente o que o nginx faz (`absolute_redirect` à parte).

A **query string sobrevive** ao redirect (`/sub?x=1` → `Location:
/sub/?x=1`): perder filtros/página num redirect de diretório é surpresa
desagradável — mesmo comportamento do nginx. Testado.

## 7. Autoindex — delegação sem duplicação

`serveDirectoryListing(req, res, dirPath)` instancia um
`DirectoryListingHandler` e chama `lister.serve(res, dirPath, req.path)`:

- `dirPath` já resolvido → respeita o root efetivo (ver §3);
- `req.path` como `urlPath` → título e links do listing corretos;
- `autoindex` **off** + sem index → 403 (não 404): o diretório existe,
  a política é que proíbe a listagem.

Ver `DIRECTORY_LISTING_HANDLER.md` para a listagem em si (readdir,
ordenação, percent-encoding dos hrefs).

## 8. O que foi verificado

Testes em `test/handler_test.cpp` (fixtures em /tmp, apagados no fim):

- ficheiro simples 200 + conteúdo + `Content-Type: text/plain`;
- binário (`\x01\x02...`) intacto; extensão desconhecida → octet-stream;
- index em `/` e em `/sub/`; dir sem slash → 301 (**com e sem query**);
- autoindex on → listagem; autoindex off sem index → 403;
- 404 ficheiro ausente; 403 traversal (`/../secret.txt`);
- `..` legítimo dentro da root funciona;
- 405 por policy (Allow = lista da config) e por capacidade
  (Allow = `GET, HEAD`); lista vazia de métodos → policy deixa passar,
  capacidade apanha (paridade nginx);
- HEAD sem corpo mas com `Content-Length` do GET;
- root override da location (index e ficheiro relocado);
- listagem: entradas ordenadas, espaços percent-encodados nos hrefs,
  nenhum caminho de disco (/tmp/...) no HTML;
- redirect: ver `REDIRECT_HANDLER.md` para os 8 testes próprios.

`make` limpo com `-Wall -Wextra -Werror -std=c++98`.
