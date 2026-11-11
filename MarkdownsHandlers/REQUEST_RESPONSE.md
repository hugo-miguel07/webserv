# Camada HTTP: `Request` e `Response`

Documentação de estudo da dupla de classes que forma a camada HTTP do webserv.
`Request` **lê e interpreta** os bytes que chegam do cliente; `Response` **monta e
serializa** os bytes que voltam. Tudo em C++98, sem bibliotecas externas.

Este capítulo descreve a **implementação atual** (`srcs/Request.cpp`,
`includes/Request.hpp`, `srcs/Response.cpp`, `includes/Response.hpp`) e o ponto
onde o `Server::handleRead` a chama. A versão anterior deste documento descrevia
uma API planeada (`parse`/`isComplete`/`peekContentLength`) que **nunca foi
escrita**; os nomes abaixo são os que existem no código.

---

## Visão geral do fluxo

```
socket do cliente
      │  recv()                       (Server::handleRead, 4096 bytes de cada vez)
      ▼
Client::_requestBuffer                (bytes crus, acumulados entre recv)
      │
      ├─ Request::checkHeader()            já há "\r\n\r\n"? veio CL/TE?
      ├─ Request::readingBody()            CL só dígitos? TE == "chunked"?
      ├─ Request::checkingBody_framing()   já chegaram os CL bytes de corpo?
      │   ou checkingBody_chuncked()             │
      │                                          └─ não → return, espera mais
      ├─ Request::start_parsing()          preenche method/path/query/body/headers
      ▼
seleção de location → Handler::handle(req, res, srv, loc)     [Server::handleRead]
      ▼
Response::toString()  →  Client::_responseBuffer  →  send()
```

Os três pontos de entrada continuam a ser **três chamadas**, mas não são
`isComplete`/`parse`/`toString`: são `checkHeader()`, `start_parsing()` e
`toString()`. O que fica no meio — `readingBody()` e os dois `checkingBody_*` —
é o **enquadramento (framing)** do corpo, e é o `Server` que o dirige:

```cpp
/* Server::handleRead — resumido */
Request req(client);                       // um Request novo por recv()
header_status = req.checkHeader(client.getRequestBuffer());
if (req.getHasContentStatus() && req.getTransferEncodingStatus())
    throw std::runtime_error("Invalid request format");   // CL+TE → 400
if (header_status && req.getHasContentStatus())
    if (req.checkingBody_framing(...))  req.start_parsing(...);
else if (header_status && req.getTransferEncodingStatus())
    if (req.checkingBody_chuncked(...)) req.start_parsing(...);
else if (!header_status)
    /* headers ainda a meio: não faz nada */;
else
    req.start_parsing(...);                // pedido sem corpo
```

Duas consequências que valem a pena reter:

- **O estado não vive no `Request`.** `req` é local a `handleRead`, portanto o
  que persiste entre `recv()` é só `Client::_requestBuffer`. Enquanto o corpo
  não está completo, cada `recv()` volta a varrer o buffer todo desde o início
  (`checkHeader` faz um `find` sobre a string acumulada) — num upload grande o
  custo cresce com o quadrado do tamanho.
- **Qualquer desvio é uma exceção.** Não há `_valid` nem `_errorStatus`: o
  parser lança `std::runtime_error` e o `catch` do `Server` responde **400**
  (ver §1.8).

---

## 1. `Request` — interpretar o pedido

`class Request : public Client` — herda o buffer cru, o listener e a config do
cliente dono do pedido. Há três construtores: `Request()`, `Request(const
Client&)` (copia o cliente e vai buscar os servidores ao listener) e
`Request(std::vector<ServerConfig>)` (para testes, sem socket).

### 1.1 Campos públicos

| Campo | Conteúdo | Exemplo |
|---|---|---|
| `method` | método da linha de pedido, tal como veio | `"GET"` |
| `path` | target até ao `?` — **cru, sem percent-decoding** | `/a/b` |
| `query` | depois do `?`, cru, sem o `?`; vazio se não houver | `x=1` |
| `body` | corpo (CL ou chunked já descodificado) | `"a=1&b=2"` |
| `version` | **nunca é escrito** — ver abaixo | `""` |
| `headers` | `map<nome, valor>`, nomes em lowercase | `headers["host"]` |

Estado privado que interessa: `_contentLen`, `_hasContent`,
`_hasTransferEncoding`, `_TransferMethod` (`"chunked"`), `_headers` (o mapa que
`start_parsing` copia para `headers`), `_requestLine`, `_server`/`_serverName`
(o `ServerConfig` casado pelo `Host`), `_nextRequestBytes` e `_finisedHeader`
(os dois últimos são escritos e nunca lidos — §1.7).

> **`version` está vazio em tempo de execução.** `RequestLineParsing` guarda a
> versão em `_requestLine["Version"]` e valida-a, mas `start_parsing` só copia
> `method`, `path`, `query`, `body` e `headers`. Nada no parser preenche o campo
> público. O `CGIHandler` usa-o para `SERVER_PROTOCOL` (`srcs/CGIHandler.cpp`),
> por isso o script recebe essa variável vazia. O único sítio que lhe dá valor é
> `test/handler_test.cpp` (`req.version = "HTTP/1.1";`), o que esconde o bug.

### 1.2 API real

Tudo é **público** (não há separação private/protected no parser) e as funções
recebem o buffer **por valor** — cada chamada copia a string acumulada:

```cpp
/* etapa 1 — há headers? veio corpo? */
bool  checkHeader(const std::string buffer);            // true quando existe "\r\n\r\n"

/* etapa 2 — validar e guardar o enquadramento (lança em erro) */
void  readingBody(const std::string buffer);            // CL só dígitos; TE == "chunked"

/* etapa 3 — o corpo já está todo no buffer? */
bool  checkingBody_framing(const std::string buffer);   // Content-Length
bool  checkingBody_chuncked(const std::string buffer);  // chunked (grafia do código)

/* etapa 4 — interpretar de facto (lança em erro) */
void  start_parsing(const std::string requestbuffer);

/* peças de start_parsing, chamáveis à parte */
void  RequestLineParsing(const std::string requestBuffer);
void  QueryParsing(std::string requestTarget);
void  HeadersToMap(const std::string requestBuffer);
void  HostParsing(std::string value);
void  MethodParsing(std::string method);
void  parseBody(const std::string& buffer);
void  parseChunkedBody(const std::string& buffer, size_t pos);

/* consultas */
bool               getHasContentStatus();        // veio Content-Length?
bool               getTransferEncodingStatus();   // veio Transfer-Encoding?
size_t             getContentLength() const;      // _contentLen (após readingBody)
const std::string& getHeader(const std::string& name) const;
```

Não existem `parse()`, `isValid()`, `getErrorStatus()`, `clear()`,
`isComplete()`, `peekContentLength()`, `hasBody()` nem `urlDecode()`.

### 1.3 O handshake passo a passo

| Passo | O que decide | Detalhe que engana |
|---|---|---|
| `checkHeader(buf)` | `true` quando o buffer contém `"\r\n\r\n"`; deixa `_hasContent`/`_hasTransferEncoding` a partir dos headers **crus** | só reconhece CRLF: um cliente que mande `\n\n` fica preso para sempre |
| `readingBody(buf)` | CL: todos os caracteres têm de ser dígitos (senão lança) e o valor vai para `_contentLen`; TE: o valor tem de ser **exatamente** `chunked` (comparação case-sensitive) | é chamado duas vezes: aqui e outra vez dentro de `start_parsing` |
| `checkingBody_framing(buf)` | compara `buf.size() - inícioDoCorpo` com `_contentLen`: menor → `false` (espera), igual → `true`, maior → imprime `"too many bytes"`, guarda `_nextRequestBytes` e devolve `true` | os bytes a mais **nunca são usados**: não há pipelining |
| `checkingBody_chuncked(buf)` | percorre os blocos: tamanho em hex, dados, `\r\n` obrigatório; `true` só no bloco `0` seguido de `\r\n` | tamanho vazio ou não-hex → exceção; extensões (`5;ext`) passam por acidente; trailers depois do `0` são ignorados |
| `start_parsing(buf)` | linha de pedido → headers (+ `Host` → `ServerConfig`) → `MethodParsing` → `readingBody` → `parseBody` → copia os campos públicos | **sem `checkHeader` antes, o corpo sai sempre vazio** (`_hasContent` fica `false`), porque é o `checkHeader` que o marca |

`start_parsing` falha (exceção) se: não houver `\r\n` na linha de pedido, a
linha estiver vazia, faltar o espaço depois do método, o target estiver vazio ou
não começar por `/`, a versão não for `HTTP/1.1`, o `?` vier sem query, um
header não tiver `:`, um nome de header tiver caracteres fora do conjunto
*token*, faltar o `Host`, o `Host` não casar com nenhum `server_name`, ou não
existir location para o path (o `MethodParsing` procura uma e lança
`"Location not found"`; o resultado da procura é descartado e o `Server` repete
a mesma seleção).

### 1.4 O que é aceite e o que é recusado

| Situação | Resultado |
|---|---|
| `GET /a HTTP/1.1` | aceite |
| `HTTP/1.0`, `HTTP/2.0`, `HTTP/1.1 ` (espaço a mais) | **400** (não há 505) |
| `GET http://host/a HTTP/1.1` (absolute-form) | **400** — o target tem de começar por `/` |
| `OPTIONS * HTTP/1.1` | **400** (asterisk-form não é suportado) |
| `FROBNICATE /a HTTP/1.1` | aceite pelo parser (não há whitelist de métodos); o dispatcher responde **405** |
| `GET /a? HTTP/1.1` (query vazia) | **400** |
| `POST /a HTTP/1.1` sem CL nem TE | aceite, `body` vazio — **não há 411** |
| CL e TE em simultâneo | **400** (verificado no `Server`, antes do parsing) |
| `Transfer-Encoding: gzip` (ou `chunked, gzip`) | **400** (não há 501) |
| `Content-Length: 12a` | **400** |
| header sem `:`, nome com espaço, nome vazio | **400** |
| obs-fold (linha a começar por espaço) | se tiver `:`, é lida como header normal; se não tiver, **400**. Não é rejeitada |
| header repetido | fica o **primeiro** (`map::insert`), não são combinados com `", "` |
| sem `Host` | **400** |
| `Host` que não casa com nenhum `server_name` | **400** |
| target com `%XX` | não é descodificado: `/my%20file` procura um ficheiro chamado literalmente `my%20file` |
| secção de headers ou URI gigantes | aceites: **não há limites** (8192/2048 não existem, logo 431 e 414 também não) |

Sobre os escapes: `%2e%2e` **não** se transforma em `..`. Como o `resolvePath()`
colapsa segmentos literalmente, vê um segmento chamado `%2e%2e`, que não é `..`
e portanto não escapa à root — o resultado é 404, não traversal. É seguro por
acidente, não por desenho; o preço é que URLs com escapes não funcionam.

### 1.5 Corpo: Content-Length e chunked

- **CL**: `_body = buffer.substr(inícioDoCorpo, _contentLen)`. Se o buffer tiver
  menos bytes, o `substr` trunca em silêncio — o `Server` só chama isto depois
  de `checkingBody_framing` dizer que está completo.
- **chunked**: `checkingBody_chuncked` só responde; quem descodifica é
  `parseChunkedBody`, que percorre os blocos e faz `append` ao `_body`. Bloco
  malformado → exceção; `\r\n` em falta depois dos dados → exceção.
- **bytes a mais**: `_nextRequestBytes` é calculado em `checkingBody_framing` e
  nunca é lido. O `Client` também não tem forma de consumir/truncar
  `_requestBuffer`, pelo que na prática cada ligação serve **um** pedido — o que
  sobra é descartado quando a ligação fecha.

### 1.6 `getHeader()` — só funciona com nomes em lowercase

As chaves de `_headers`/`headers` são normalizadas para lowercase
(`lowerHeaderName`), mas **o argumento não é**: `getHeader` faz `find` direto e
depois tenta o mapa público. Ou seja:

```cpp
req.getHeader("host")        // ✅ devolve o valor
req.getHeader("Host")        // ❌ devolve "" (string vazia estática)
```

Não há crash nem exceção — devolve uma string vazia por referência. Todos os
chamadores internos usam lowercase (o `CGIHandler` e o `UploadHandler` incluídos),
por isso o servidor funciona; a armadilha é para quem escrever um handler novo.
Quem precisa de comparar como o `Response` faz usa `hasHeader()`, que normaliza
os dois lados.

### 1.7 O que o parser ainda **não** faz

| Dívida | Efeito prático |
|---|---|
| `version` nunca preenchido | `SERVER_PROTOCOL` do CGI vai vazio |
| sem percent-decoding | `/my%20file`, `%2F`, UTF-8 escapado → 404; quem quiser descodificar tem de o fazer no parser ou no `resolvePath` |
| sem limites de header/URI/corpo | um header de 100 MB é memória do processo; 414/431 não existem |
| sem 411 | POST sem framing é despachado com corpo vazio |
| versão errada dá 400, não 505 | um `HTTP/2.0` recebe 400 |
| TE inválido dá 400, não 501 | idem |
| `_nextRequestBytes` calculado e ignorado | sem pipelining |
| sem keep-alive no `Response` | nenhuma resposta emite `Connection` |
| `_finisedHeader` escrito, nunca lido (e com typo) | campo morto |
| `MethodParsing` seleciona location e deita fora | o `Server` repete a seleção (trabalho duplicado) |
| buffers por valor | `checkHeader`/`readingBody`/`checkingBody_*` copiam o buffer acumulado a cada chamada |
| `std::cout` de debug no parser | imprime `Method`, `Target`, `Query`/`Path` (trocados), todos os headers e `"Parsing done"` por cada pedido |

### 1.8 Erros: só 400, e só por exceção

O parser não devolve código nenhum. Cada falha é uma `std::runtime_error` com
uma mensagem de debug, e `Server::handleRead` converte-a num `Response` com
`setError(400)`:

| Mensagem no código | Quando |
|---|---|
| `Invalid request line(invalid end)!` | não há `\r\n` na linha de pedido |
| `Invalid request line!` / `(no method)` / `Method doesnt exist!` | linha vazia, sem espaço, método vazio |
| `Invalid request target(doesnt exixt)` / `Inavalid request target` | target ausente, vazio, ou sem `/` inicial |
| `Invalid HTTP version` | versão diferente de `HTTP/1.1` |
| `Query not found` / `Invalid path` / `Invalid query` | `?` sem query, ou path vazio à frente do `?` |
| `Invalid header` / `Invalid header name` | linha sem `:`, ou nome fora do conjunto *token* |
| `No host` / `Invalid host` | falta `Host`, ou não casa com `server_name` |
| `Location not found` | nenhuma location é prefixo do path |
| `Invalid content len` / `Invalid encoding information` | CL não numérico, ou TE diferente de `chunked` |
| `Invalid chunk size` / `Invalid chunk` / `Invalid chunk ending` | chunked malformado |
| `Invalid request format` | CL+TE (lançada pelo `Server`, não pelo parser) |

Os códigos que este capítulo antes prometia — **411, 414, 431, 501, 505 — não
são emitidos em lado nenhum**. As frases existem em `Response::getReasonPhrase`
(são texto, não comportamento). Os 405/403/404/413/500 que o servidor devolve
vêm dos handlers, não do parser.

---

## 2. `Response` — montar a resposta

### 2.1 Campos públicos

```cpp
int    statusCode;                          // 200 no construtor
std::string reasonPhrase;                   // "OK"; vazio → lookup automático
std::map<std::string, std::string> headers; // chave tal como foi inserida
std::string body;                           // conteúdo cru (HTML, binário, ...)
```

### 2.2 API

```cpp
void   setStatus(int code, const std::string &reason);  // reason "" → frase padrão
void   setHeader(const std::string &name, const std::string &value);  // sobrescreve
void   setBody(const std::string &content);
bool   hasLocation() const;              // procura "Location" case-insensitive
void   clear();                          // volta a 200 OK, sem headers nem body

static std::string getReasonPhrase(int code);            // desconhecido → "Unknown"
void   setError(int code);                               // corpo HTML padrão
void   setError(int code, const std::string &customBody); // gancho para error_page
std::string toString() const;                            // serializa
```

### 2.3 `setError()`

`setError(404)` põe `404 Not Found`, corpo HTML no estilo nginx
(`<center><h1>404 Not Found</h1></center>`), `Content-Type: text/html` e
`Content-Length` igual ao corpo. A variante com dois argumentos usa o corpo
dado (o caminho já existe para as `error_page`, mas nada o chama com um corpo
vindo da config). `setError` **não limpa** nada antes: quem quer garantir que
headers antigos não escapam chama `clear()` primeiro — é exatamente o que o
`Handler::buildError()` faz (ver `HANDLER_BASE.md` §4).

### 2.4 `toString()` — o que sai exatamente

```
HTTP/1.1 404 Not Found\r\n                 ← statusCode + frase (lookup se vazia)
Content-Length: 8\r\n                      ← headers do map, por ordem de chave
Content-Type: text/html\r\n
Date: Sun, 20 Sep 2026 11:52:19 GMT\r\n    ← injetado se ausente
Server: webserv/1.0\r\n                    ← injetado se ausente
\r\n
not here                                   ← body verbatim
```

Notas que o documento anterior dava mal:

- Os headers saem na **ordem das chaves do `std::map`** — ordem lexicográfica
  por bytes, portanto `Content-Length` antes de `content-type`, e maiúsculas
  antes de minúsculas. O HTTP não impõe ordem.
- `Content-Length` **só é injetado quando não existe** (procura
  case-insensitive). Se um handler escrever um valor errado, é o dele que sai.
  O `setError` acerta-o; o `HEAD` do `StaticHandler` depende disso para anunciar
  o tamanho do GET com corpo vazio.
- `Date` é gerado com `gmtime` + `strftime("%a, %d %b %Y %H:%M:%S GMT")`; se
  falhar, o header é **omitido** (não sai linha vazia).
- Não há `Connection`, `Transfer-Encoding` nem negociação: cada resposta é um
  corpo integral.
- `HEAD` não é tratado aqui nem no `Server`; quem esvazia o corpo mantendo o CL
  são os handlers que o suportam (`StaticHandler`, `RedirectHandler`).

### 2.5 Frases de estado

`getReasonPhrase()` cobre 31 códigos: 2xx (200/201/202/204/206), 3xx
(301/302/303/304/307/308), 4xx (400/401/403/404/405/408/409/410/411/413/414/
415/418/431) e 5xx (500/501/502/503/504/505). Qualquer outro código — incluindo
os 411/414/431/501/505 que o parser não emite — serializa com a frase
`Unknown`, porque a tabela só serve para dar texto à linha de estado.

---

## 3. Testar à mão

O `Server` já está ligado, por isso o caminho curto é usá-lo:

```bash
make && ./webserv configurations/config.config

curl -v http://localhost:8080/                       # GET
curl -v -X POST --data 'ola=mundo' \
     http://localhost:8080/upload/notas.txt          # POST com Content-Length
curl -v -X POST -H 'Transfer-Encoding: chunked' \
     --data 'ola=mundo' http://localhost:8080/upload/notas.txt   # chunked
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc localhost 8080
```

Para exercitar o parser isolado (sem socket), o construtor tem de receber
servidores, senão o `HostParsing` não tem `server_name` com que casar:

```cpp
Request req(servers);                       // std::vector<ServerConfig> da config
if (!req.checkHeader(raw)) return (1);      // headers ainda incompletos
req.readingBody(raw);                       // valida CL/TE  (lança em erro)
if (req.checkingBody_framing(raw))          // ou checkingBody_chuncked(raw)
    req.start_parsing(raw);                 // preenche method/path/query/body/headers
std::cout << req.method << " " << req.path << "?" << req.query << "\n";
```

---

## 4. O que falta (estado real, não o planeado)

1. **Códigos e limites**: 411, 414, 431, 501 e 505 não existem; header/URI
   ilimitados. Se forem requisito, têm de ser acrescentados ao parser (e o
   `_errorStatus` que faltava aqui volta a fazer falta).
2. **`req.version`**: uma linha em `start_parsing`
   (`version = _requestLine["Version"];`) resolve o `SERVER_PROTOCOL` vazio do
   CGI.
3. **Percent-decoding**: decidir onde acontece (parser ou `resolvePath`) —
   hoje `/my%20file` dá 404.
4. **413 antecipado**: sem `peekContentLength`, o 413 do `UploadHandler` só
   corre depois de o corpo inteiro estar em memória; o limite protege o disco,
   não a RAM.
5. **Pipelining / keep-alive**: `_nextRequestBytes` está calculado e ignorado, e
   o `Response` não emite `Connection`.
6. **`error_page`**: o gancho `setError(code, customBody)` existe e ninguém o
   alimenta com a config.
