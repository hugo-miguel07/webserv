# RedirectHandler — o handler mais simples, e por isso mesmo interessante

Documentação de estudo do `RedirectHandler`
(`includes/RedirectHandler.hpp` + `srcs/RedirectHandler.cpp`).
Primeiro handler concreto implementado; estabeleceu o padrão que os
outros seguiram (limpar a resposta, validar a config, construir, DONE).

Estado: **implementado e testado** (8 testes em `test/handler_test.cpp`).

---

## 1. Contrato — o que este handler decide e o que NÃO decide

Quando o router despacha um pedido para o `RedirectHandler`, a location
**é** um redirect: o parser preencheu `setRedirect(target, code)` a
partir de um `return <code> <target>` (estilo nginx). O handler:

- **decide**: o código exato da resposta (o configurado), o valor do
  header `Location`, se há corpo (HEAD → não);
- **não decide**: se o pedido devia ser redirecionado (trabalho do
  router escolher esta location), nem reescrever caminhos
  (`rewrite` é outro mecanismo, não implementado).

## 2. Fluxo de handle()

```
target vazio || code == 0        → 500 (config em falta)
code fora de [300, 399]          → 500 (config sem sentido)
res.clear()                      → limpa o que o router pré-preencheu
setStatus(code, reason)          → 301/302/303/307/308...
setHeader("Location", target')   → RFC 7231 §6.4: obrigatório em 3xx
HEAD? → sem corpo                → RFC 7231 §4.3.2
senão → corpo HTML mínimo        → navegadores sem redirect mostram algo
return DONE                      → resposta normal, conexão mantida
```

### Porquê 500 e não `ERROR` quando a config está mal

`e_status::ERROR` significa "o servidor falhou, fechar a conexão sem
resposta coerente" (HANDLER_BASE.md §3). Uma location de redirect sem
`return` configurado é um **erro de configuração** — o servidor está
saudável, o pedido é legítimo, e uma resposta 500 com página de erro é
exatamente o que um nginx faria. As duas guardas (target vazio, code
não-3xx) são a defesa contra o parser ainda não ter preenchido a
location — os stubs devolvem `""`/`0`, e o handler degrada com dignidade.

### Porquê `res.clear()` num handler que "só" redireciona

Se o router ou outro layer pré-preencheu headers (Content-Type, um
corpo parcial), esses headers **não podem** escapar na resposta de
redirect. Mesmo padrão do `buildError()` (HANDLER_BASE.md §4).

## 3. buildTarget() — semântica do `return` do nginx

```cpp
target tem "://"  → absoluto: usa-se VERBATIM ("https://exemplo.com")
senão             → mesmo host: "http://" + Host do pedido + target
Host ausente      → target nu (relativo — válido por RFC 7231 §7.1.2)
```

Decisões documentadas porque custaram uma iteração de testes:

| Decisão | Razão |
|---|---|
| **NÃO juntar o caminho do pedido ao target** | `return 301 /new` significa `Location: /new` para **todos** os pedidos da location, igual ao nginx. A primeira versão juntava `req.path` e `GET /old/sub/page` produzia `http://host/new/old/sub/page` — o teste apanhou o bug imediatamente |
| `http://` fixo para targets relativos | o webserv não sabe se há TLS a montante; o Host do pedido é a melhor aproximação do host canónico. Targets absolutos passam intocados, por isso `return 301 https://...` continua perfeito |
| sem Host (HTTP/1.0) → target relativo | um `Location: /new` é um URI de referência válido; o cliente resolve contra o host do pedido. Melhor que inventar um host |

### HEAD: Location sem corpo

O redirect em HEAD mantém `Location` e os headers todos mas devolve
corpo vazio (RFC 7231 §4.3.2). Navegadores que seguem redirects
ignoram o corpo de qualquer maneira — o corpo só existe para humanos
com clientes que não seguem.

## 4. Integração com a config (lado da config por ligar)

`Locations` tem os campos placeholder acordados com quem faz a camada
de config:

```cpp
void setRedirect(const std::string &target, int code);  // parser chama isto
const std::string& getRedirect() const;                 // "" = não configurado
int getRedirectCode() const;                            // 0 = não configurado
```

O parser deve chamar `setRedirect(target, code)` uma vez por diretiva
`return <code> <target>`. Enquanto ninguém chamar, o handler responde
500 (ver §2) — nunca um redirect silencioso para o vazio.

## 5. O que foi verificado

Testes em `test/handler_test.cpp`:

- 301 com target absoluto passa verbatim (`https://example.com`);
- corpo menciona a reason phrase; `Content-Type: text/html`;
- target relativo + Host do pedido → `http://site1.com:8080/new`
  (a versão com join de path falhou aqui e foi corrigida, §3);
- HEAD → status 301 + Location, corpo vazio;
- `setRedirect("/new", 200)` → 500 (code não-3xx);
- location sem config nenhuma → 500 (target vazio);
- 8 testes próprios + os partilhados do suite (80 no total).

`make` limpo com `-Wall -Wextra -Werror -std=c++98`.
