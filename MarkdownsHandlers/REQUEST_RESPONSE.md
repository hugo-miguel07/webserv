# Camada HTTP: `Request` e `Response`

Documentação de estudo da dupla de classes que forma a camada HTTP do webserv.
`Request` **lê e interpreta** os bytes que chegam do cliente; `Response` **monta e
serializa** os bytes que voltam. Tudo em C++98, sem bibliotecas externas.

---

## Visão geral do fluxo

```
socket do cliente
      │  recv()
      ▼
Client::_requestBuffer            (bytes crus, crescem aos pedaços)
      │
      │  Request::isComplete(buf)  ← já chegou o request inteiro?
      ▼
Request::parse(buf)               (interpreta: linha, headers, corpo)
      │
      │  roteamento → Handler::handle(req, res)
      ▼
Response                          (o handler preenche status/headers/body)
      │
      │  Response::toString()
      ▼
Client::_responseBuffer           → send() de volta ao cliente
```

O `Server` (ainda a ligar) usa exatamente estes três pontos de entrada:
`isComplete()` → `parse()` → `toString()`.

---

# 1. `Request` — interpretar o pedido

## 1.1 Campos públicos

| Campo | Conteúdo | Exemplo |
|---|---|---|
| `method` | método da linha de pedido | `"GET"` |
| `target` | request target **cru**, tal como veio | `"/a/b?x=1"` |
| `path` | target **sem query e percent-decodificado** | `"/a/b"` |
| `query` | query string **crua**, sem o `?` | `"x=1"` |
| `version` | versão do protocolo | `"HTTP/1.1"` |
| `headers` | `map<nome, valor>` — **nomes guardados em lowercase** | `headers["host"]` |
| `body` | corpo já **descodificado** (chunked → texto final) | `"a=1&b=2"` |

Estado interno privado: `_valid` (última `parse()` correu bem?) e
`_errorStatus` (código HTTP a responder quando falhou).

## 1.2 API

```cpp
bool   parse(const std::string &raw);            // interpreta; false se malformado
bool   isValid() const;                          // resultado da última parse()
int    getErrorStatus() const;                   // 400/411/414/431/501/505
void   clear();                                  // repõe tudo a zero

static bool   isComplete(const std::string &buf);        // buffer tem o request todo?
static size_t peekContentLength(const std::string &buf); // CL declarado (streaming)

std::string getHeader(const std::string &name) const;    // case-insensitive
bool        hasBody() const;                             // body não vazio?
size_t      getContentLength() const;                    // CL dos headers (0 se ausente)
static std::string urlDecode(const std::string &s, bool plusAsSpace);
```

## 1.3 `parse()` passo a passo

O parser é **tolerante nos finais de linha** (aceita `\r\n\r\n` e `\n\n` como
fim da secção de headers; dentro das linhas remove um `\r` final) mas
**estrito na estrutura**. Ordem de execução:

1. **`clear()`** — repõe o estado (e `_errorStatus = 400`).
2. **Procurar o fim dos headers** — `findHeaderEnd()` devolve a posição e o
   comprimento do separador. Se a secção exceder `MAX_HEADER_BYTES`
   (8192) → **431**.
3. **Linha de pedido** — dividida em tokens por espaços/tabs; têm de ser
   **exatamente 3** (`method target version`), senão → 400.
   - método: tem de ser um *token* válido **e** conhecido
     (`GET`, `HEAD`, `POST`, `PUT`, `DELETE`, `OPTIONS`), senão → **501**
     (*Not Implemented* — o 405 "método não permitido" é trabalho do handler,
     que conhece os métodos da location; aqui ainda não há contexto).
   - versão: só `HTTP/1.0` ou `HTTP/1.1`; começa por `HTTP/` mas é outra →
     **505** (*Version Not Supported*); outra coisa qualquer → 400.
   - target: mais de `MAX_URI_BYTES` (2048) → **414** (*URI Too Long*).
4. **Absolute-form tolerado** — `GET http://host:8080/a/b` é aceite: remove-se
   o esquema+autoridade e fica só o caminho; se não existir header `Host`,
   é semeado a partir da autoridade (requisito do HTTP/1.1).
   Qualquer target que não comece por `/` → 400 (asterisk-form não suportado).
5. **Split path/query** — primeiro `?` separa os dois. A `path` passa por
   `urlDecode()`; se resultar vazia havia um escape inválido (`%zz`, `%`
   solto) → 400. A `query` fica crua de propósito (ver §1.6).
6. **Headers** — para cada linha:
   - vazia → ignora;
   - começa por espaço/tab (*obs-fold*, continuação de linha) → **400**
     (rejeitado por ser vetor clássico de *request smuggling*);
   - sem `:`, ou nome vazio/não-token → 400;
   - nome guardado em lowercase; **valor repetido é combinado com `", "`**
     (RFC 7230 permite combinar; dois `Content-Length` diferentes tornam-se
     `"5, 5"` e falham a validação de dígitos mais à frente).
7. **Enquadramento do corpo (framing)** — é aqui que se decide onde o corpo
   acaba:
   - `Transfer-Encoding` **e** `Content-Length` presentes em simultâneo →
     **400** (ambiguidade proibida — proteção contra *request smuggling*);
   - `Transfer-Encoding` sem `chunked` → **501** (só chunked é suportado);
   - `chunked` → `walkChunked()` descodifica os blocos para `body`
     (malformado → 400);
   - `Content-Length` → tem de ser só dígitos; `body` = `substr(início, CL)`
     (se o buffer tiver bytes a mais, pertencem ao request seguinte —
     *pipelining* — e são ignorados aqui);
   - `POST`/`PUT` sem nenhum dos dois → **411** (*Length Required*);
   - `GET`/`DELETE`/... sem framing → corpo vazio, tudo bem.

Se tudo correu bem: `_valid = true`.

## 1.4 `isComplete()` — como saber que já chegou tudo?

O socket entrega bytes **aos bocados**: um POST com corpo de 5 MB pode
chegar em centenas de `recv()`. O Server não pode chamar `parse()` antes da
hora — daí este método **estático**, que analisa o buffer cru sem o alterar:

1. Sem fim de headers à vista → ainda não está completo…
   - **mas** se o buffer já passou `MAX_HEADER_BYTES`, devolve `true`!
     É intencional: obriga o Server a sair do loop de buffering e responder
     431, em vez de acumular memória para sempre.
2. Com fim de headers à vista, lê `Content-Length` / `Transfer-Encoding`:
   - **chunked** → `walkChunked()` percorre os blocos; só está completo
     quando encontra o bloco de tamanho 0 seguido da linha vazia final;
   - **Content-Length** → completo quando `buf.size() - inícioDoCorpo >= CL`;
     valores absurdos (> 12 dígitos) contam como "nunca completo" — quem
     corta a ligação nesses casos é o Server via `peekContentLength()`;
   - **nenhum** → completo no fim dos headers (pedidos sem corpo).

## 1.5 `peekContentLength()` — aplicar `client_max_body_size` cedo

Devolve o CL declarado por um buffer **ainda incompleto** (0 se os headers
nem chegaram). Isto permite ao Server rejeitar um upload de 2 GB **antes**
de gastar 2 GB de RAM: se `peek > maxBodySize` → responde logo **413**
(*Payload Too Large*) e corta. O valor `> 12` dígitos devolve o sentinela
`(size_t)-1`, maior do que qualquer limite.

## 1.6 Helpers

- **`getHeader(name)`** — os nomes são guardados em lowercase, portanto a
  procura também passa o argumento para lowercase: HTTP diz que nomes de
  header são *case-insensitive*, e assim `getHeader("Host")`,
  `getHeader("host")` e `getHeader("HOST")` são equivalentes.
- **`urlDecode(s, plusAsSpace)`** — `%XX` → byte; `+` → espaço só quando
  `plusAsSpace` é `true` (convenção das query strings de formulários).
  Escape inválido devolve `""` (sinal de erro, usado pela `parse()`).
- **`walkChunked()`** (função interna) — percorre a codificação chunked:
  linha com tamanho em hexadecimais (extensões após `;` ignoradas), dados,
  `\r\n` obrigatório, até chegar ao bloco `0` + linha vazia. Devolve
  `CHUNK_OK` / `CHUNK_INCOMPLETE` / `CHUNK_BAD`, pelo que serve tanto para
  descodificar (`parse`) como para testar completude (`isComplete`).

## 1.7 Porquê estas decisões?

| Decisão | Razão |
|---|---|
| `path` descodificada, `query` crua | A `path` tem de casar com o sistema de ficheiros (`/my%20file` → `/my file`) e com os prefixos das locations. A `query` viaja crua porque o CGI recebe `QUERY_STRING` cru por especificação — quem descodifica é o script. |
| Descodificar **antes** de verificar traversal | `GET /%2e%2e/etc/passwd` só se revela perigoso depois de descodificado (`/../etc/passwd`). O `resolvePath()` do `Handler` é quem corta estes caminhos. |
| Nomes de header em lowercase | Elimina toda uma classe de bugs de comparação; o valor mantém a caixa original (pode ser significativo). |
| Aceitar `\n\n` sem `\r` | Clientes mínimos (`nc`, `telnet`) e testes manuais nem sempre enviam CRLF; ser tolerante aqui poupa dores de cabeça na avaliação sem enfraquecer a estrutura. |
| Rejeitar obs-fold e CL+TE juntos | Ambos são técnicas conhecidas de *request smuggling*; o nginx rejeita pelas mesmas razões. |
| Limites 8192/2048 | Valores próximos dos defaults do nginx; sem limites, um header infinito é um ataque de memória. |

## 1.8 Códigos de erro que a `parse()` emite

| Código | Quando |
|---|---|
| `400 Bad Request` | linha/headers malformados, obs-fold, escape inválido, CL+TE juntos, CL não numérico, chunked malformado, target sem `/` inicial |
| `411 Length Required` | POST/PUT sem Content-Length nem Transfer-Encoding |
| `414 URI Too Long` | target > 2048 bytes |
| `431 Request Header Fields Too Large` | secção de headers > 8192 bytes |
| `501 Not Implemented` | método desconhecido ou Transfer-Encoding diferente de chunked |
| `505 HTTP Version Not Supported` | `HTTP/2.0`, `HTTP/0.9`, etc. |

---

# 2. `Response` — montar a resposta

## 2.1 Campos públicos

```cpp
int    statusCode;      // 404
std::string reasonPhrase;   // "Not Found" (pode ficar vazio → lookup automático)
std::map<std::string, std::string> headers;  // tal como foram inseridos
std::string body;           // conteúdo cru (HTML, ficheiro, JSON...)
```

## 2.2 API

```cpp
void   setStatus(int code, const std::string &reason);  // reason "" → frase padrão
void   setHeader(const std::string &name, const std::string &value);
void   setBody(const std::string &content);
bool   hasLocation() const;             // há header Location? (redirects)
void   clear();                         // repõe 200 OK vazio

static std::string getReasonPhrase(int code);   // 404 → "Not Found"; senão "Unknown"
void   setError(int code);                      // erro com corpo HTML padrão
void   setError(int code, const std::string &customBody); // p.ex. error_page
std::string toString() const;                   // serializa tudo
```

## 2.3 `setError()` — respostas de erro numa linha

`setError(404)` produz: status `404 Not Found`, corpo HTML no estilo nginx
(`center><h1>404 Not Found</h1>...`), `Content-Type: text/html` e
`Content-Length` certinho. Com dois argumentos usa um corpo customizado —
é o gancho para as `error_page` da configuração mais tarde.
O `Handler::buildError()` delega aqui.

## 2.4 `toString()` — o que sai exatamente

```
HTTP/1.1 404 Not Found\r\n          ← linha de estado (reason vazio → lookup)
Content-Type: text/plain\r\n        ← headers, por ordem alfabética (std::map)
Content-Length: 8\r\n               ← injetado se nenhum header CL existir
Server: webserv/1.0\r\n             ← injetado se ausente
Date: Sun, 20 Sep 2026 11:52:19 GMT\r\n  ← injetado se ausente (gmtime+strftime)
\r\n                                 ← linha vazia separa headers do corpo
not here                             ← body, sem transformações
```

Notas:

- Os headers saem **ordenados alfabeticamente** porque `std::map` é ordenado
  — o HTTP não impõe ordem, portanto não é problema.
- As três injeções (`Content-Length`, `Server`, `Date`) verificam a presença
  **case-insensitive**: se o handler pôs `content-length` (minúsculas), não
  é duplicado.
- `Content-Length` reflete sempre `body.size()` — é o `toString()` que
  garante a coerência, não o handler.
- HEAD não é tratado aqui nem no Server: quem limpa o corpo mantendo o
  CL são os próprios handlers que o suportam (StaticHandler e
  RedirectHandler; ver `STATIC_HANDLER.md` §4) — o Server só serializa
  e envia o que o handler deixou na `Response`.

## 2.5 Frases de estado

`getReasonPhrase()` cobre ~31 códigos — 2xx (200/201/202/204/206), 3xx
(301/302/303/304/307/308), 4xx (400...431, incl. 418 *I'm a teapot* 😉) e
5xx (500...505). `setStatus()` com reason vazio faz lookup automático, e
`toString()` também — ou seja, `res.statusCode = 301` sozinho já serializa
com a frase certa.

---

# 3. Testar à mão (sem o Server ligado)

```bash
# Request: GET simples com query e header
printf 'GET /index.html?a=1 HTTP/1.1\r\nHost: x\r\n\r\n' | nc localhost 8080

# POST com corpo
curl -X POST --data 'ola=mundo' http://localhost:8080/upload

# POST chunked (exercita walkChunked)
curl -X POST -H 'Transfer-Encoding: chunked' --data 'ola=mundo' http://localhost:8080/upload
```

O ficheiro `srcs/Request.cpp` tem tudo comentado por secção; para ver o
parser a viver, um mini-main com `Request r; r.parse(raw);` + prints das
linhas 1–7 do fluxo do topo deste documento chega para estudar.

---

# 4. O que falta ligar

1. **Server**: `handleRead` → `appendRequest` → `isComplete()` → `parse()` →
   rotear → `toString()` → `appendResponse()`. Hoje o `Client::mirror()`
   faz eco do buffer e é o próximo a substituir.
2. **Roteamento**: escolher o handler por método + location; 405/404 vivem aí
   (checklist completo em **`INTEGRATION_ROUTER.md`**).
3. **413**: com `peekContentLength()` vs `client_max_body_size`.
4. ~~**Handlers concretos**~~ — **feito**: todos os seis recebem um
   `Request` pronto, preenchem a `Response` e devolvem `DONE`/`ERROR`
   (ver `HANDLERS.md`).
