# webserv — Documentação Técnica Completa

Servidor HTTP/1.1 em C++98: leitura da configuração, `poll()`, parsing de
pedidos HTTP, handlers de método e execução de CGI.

Este ficheiro é a **compilação de todos os Markdown do repositório**
(`README.md` + `MarkdownsHandlers/*.md`) num só documento, na ordem em que se
lê o projeto: visão geral → camada HTTP → classe base → um capítulo por
handler → referência rápida → referências de estudo.

> O texto de cada capítulo é o dos documentos originais, sem reescrita: só os
> níveis de cabeçalho foram normalizados para encaixarem nesta hierarquia e
> cada capítulo indica o ficheiro de origem. As referências cruzadas entre
> documentos aparecem pelo nome do ficheiro original (ex.:
> `INTEGRATION_ROUTER.md`), que continua a ser o sítio canónico desse assunto.

## Índice

1. [Camada de Handlers de Requisição](#cap-1) — `HANDLERS.md`
2. [Camada HTTP: `Request` e `Response`](#cap-2) — `REQUEST_RESPONSE.md`
3. [Handler base — a classe abstrata que todos os handlers partilham](#cap-3) — `HANDLER_BASE.md`
4. [StaticHandler — servir ficheiros do disco com segurança](#cap-4) — `STATIC_HANDLER.md`
5. [UploadHandler — POST de ficheiros sem abrir buracos](#cap-5) — `UPLOAD_HANDLER.md`
6. [DeleteHandler — o destrutivo, e por isso o mais conservador](#cap-6) — `DELETE_HANDLER.md`
7. [DirectoryListingHandler — autoindex seguro e barato](#cap-7) — `DIRECTORY_LISTING_HANDLER.md`
8. [RedirectHandler — o handler mais simples, e por isso mesmo interessante](#cap-8) — `REDIRECT_HANDLER.md`
9. [CGIHandler — fork, pipes, e a guerra contra os zombies](#cap-9) — `CGI_HANDLER.md`
10. [CHEATSHEET — webserv](#cap-10) — `CHEATSHEET.md`
11. [Referências de estudo](#cap-ref)

<a id="cap-1"></a>

## 1. Camada de Handlers de Requisição

*Fonte: `MarkdownsHandlers/HANDLERS.md`*

---

Estrutura dos seis handlers de requisição
(`StaticHandler`, `UploadHandler`, `DeleteHandler`, `DirectoryListingHandler`,
`RedirectHandler`, `CGIHandler`). Os cabeçalhos e a estrutura das classes
chegaram aqui como scaffolding; **todas as implementações estão agora
completas e testadas** (ver tabela de estado no fim deste documento).

O projeto compila limpo com `make` após essas mudanças.

---

### Novos arquivos

#### Tipos compartilhados (placeholders para a camada HTTP)

| Arquivo | Finalidade |
|---|---|
| `includes/Request.hpp` / `srcs/Request.cpp` | Requisição HTTP parseada: método, caminho, query, versão, mapa de cabeçalhos, corpo. Preenchido depois pelo parser de requisições. |
| `includes/Response.hpp` / `srcs/Response.cpp` | Resposta em construção: código de status, frase de razão (reason phrase), mapa de cabeçalhos, corpo. Serializada e transmitida depois por `Server` através do buffer de resposta do `Client`. |

#### Hierarquia dos handlers

```
Handler (abstrata)
├── StaticHandler
├── UploadHandler
├── DeleteHandler
├── DirectoryListingHandler
├── RedirectHandler
└── CGIHandler
```

**`Handler.hpp` / `Handler.cpp`** — classe base abstrata com:

- `enum e_status { DONE, NEED_CGI, ERROR }`
  - `DONE` — resposta totalmente construída, pronta para enviar
  - `NEED_CGI` — manter a conexão aberta e continuar fazendo polling (processo CGI em execução)
  - `ERROR` — falha interna, a conexão deve ser fechada
- `virtual handle(const Request&, Response&, const ServerConfig&, const Locations&) = 0` — ponto de entrada principal
- Helpers compartilhados por todos os handlers concretos:
  - `buildError()` — resposta de erro mínima (status + corpo padrão)
  - `resolvePath()` — resolve o caminho da requisição contra a raiz (root), com proteção contra traversal
  - `isMethodAllowed()` — verifica o método contra os métodos permitidos de uma location

| Handler | Arquivo | Funções declaradas |
|---|---|---|
| `StaticHandler` | `includes/StaticHandler.hpp` | `handle`, `serveFile`, `serveIndex` (resolução de index), `serveDirectoryListing` (delega no listing), `guessContentType` |
| `UploadHandler` | `includes/UploadHandler.hpp` | `handle`, `isBodyTooLarge` (vs `client_max_body_size`), `extractFilename`, `saveFile` (em `upload_store`), `getBoundary`, `parseMultipart` / `extractDispositionFilename` (multipart RFC 2046), `sanitizeFilename` (traversal/ NUL rejeitados) |
| `DeleteHandler` | `includes/DeleteHandler.hpp` | `handle`, `removeFile`, `removeDirectory` (rmdir, nunca recursivo), `deleteError` (errno → HTTP) |
| `DirectoryListingHandler` | `includes/DirectoryListingHandler.hpp` | `handle`, `serve` (para quem já resolveu o caminho — `StaticHandler`), `listEntries` (ordenado), `buildHtml` (+ `urlEncode` nos hrefs, `htmlEscape` no título/texto) |
| `RedirectHandler` | `includes/RedirectHandler.hpp` | `handle`, `buildTarget` (a partir da config de redirect da location) |
| `CGIHandler` | `includes/CGIHandler.hpp` | `handle`, `isScript` (vs `cgi_extension`), `buildEnv` / `freeEnv` (variáveis de ambiente do CGI), `launch` (fork/dup2/execve), membro `_cgiPid`, acessores `getCgiPid`/`getStdinFd`/`getStdoutFd` (integração NEED_CGI) |

Cada handler tem o seu `.cpp` em `srcs/` — os stubs iniciais foram todos
substituídos por implementações completas (estado na tabela abaixo).

---

### Correções em arquivos existentes

- **`includes/ServerConfig.hpp`** — adicionados `_host` e `getHost()` (não há `setHost`: `_host` nunca é preenchido),
  aos quais `ServerConfig.cpp` já fazia referência (incompatibilidade pré-existente).
- **`includes/Client.hpp` / `srcs/Client.cpp`** — adicionado `getRequestBuffer()`
  (os handlers precisam de acesso à requisição bruta).
- **`Makefile`** — adicionados os 10 novos arquivos `.cpp` a `SRC_FILES`.

---

### Decisões de design

- Os handlers são **stateless** (sem estado), exceto `CGIHandler`, que rastreia
  o pid do seu processo filho. São instanciados **por requisição**: o dispatch
  de `Server::handleRead` cria o handler numa variável local, dentro do próprio
  `handle(...)`.
- Tudo é compatível com **C++98** e segue as convenções do projeto
  (nomenclatura `_member`, acessores `get_x()`, casts `(void)` em
  parâmetros não usados).

### Próximos passos conhecidos

~~`handle()` atualmente não recebe configuração~~ — **resolvido**: `handle()`
passa a receber `(const Request&, Response&, const ServerConfig&, const
Locations&)`; os helpers partilhados (`buildError`, `resolvePath`,
`isMethodAllowed`) já estão implementados. Ver `HANDLER_BASE.md` para a
explicação completa.

### Estado dos handlers concretos

| Handler | Estado | Documento |
|---|---|---|
| `RedirectHandler` | **implementado + testado** | `REDIRECT_HANDLER.md` |
| `StaticHandler` | **implementado + testado** | `STATIC_HANDLER.md` |
| `DirectoryListingHandler` | **implementado + testado** | `DIRECTORY_LISTING_HANDLER.md` |
| `UploadHandler` | **implementado + testado** | `UPLOAD_HANDLER.md` |
| `DeleteHandler` | **implementado + testado** | `DELETE_HANDLER.md` |
| `CGIHandler` | **implementado + testado** (síncrono; NEED_CGI p/ integração futura) | `CGI_HANDLER.md` |

Regressão: `test/handler_test.cpp` (compilar à parte, instruções no
cabeçalho do ficheiro). 81 testes, todos verdes.

Ligação ao `Server` (despacho, ordem de checks, smoke tests):
ver **`INTEGRATION_ROUTER.md`** — o checklist para quem faz o router.

`Locations` expõe `getPath`/`setPath`, `getAllowedMethods`/`setAllowedMethod`,
`getReturn`/`setReturn(code, target)`, `getRoot`/`setRoot`,
`getIndexes`/`setIndex`, `getAutoIndex`/`setAutoIndex`,
`getUploadStore`/`setUploadStore`, `getCgiExtension`/`setCgiExtension`,
`getCgiPath`/`setCgiPath` e `erase()` — e o parser de config **preenche-os**
(`handleLocation*` → `addLocation`). O redirect só dá 500 quando a location
não tem `return` configurado.

Também na base (`Handler`): `notAllowed()` (405 + `Allow`), `isFile()`,
`isDirectory()` — partilhados por todos os handlers que mexem em ficheiros.
**Atenção à ordem**: headers pós-`buildError()`, que o `clear()` apaga
o que veio antes (apanhado pelos testes; ver `STATIC_HANDLER.md` §2).

<a id="cap-2"></a>

## 2. Camada HTTP: `Request` e `Response`

*Fonte: `MarkdownsHandlers/REQUEST_RESPONSE.md`* (reescrito a partir do código atual)

---

Documentação de estudo da dupla de classes que forma a camada HTTP do webserv.
`Request` **lê e interpreta** os bytes que chegam do cliente; `Response` **monta e
serializa** os bytes que voltam. Tudo em C++98, sem bibliotecas externas.

Este capítulo descreve a **implementação atual** (`srcs/Request.cpp`,
`includes/Request.hpp`, `srcs/Response.cpp`, `includes/Response.hpp`) e o ponto
onde o `Server::handleRead` a chama. A versão anterior deste documento descrevia
uma API planeada (`parse`/`isComplete`/`peekContentLength`) que **nunca foi
escrita**; os nomes abaixo são os que existem no código.

---

### Visão geral do fluxo

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

### 1. `Request` — interpretar o pedido

`class Request : public Client` — herda o buffer cru, o listener e a config do
cliente dono do pedido. Há três construtores: `Request()`, `Request(const
Client&)` (copia o cliente e vai buscar os servidores ao listener) e
`Request(std::vector<ServerConfig>)` (para testes, sem socket).

#### 1.1 Campos públicos

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

#### 1.2 API real

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

#### 1.3 O handshake passo a passo

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

#### 1.4 O que é aceite e o que é recusado

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

#### 1.5 Corpo: Content-Length e chunked

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

#### 1.6 `getHeader()` — só funciona com nomes em lowercase

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

#### 1.7 O que o parser ainda **não** faz

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

#### 1.8 Erros: só 400, e só por exceção

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

### 2. `Response` — montar a resposta

#### 2.1 Campos públicos

```cpp
int    statusCode;                          // 200 no construtor
std::string reasonPhrase;                   // "OK"; vazio → lookup automático
std::map<std::string, std::string> headers; // chave tal como foi inserida
std::string body;                           // conteúdo cru (HTML, binário, ...)
```

#### 2.2 API

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

#### 2.3 `setError()`

`setError(404)` põe `404 Not Found`, corpo HTML no estilo nginx
(`<center><h1>404 Not Found</h1></center>`), `Content-Type: text/html` e
`Content-Length` igual ao corpo. A variante com dois argumentos usa o corpo
dado (o caminho já existe para as `error_page`, mas nada o chama com um corpo
vindo da config). `setError` **não limpa** nada antes: quem quer garantir que
headers antigos não escapam chama `clear()` primeiro — é exatamente o que o
`Handler::buildError()` faz (ver `HANDLER_BASE.md` §4).

#### 2.4 `toString()` — o que sai exatamente

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

#### 2.5 Frases de estado

`getReasonPhrase()` cobre 31 códigos: 2xx (200/201/202/204/206), 3xx
(301/302/303/304/307/308), 4xx (400/401/403/404/405/408/409/410/411/413/414/
415/418/431) e 5xx (500/501/502/503/504/505). Qualquer outro código — incluindo
os 411/414/431/501/505 que o parser não emite — serializa com a frase
`Unknown`, porque a tabela só serve para dar texto à linha de estado.

---

### 3. Testar à mão

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

### 4. O que falta (estado real, não o planeado)

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

<a id="cap-3"></a>

## 3. Handler base — a classe abstrata que todos os handlers partilham

*Fonte: `MarkdownsHandlers/HANDLER_BASE.md`*

---

Documentação de estudo do `Handler` (`includes/Handler.hpp` +
`srcs/Handler.cpp`) e das mudanças em `Locations` que o suportam.
Complementa o `REQUEST_RESPONSE.md`: o `Request` chega interpretado, o
`Response` sai montado — o `Handler` é **quem decide o que fazer com o
pedido e preenche a resposta**.

---

### 1. Onde isto vive na arquitetura

```
Server (loop de poll)
  │
  │  checkHeader → readingBody → checkingBody_* → start_parsing
  │                                    [REQUEST_RESPONSE.md §1.3]
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

### 2. A assinatura de `handle()` — a decisão mais importante

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

### 3. `e_status` — o que o handler devolve e o que o Server faz com isso

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

### 4. `buildError()` — erros numa linha, sem fugas de estado

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

#### Padrão para 405 (Method Not Allowed)

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

### 5. `resolvePath()` — transformar URL em caminho seguro no disco

```cpp
bool resolvePath(const std::string &root,
                 const std::string &requestPath,
                 std::string &outPath) const;
```

É o helper mais importante para a segurança do servidor. Junta a `root`
da config ao caminho pedido **colapsando os segmentos `.` e `..`**, e
falha se o resultado escaparia à root.

#### Algoritmo, passo a passo

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

#### Exemplos (com `root = "/www"`)

| requestPath | resultado | motivo |
|---|---|---|
| `/index.html` | `/www/index.html` | caso normal |
| `/` | `/www` | raiz da location |
| `/a//b/` | `/www/a/b` | slashes a mais ignorados |
| `/a/./b/../c` | `/www/a/c` | `.` e `..` colapsados |
| `/../etc/passwd` | **`false`** | `..` sem nada atrás → sairia da root |
| `/a/../../etc/passwd` | **`false`** | o segundo `..` esvaziou a pilha |
| `/a/../b` | `/www/b` | `..` que **fica** dentro da root é legítimo |

#### Porquê colapsar em vez de só concatenar?

- Se fizesse só `root + requestPath`, o `stat()` seguiria os `..` por
  nós: `/www/../etc` chamaria `stat("/www/../etc")` e o kernel resolve
  para `/etc`. Colapsar *lexicamente* (sem tocar no disco) deixa o
  servidor no controlo.
- O `requestPath` que aqui chega **não foi descodificado**: o `Request` não
  faz percent-decoding (ver `REQUEST_RESPONSE.md` §1.7). Consequência dupla:
  `%2e%2e` é o nome literal de um ficheiro (404 se não existir), não um `..`,
  e `/my%20file` procura um ficheiro chamado `my%20file`; e como `/` não se
  esconde em `%2F`, os separadores que o colapso conta são os reais. Se algum
  dia se acrescentar decoding, tem de ser **antes** de `resolvePath` — senão
  `%2e%2e` passa a ser um `..` que este colapso já não apanha.

Os handlers chamam-no no início de `handle()`:

```cpp
std::string filePath;
if (!resolvePath(locRoot, req.path, filePath))
{
    buildError(res, 403);   // ou 400; traversal nunca chega ao disco
    return (DONE);
}
```

### 6. `isMethodAllowed()` — 405, e porque não há 501

```cpp
bool isMethodAllowed(const Request &req, const Locations &loc) const;
```

Compara `req.method` com os métodos configurados na location
(`loc.getAllowedMethods()`). Duas regras:

1. **Lista vazia = sem restrição** (estilo nginx): se a location não
   configura métodos, tudo é permitido. Devolve `true`.
2. Caso contrário, procura o método na lista (comparação exata,
   case-sensitive — os métodos HTTP são case-sensitive por definição).

#### A divisão de responsabilidades entre camadas

| Camada | Código | Exemplo |
|---|---|---|
| **Parser** (`checkHeader` → `start_parsing`) | não valida o método: guarda o token como veio | `FROBNICATE /a` chega ao handler com `method == "FROBNICATE"` — **sem 501** |
| **Handler** (`isMethodAllowed`) | método fora do `allow_methods` da location | `DELETE /index.html` numa location só-GET → **405 Method Not Allowed** (+ header `Allow`) |
| **Handler concreto** | método que a config permite mas aquele handler não serve | `POST` num `StaticHandler` → **405** com `Allow: GET, HEAD` |

Cuidado com a expectativa: o nginx responde **501** a um método que não
implementa e **405** ao que implementa mas está proibido. Este servidor
**não faz a primeira distinção** — não há whitelist de métodos no parser,
por isso tudo o que não é servido sai como 405. Um `FROBNICATE` não é erro
de protocolo aos olhos do parser; é só um método que nenhum handler aceita.

### 7. `Locations` — o que foi acrescentado

A classe existia mas era uma caixa fechada (campos privados sem
acessores). Agora:

```cpp
std::string                     getPath() const;
std::vector<std::string>        getAllowedMethods() const;   // por valor: cópia
void setPath(const std::string &path);
void setAllowedMethod(const std::string &method);   // um por token
```

`setAllowedMethod` segue o estilo do `ServerConfig::setIndex`
(um `push_back` por chamada) — é o formato que o `ConfigParser` usa nos
blocos `location { }` (um `allow_methods GET POST` gera duas chamadas).

**Já existe na `Locations`** (os handlers usam tudo isto): `root`, `index`,
`allow_methods`, `autoindex`, `upload_store`, `cgi_extension` + `cgi_path` e
`return` (`map<int, string>`). A `error_page` **é lida** pelo parser mas fica
no `ServerConfig` (`_errorPages`), não na location, e nenhum handler lê
`getErrorPages()`: os erros continuam a sair com o corpo default
(`Response::setError(code)`). Quando se ligar, o caminho já existe
(`setError(code, customBody)`) — falta decidir quem a aplica
(ver `INTEGRATION_ROUTER.md` §3).

### 8. Esqueleto típico de um handler concreto

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

### 9. O que foi verificado

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

<a id="cap-4"></a>

## 4. StaticHandler — servir ficheiros do disco com segurança

*Fonte: `MarkdownsHandlers/STATIC_HANDLER.md`*

---

Documentação de estudo do `StaticHandler` (`includes/StaticHandler.hpp` +
`srcs/StaticHandler.cpp`). Assume o `HANDLER_BASE.md` (helpers partilhados,
`resolvePath`, `e_status`) e o `REQUEST_RESPONSE.md` (`Request` entra
preenchido, `Response` sai montada).

Estado: **implementado e testado** — 20 testes próprios + 8 do listing
(numa suite de regressão de 81 em `test/handler_test.cpp`, compilada à
parte, não entra no `make` do webserv).

---

### 1. Decisão de fluxo — a ordem dos checks NÃO é arbitrária

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

### 2. Os dois 405 — policy vs. capacidade (armadilha clássica)

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

### 3. Root — override da location com fallback para o server

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

### 4. serveFile() — leitura e Content-Type

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

#### HEAD: o detalhe do Content-Length

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

### 5. serveIndex() — resolução de index

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

### 6. O 301 da trailing slash

`GET /sub` (diretório, sem slash) → `301` + `Location: /sub/`.

Porquê antes do index: se servisse logo `/sub/index.html`, os links
relativos dessa página (e de qualquer listagem) resolveriam contra `/`
em vez de `/sub/`. O redirect normaliza o URL e mantém os hrefs
corretos. É exatamente o que o nginx faz (`absolute_redirect` à parte).

A **query string sobrevive** ao redirect (`/sub?x=1` → `Location:
/sub/?x=1`): perder filtros/página num redirect de diretório é surpresa
desagradável — mesmo comportamento do nginx. Testado.

### 7. Autoindex — delegação sem duplicação

`serveDirectoryListing(req, res, dirPath)` instancia um
`DirectoryListingHandler` e chama `lister.serve(res, dirPath, req.path)`:

- `dirPath` já resolvido → respeita o root efetivo (ver §3);
- `req.path` como `urlPath` → título e links do listing corretos;
- `autoindex` **off** + sem index → 403 (não 404): o diretório existe,
  a política é que proíbe a listagem.

Ver `DIRECTORY_LISTING_HANDLER.md` para a listagem em si (readdir,
ordenação, percent-encoding dos hrefs).

### 8. O que foi verificado

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

<a id="cap-5"></a>

## 5. UploadHandler — POST de ficheiros sem abrir buracos

*Fonte: `MarkdownsHandlers/UPLOAD_HANDLER.md`*

---

Documentação de estudo do `UploadHandler` (`includes/UploadHandler.hpp` +
`srcs/UploadHandler.cpp`). Assume `HANDLER_BASE.md` (helpers partilhados) e
`STATIC_HANDLER.md` (o padrão dos dois 405, lá explicado — aqui reutilizado).

Estado: **implementado e testado** — 15 testes próprios em
`test/handler_test.cpp` (81 no total no suite).

---

### 1. Contrato — os códigos de resposta e quem os provoca

| Código | Quando | É erro de quem? |
|---|---|---|
| **201 Created** + `Location` | ficheiro(s) gravado(s) | — |
| **400** | multipart malformado, sem part com `filename=`, nome hostil (`..`, NUL, vazio, >200 bytes) | cliente |
| **405** + `Allow: POST` | método não-POST (policy da location *ou* capacidade do handler) | cliente |
| **413** | corpo/Content-Length acima de `client_max_body_size`, **ou limite não configurado** | cliente / config |
| **500** | location sem `upload_store`; erro de disco a gravar | servidor |

### 2. Ordem dos checks — cada um mais caro que o anterior

```
handle()
  ├─ 1. método (policy + capacidade)   → 405        [barato, sem I/O]
  ├─ 2. tamanho do corpo               → 413        [antes de PARSE e de DISCO]
  ├─ 3. upload_store configurado?      → 500
  ├─ 4. multipart? parse               → 400
  ├─ 5. sanitize do nome               → 400
  └─ 6. saveFile()                     → 201 | 500
```

O princípio: **rejeitar o mais cedo possível dentro do handler**. Um corpo
de 10 GB leva 413 antes de ser parseado como multipart e antes de tocar no
disco. Mas atenção ao que "antes" quer dizer aqui: o corpo **já foi lido
para memória** pelo `Request` antes de este `handle()` correr (ver
`REQUEST_RESPONSE.md` §1.5) — o corte é antes do parsing e do I/O de
escrita, não antes da leitura. É a mesma lógica do `resolvePath` antes do
`stat()` no `StaticHandler`: validação barata primeiro, I/O caro depois.

#### 413 com limite não configurado — fail closed

```cpp
if (srv.getBodySize() <= 0)
    return true;    // "too large" = rejeita TUDO
```

Decisão deliberada e contraintuitiva: `client_max_body_size` em falta
poderia significar "sem limite" (estilo nginx). Mas um webserv sem
limite aceita corpos ilimitados em memória — **DoS trivial**. Como o
`Request` já bufferiza o corpo inteiro (ver `REQUEST_RESPONSE.md`), o
tamanho do corpo é memória do processo. Fail closed: sem limite
explícito, não há uploads. Para ter uploads, a config **tem** de
declarar o limite — a escolha fica explícita em quem configura, não em
quem ataca.

Repara também: o check conta o `Content-Length` **declarado** *e* o
tamanho **real** do corpo (a verdade quando o cliente mente no header).
Os dois têm de caber no limite. O que não existe é rejeição antecipada: o
`Server` não compara o `Content-Length` com o limite antes de ler, e o
`Request` não expõe nada como `peekContentLength` — quando este check
corre, o corpo inteiro já está em `req.body`. Ver `REQUEST_RESPONSE.md` §1.7.

### 3. Dois modos de upload

#### Modo raw (sem boundary no Content-Type)

O último segmento do URL nomeia o ficheiro:

```
POST /upload/notas.txt        body = "conteúdo"
→ grava <upload_store>/notas.txt, 201 + Location: <store>/notas.txt
```

`extractFilename()` faz `rfind('/')` no `req.path` (a query string não faz
parte do path, que o `Request` corta no primeiro `?` — ver
`REQUEST_RESPONSE.md` §1.4). O nome é usado **tal como vem no URL**, sem
percent-decoding: `POST /upload/nota%201.txt` grava um ficheiro chamado
literalmente `nota%201.txt`. Nome vazio (`POST /upload/`) → 400.

#### Modo multipart (`boundary=` presente no Content-Type)

Um ficheiro gravado por cada part com `filename=`; form fields simples
são ignorados. Vantagem: um único POST pode trazer vários ficheiros, e
o nome vem do próprio corpo (como os browsers mandam) em vez do URL.

### 4. Anatomia do multipart — o que o parser espera

```
--BOUND\r\n                                    ← delimitador de abertura
Content-Disposition: form-data; name="f1"; filename="a.txt"\r\n
Content-Type: text/plain\r\n
\r\n                                           ← fim dos headers da part
alpha content\r\n                              ← CONTEÚDO (verbatim)
--BOUND\r\n                                    ← próxima part
...
--BOUND--\r\n                                  ← fecho (delimitador + "--")
```

Regras que o `parseMultipart()` implementa (RFC 2046 núcleo):

| Regra | Porquê |
|---|---|
| o `\r\n` ANTES do delimitador pertence ao delimitador, não ao conteúdo | um ficheiro gravado com "abc" não pode ganhar um `\r\n` final fantasma — corrompe binários e hashes |
| tudo antes do primeiro delimitador (preamble) é descartado | alguns clientes mandam texto explicativo antes |
| padding após o delimitador na linha é ignorado | o RFC permite whitespace entre o boundary e o CRLF |
| `--boundary--` termina o corpo | o loop `break`a; o que vier depois é trailer e ignora-se |
| part sem `filename=` = form field → skip | `name="note"` sem filename é dados de formulário, não ficheiro |
| delimitador em falta/truncado → `false` | corpo malformado → 400, nunca gravar metade de nada |

O conteúdo é extraído por `substr(contentStart, next - contentStart)`
onde `next` é a posição do `\r\n--boundary` seguinte: os bytes do
ficheiro chegam **verbatim**, binários incluídos (testado com
`\x01\x02binary`).

### 5. Boundary — onde vive e as suas duas formas

`getBoundary()` procura `boundary` no header `Content-Type`:

```
multipart/form-data; boundary=BOUND          → BOUND       (token nu)
multipart/form-data; boundary="q tok"        → q tok       (entre aspas)
multipart/form-data                          → ""          (sem boundary
                                                            → modo raw)
```

O token nu termina em `;` (próximo parâmetro) ou no fim do header, com
whitespace aparado. Detalhe: um `Content-Type: multipart/form-data`
**sem** boundary não é erro — faz o handler cair no modo raw. (Pode
discutir-se 400; o modo raw é mais útil e o teste "no boundary + empty
name → 400" cobre o caso em que o URL também não nomeia ficheiro.)

### 6. O bug que os testes apanharam — case sensitivity dos headers de part

Os headers da **part** chegam como o cliente os mandou:
`Content-Disposition`. O parser do `Request` normaliza os headers do
**pedido** para minúsculas, mas dentro do corpo multipart não há
normalização nenhuma — e a primeira versão procurava
`"content-disposition"` em minúsculas e não encontrava NADA. Todas as
parts eram tratadas como form fields; zero ficheiros gravados; o teste
multipart falhou em cadeia.

Correção (e técnica por isso):

```cpp
// cópia minúscula PARA PROCURAR, original PARA EXTRAIR:
std::string lower;                    // mesmo comprimento
for (...) lower += tolower(partHeaders[i]);

size_t fn = lower.find("filename=", disp);   // índice válido no original!
... partHeaders.substr(fn + 1, end - fn - 1) // extrai da string ORIGINAL
```

Como `lower` tem o mesmo comprimento do `partHeaders`, os índices
encontrados no lowercase são diretamente usáveis na original — o valor
do `filename` conserva a casing exata que o cliente mandou. (RFC 9110
§5.1: header names são case-insensitive; os *valores* não.)

### 7. Política de filenames — reject, don't rewrite

`sanitizeFilename()` devolve `""` (= 400 no chamador) quando:

| Input | Resultado | Razão |
|---|---|---|
| `../../evil.txt` | **400** | qualquer segmento `.`/`..` = nome hostil. Traversal **rejeita-se, não se reescreve** — `evil.txt` silenciosamente gravado dentro da store seria uma surpresa para o cliente e um leniency bug |
| `a\0b.txt` | **400** | NUL truncaria a gravação ao meio do nome (`open()`/`ofstream` param no byte nulo) |
| `C:\fakepath\foto.jpg` | **201, grava `foto.jpg`** | é o que browsers mandam de verdade (especificação HTML: `C:\fakepath\` por privacidade). Segmentos não são dot-dirs → passa no check acima → o basename fica |
| `.` / `..` / vazio | **400** | nada para gravar |
| nome com caracteres de controlo | 201, control chars removidos | leniência razoável: o ficheiro fica com nome limpo |
| nome > 200 bytes | **400** | abaixo de NAME_MAX=255; demasiado comprido é bug do cliente — melhor 400 claro do que 500 de disco mais tarde |

O contraste `C:\fakepath` (aceita, basename) vs `../..` (rejeita) é a
decisão de design central: **caminhos profundos são normalizados para
o basename, mas dot-dirs são prova de intenção hostil** — um browser
normal nunca manda `..` num `filename=`.

### 8. saveFile() e a resposta 201

```cpp
bool saveFile(const std::string &storeDir, const std::string &filename,
              const std::string &body) const;
```

- trailing slashes da store aparados; caminho = `store + "/" + name`
  (o `name` já veio saneado — basename puro);
- `std::ofstream` **binário + trunc**: binários não podem sofrer
  tradução de `\n`, e regravar por cima substitui (não faz append) —
  reenviar o mesmo ficheiro é idempotente;
- `!is_open()` → `false` → 500 (store sem permissões é problema do
  servidor); `bad()` após escrita → 500 (disco cheio, etc.).

A resposta:

```
HTTP/1.1 201 Created
Location: /var/www/site1/uploads/a.txt
Content-Type: text/plain

Created: /var/www/site1/uploads/a.txt
```

`201 Created` + `Location` é o que o RFC 7231 §6.3.2 pede para POST que
cria um recurso. No multipart, `Location` lista os caminhos separados
por `, ` (extensão pragmática; o caso normal é um ficheiro só).

### 9. Limitações conhecidas (documentadas, não escondidas)

- **Corpo inteiro em memória**: o `Request` já bufferiza tudo; o handler
  grava de uma vez. Streaming para ficheiro durante a receção seria
  mudança na camada `Server`/`Request`, não aqui.
- **Sem `filename=""` explícito**: `filename=""` devolve `""` e a part é
  tratada como field → skip. Alinhado com o RFC (filename vazio = não é
  upload).
- **Sem quota por store**: gravações repetidas com o mesmo nome
  substituem. Versionar/unicidade ficaria para o router/config decidir.

### 10. O que foi verificado

15 testes em `test/handler_test.cpp`:

- raw: 201 + `Location`, conteúdo verbatim no disco;
- multipart: 2 ficheiros + 1 field → os 2 gravados, field não; conteúdo
  binário verbatim; sem `\r\n` fantasma no fim;
- `C:\fakepath\win.txt` → gravado como `win.txt`;
- `../../evil.txt` → 400 e **nada** escapou para fora da store;
- sem boundary + nome vazio → 400; multipart truncado → 400;
- 413 com limite pequeno; **413 com limite em falta** (fail closed);
- 500 sem `upload_store`;
- 405 + `Allow: POST` (policy) e 405 + `Allow: POST` (capacidade, lista
  vazia).

`make` limpo com `-Wall -Wextra -Werror -std=c++98`; suite completa
80/80.

<a id="cap-6"></a>

## 6. DeleteHandler — o destrutivo, e por isso o mais conservador

*Fonte: `MarkdownsHandlers/DELETE_HANDLER.md`*

---

Documentação de estudo do `DeleteHandler` (`includes/DeleteHandler.hpp` +
`srcs/DeleteHandler.cpp`). Assume `HANDLER_BASE.md` (helpers partilhados) e
`STATIC_HANDLER.md`/`UPLOAD_HANDLER.md` (padrões de 405 e de ordem de checks).

Estado: **implementado e testado** — 14 testes próprios em
`test/handler_test.cpp` (81 no total no suite).

---

### 1. Contrato — códigos e quem os provoca

| Código | Quando | É erro de quem? |
|---|---|---|
| **204 No Content** | ficheiro OU diretório vazio removido | — |
| **403** | traversal bloqueada; sem permissão (EACCES/EPERM/EROFS); DELETE não ativado na location (via 405 não — ver §2) | cliente / config |
| **404** | alvo não existe (ou desapareceu entre `stat()` e `remove()`) | cliente |
| **405** + `Allow` | DELETE não está na lista configurada; ou método não-DELETE | cliente |
| **409 Conflict** | diretório não vazio (ENOTEMPTY) | estado do recurso |
| **500** | errno desconhecido | servidor |

### 2. A decisão central — lista vazia NÃO permite (fail closed)

Todos os handlers de leitura seguem a semântica nginx do
`isMethodAllowed()` (HANDLER_BASE.md §6): **lista vazia = sem
restrição**. O `DeleteHandler` deliberadamente **não** a usa para a
policy:

```cpp
// DELETE tem de estar EXPLICITAMENTE na allow_methods da location
for (size_t i = 0; i < methods.size(); ++i)
    if (methods[i] == "DELETE") { policyAllows = true; break; }
if (!policyAllows) { /* 405 + Allow */ }
```

Porquê a diferença: servir um ficheiro a mais é um erro benigno;
**apagar um ficheiro a mais é irreversível**. Com a semântica
"lista vazia permite", uma location sem `allow_methods` — por omissão,
por preguiça, por config incompleta — teria DELETE aberto. Fail closed:
só se apaga onde a config **disse explicitamente** que se pode apagar.
É a filosofia do `dav_methods` do nginx (nada é destrutivo sem declaração).

O `Allow` da resposta 405 mantém-se **verdadeiro** nos dois casos:

- lista não-vazia sem DELETE → `notAllowed()` manda a lista configurada;
- lista vazia → `Allow: DELETE` (o que uma versão corretamente
  configurada desta location aceitaria).

E o inverso: numa location DELETE-only, um `GET` leva 405 +
`Allow: DELETE` via `notAllowed()`.

### 3. Fluxo e ordem dos checks

```
handle()
  ├─ 1. policy: DELETE explícito na location?   não → 405 + Allow
  ├─ 2. método == DELETE?                       não → 405 (lista configurada)
  ├─ 3. resolvePath()                           falha → 403 (traversal)
  ├─ 4. isFile()   → remove() → 204 | errno-map
  ├─    isDirectory() → rmdir() → 204 | errno-map
  └─ 5. nem ficheiro nem diretório              → 404
```

A ordem 404-antes-de-remover tem uma subtileza: o `stat()` do `isFile()`
e o `remove()` não são atómicos — entre eles o ficheiro pode
desaparecer (race). Nesse caso `remove()` falha com ENOENT e o
`deleteError()` responde 404, que é exatamente o código certo. O 404 do
passo 5 é o caso comum (nada existia); o do errno é o raro (existia e
fugiu) — ambos honestos.

Traversal (passo 3) continua a nunca chegar ao disco: o
`/../secret2.txt` leva 403 **e o teste confirma que o alvo continua
lá** — não basta recusar, é preciso não ter tocado.

### 4. errno → HTTP: a resposta mais honesta possível

Em vez de um 500 genérico para toda a falha de `remove()`/`rmdir()`,
o `deleteError()` mapeia o errno real:

| errno | HTTP | Racional |
|---|---|---|
| `EACCES`, `EPERM`, `EROFS` | **403** | percebemos o alvo; o FS recusou-nos — permissões/filesystem read-only são "proibido", não " servidor partido" |
| `ENOENT` | **404** | desapareceu no intervalo stat→remove (race, §3) |
| `ENOTEMPTY`, `EEXIST` | **409 Conflict** | o estado ATUAL do recurso (tem filhos) impede a operação — é a definição de 409 no RFC 7231 §6.5.9 |
| outro | **500** | verdadeiramente inesperado |

Um 403 por permissões e um 500 por erro interno são diagnósticos
completamente diferentes em avaliação — e um `DELETE /fulldir` a devolver
409 (em vez de 500) mostra que o servidor compreende o que lhe aconteceu.

### 5. Diretórios: vazio só, nunca recursivo

```cpp
bool DeleteHandler::removeDirectory(const std::string &path) const
{
    return (rmdir(path.c_str()) == 0);   // rmdir(2), não system("rm -rf")
}
```

`rmdir(2)` tem a garantia exata que queremos no kernel: falha com
ENOTEMPTY se o diretório tiver qualquer entrada. Não há recursão no
handler, não há shell, não há caminho de código que apague uma árvore.
Um `DELETE /` (com DELETE ativado na location `/`) esbarra no 409 —
o teste confirma que os filhos ficam intactos.

Remoção recursiva, se algum dia se quiser, é política de config
(estilo `dav_methods`), nunca um default silencioso.

### 6. 204, não 200

```
HTTP/1.1 204 No Content\r\n
\r\n
```

RFC 7231 §6.3.5: a resposta a um DELETE bem-sucedido "has no payload"
salvo relatório pedido. 200 com corpo ("File deleted") também é legal,
mas 204 é o idiomático — e o que o módulo dav do nginx responde. O
`res.clear()` antes do `setStatus()` garante que nenhum header prévio
escapa (mesma disciplina do `buildError`, aqui à mão porque não é erro).

### 7. removeFile() — `std::remove`, não `unlink`?

`std::remove(path)` (cstdio) chama `unlink(2)` para ficheiros e é
portátil C++98. Para ficheiros regulares é idêntico ao unlink. O
`isFile()` anterior já garantiu que era regular — symlinks resolvidos
pelo `stat()` do `isFile` (segue links): apagar `link.txt` apaga o
ALVO do link, não o link. Decisão consciente: para o efeito do webserv
(o cliente vê o caminho e quer o conteúdo desaparecido) é o
comportamento esperado; documentado aqui para ninguém estranhar.

### 8. O que foi verificado

14 testes em `test/handler_test.cpp`:

- ficheiro → 204 e **realmente apagado** do disco;
- diretório vazio → 204 e apagado;
- diretório com filhos → **409** e filhos intactos;
- alvo inexistente → 404;
- traversal `/../secret2.txt` → 403 e alvo intacto;
- location GET-only → 405 + `Allow: GET`, ficheiro intacto;
- location com lista vazia → 405 + `Allow: DELETE` (fail closed §2),
  ficheiro intacto;
- GET numa location DELETE-only → 405 + `Allow: DELETE`.

`make` limpo com `-Wall -Wextra -Werror -std=c++98`; suite completa
80/80.

<a id="cap-7"></a>

## 7. DirectoryListingHandler — autoindex seguro e barato

*Fonte: `MarkdownsHandlers/DIRECTORY_LISTING_HANDLER.md`*

---

Documentação de estudo do `DirectoryListingHandler`
(`includes/DirectoryListingHandler.hpp` + `srcs/DirectoryListingHandler.cpp`).
Complementa o `STATIC_HANDLER.md`: este handler é o **fallback de
autoindex** — raramente chamado diretamente, quase sempre via
`StaticHandler::serveDirectoryListing()` quando `autoindex on` e nenhum
index correspondeu.

Estado: **implementado e testado** (8 testes próprios; suite completa de
81 em `test/handler_test.cpp`).

---

### 1. Dois pontos de entrada — porquê

```cpp
e_status handle(const Request&, Response&, const ServerConfig&, const Locations&);
e_status serve(Response&, const std::string &dirPath, const std::string &urlPath);
```

- `handle()` — contrato normal do router: resolve `req.path` contra
  `loc.getRoot()` e verifica `isDirectory()` antes de listar.
- `serve()` — para quem **já resolveu o caminho** (o `StaticHandler` com
  o root efetivo `loc.root || srv.root`). Recebe `dirPath` (disco) e
  `urlPath` (o que aparece no título e nos links da página).

**Bug que isto corrige**: a primeira versão delegava via `handle()`, que
re-resolvia `req.path` contra `loc.getRoot()` — com root definido só no
server, a listagem apontava para o diretório errado. `serve()` elimina a
duplicação da resolução sem duplicar o estado: quem chamou decide o
caminho, o handler só renderiza.

### 2. listEntries() — readdir + ordenação manual

```cpp
std::vector<std::string> listEntries(const std::string &dirPath) const;
```

- `opendir`/`readdir`/`closedir` (dirent.h). `.` e `..` são descartados
  (o `..` de navegação para cima é trabalho do browser, não da página).
- **Ordenação por inserção escrita à mão** em vez de `std::sort`:
  C++98 puro sem `<algorithm>`, o vetor é pequeno, e o custo é
  irrelevante face ao readdir. Ordem determinística = páginas estáveis
  entre pedidos (o readdir não garante ordem nenhuma).
- `opendir` falhou → vetor vazio: o chamador já validou o caminho
  (403/404 vieram antes); listar vazio é a degradação segura.

### 3. buildHtml() — a página e os seus hrefs

Para `GET /sub/dir/` com `a.txt` e `pasta b/`:

```html
<h1>Index of /sub/dir/</h1>
<ul>
  <li><a href="/sub/dir/a.txt">a.txt</a></li>
  <li><a href="/sub/dir/pasta%20b">pasta b</a></li>
</ul>
```

Decisões:

| Decisão | Razão |
|---|---|
| hrefs **absolutos** (`urlPath + "/" + name`) | funcionam de qualquer profundidade; nada de `../` frágil |
| `urlEncode()` no href, `htmlEscape()` no título/h1 e no texto do link | o href é sintaxe da URL (espaço parte-o); o texto visível é conteúdo HTML — nomes de ficheiros e o `urlPath` entram como texto (`&lt;`, `&amp;`, `&quot;`), nunca como markup: um ficheiro chamado `<img src=x>` ou um path com `<script>` renderiza literal, sem XSS refletido |
| sem `/` final nos diretórios | clicar num diretório dá `GET /sub/dir/pasta b` sem slash → o `StaticHandler` responde **301 para a forma com slash** e o fluxo normal recomeça. O listing fica barato (0 `stat()` por entrada) e a normalização de URL fica num só sítio |
| escape **em todo o lado** (href via `urlEncode()`, texto via `htmlEscape()`) | o texto visível vem do filesystem e o título do request — ambos são input externo. Antes só os hrefs eram codificados; um ficheiro `<b>x` mostrava o nome a bold e `<script>` no path entrava no `<title>`. Agora tudo é inerte |

### 4. urlEncode() — o conjunto seguro mínimo

```cpp
static std::string urlEncode(const std::string &raw);
```

Codifica tudo o que **não** está no conjunto não-reservado do
RFC 3986 §2.3 (`ALPHA / DIGIT / - . _ ~`) como `%XX` hexuppercase.
Ser conservador aqui é grátis: um espaço vira `%20`, um `&` vira `%26`
(dentro de um atributo href isso importa), um `<` nunca parte o HTML do
link. Não se codifica o `urlPath` duas vezes: o parser **não** faz
percent-decoding (ver `REQUEST_RESPONSE.md` §1.7), o path chega cru e é
codificado uma única vez aqui — o que também quer dizer que um `%20` já
presente no pedido sai como `%2520`.

### 5. handle() direto — defesa em profundidade

Quando o router chama `handle()` diretamente (sem passar pelo
`StaticHandler`), a resolução defensiva continua lá:

```cpp
const std::string &root = loc.getRoot().empty() ? srv.getRoot()
                                                : loc.getRoot();
if (!resolvePath(root, req.path, dirPath) || !isDirectory(dirPath))
{
    buildError(res, 404);
    return (DONE);
}
```

Mesma política de root dos outros handlers (override da location,
senão a do servidor). Traversal nunca chega ao disco (403 do
`resolvePath`), e listar um caminho que não é diretório é 404 — o
handler não confia em ninguém, nem no futuro router.

### 6. Interação com o StaticHandler (quem chama quê)

```
GET /sub/  (autoindex on, sem index)
  StaticHandler
    ├─ método OK, resolvePath OK, isDirectory OK, slash OK
    ├─ serveIndex() → false (não há index.html)
    └─ loc.getAutoIndex() == true
         └─ DirectoryListingHandler::serve(res, "/www/sub", "/sub/")
              └─ 200 + text/html + página
```

O `DirectoryListingHandler` **não verifica métodos**: quem chega ao
autoindex já passou pelo check GET/HEAD do `StaticHandler`. Se um dia
for exposto diretamente no router, precisa do mesmo check — está
documentado aqui para não ser esquecido.

### 7. O que foi verificado

Testes relevantes em `test/handler_test.cpp`:

- `lister.serve()` sobre `/tmp/ws_test_root/sub` com `data.bin`,
  `index.html` e `with space.txt`:
  - 200 + `Content-Type: text/html`;
  - entradas **ordenadas** (ordem de inserção ≠ alfabética no FS);
  - espaços → `with%20space.txt` no href;
  - **nenhum caminho de disco** (`/tmp/...`) aparece no HTML — só URLs;
  - hrefs mantêm `/` literal (nada de `%2F` — regressão do bug dos
    links partidos);
- **escaping**: ficheiro chamado `<img>x.txt` e `a&b"c.txt` aparecem
  como `&lt;img&gt;x.txt` / `a&amp;b&quot;c.txt`; um `urlPath` hostil
  (`/<script>/x&y`) chega escapado ao `<title>` e `<h1>` — nenhum
  `<`/`&` cru chega ao HTML;
- autoindex on via `StaticHandler` → página "Index of /emptydir/"
  (integração §6);
- 81 testes totais, `make` limpo com `-Wall -Wextra -Werror -std=c++98`.

<a id="cap-8"></a>

## 8. RedirectHandler — o handler mais simples, e por isso mesmo interessante

*Fonte: `MarkdownsHandlers/REDIRECT_HANDLER.md`*

---

Documentação de estudo do `RedirectHandler`
(`includes/RedirectHandler.hpp` + `srcs/RedirectHandler.cpp`).
Primeiro handler concreto implementado; estabeleceu o padrão que os
outros seguiram (limpar a resposta, validar a config, construir, DONE).

Estado: **implementado e testado** (8 testes em `test/handler_test.cpp`).

---

### 1. Contrato — o que este handler decide e o que NÃO decide

Quando o router despacha um pedido para o `RedirectHandler`, a location
**é** um redirect: o parser preencheu `setReturn(target, code)` a
partir de um `return <code> <target>` (estilo nginx). O handler:

- **decide**: o código exato da resposta (o configurado), o valor do
  header `Location`, se há corpo (HEAD → não);
- **não decide**: se o pedido devia ser redirecionado (trabalho do
  router escolher esta location), nem reescrever caminhos
  (`rewrite` é outro mecanismo, não implementado).

### 2. Fluxo de handle()

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

#### Porquê 500 e não `ERROR` quando a config está mal

`e_status::ERROR` significa "o servidor falhou, fechar a conexão sem
resposta coerente" (HANDLER_BASE.md §3). Uma location de redirect sem
`return` configurado é um **erro de configuração** — o servidor está
saudável, o pedido é legítimo, e uma resposta 500 com página de erro é
exatamente o que um nginx faria. As duas guardas (target vazio, code
não-3xx) são a defesa contra o parser ainda não ter preenchido a
location — os stubs devolvem `""`/`0`, e o handler degrada com dignidade.

#### Porquê `res.clear()` num handler que "só" redireciona

Se o router ou outro layer pré-preencheu headers (Content-Type, um
corpo parcial), esses headers **não podem** escapar na resposta de
redirect. Mesmo padrão do `buildError()` (HANDLER_BASE.md §4).

### 3. buildTarget() — semântica do `return` do nginx

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

#### HEAD: Location sem corpo

O redirect em HEAD mantém `Location` e os headers todos mas devolve
corpo vazio (RFC 7231 §4.3.2). Navegadores que seguem redirects
ignoram o corpo de qualquer maneira — o corpo só existe para humanos
com clientes que não seguem.

### 4. Integração com a config (lado da config por ligar)

`Locations` guarda o redirect num `map<int, string>` preenchido pela camada de
config:

```cpp
void setReturn(const int &code, const std::string &target);  // parser chama isto
std::map<int, std::string> getReturn() const;                // vazio = não configurado
```

O parser chama `setReturn(code, target)` uma vez por diretiva
`return <code> <target>`; o handler usa a **primeira** entrada do mapa (o menor
código), por isso mais do que uma `return` na mesma location é ambíguo. Enquanto ninguém chamar, o handler responde
500 (ver §2) — nunca um redirect silencioso para o vazio.

### 5. O que foi verificado

Testes em `test/handler_test.cpp`:

- 301 com target absoluto passa verbatim (`https://example.com`);
- corpo menciona a reason phrase; `Content-Type: text/html`;
- target relativo + Host do pedido → `http://site1.com:8080/new`
  (a versão com join de path falhou aqui e foi corrigida, §3);
- HEAD → status 301 + Location, corpo vazio;
- `setReturn(200, "/new")` → 500 (code não-3xx);
- location sem config nenhuma → 500 (target vazio);
- 8 testes próprios + os partilhados do suite (81 no total).

`make` limpo com `-Wall -Wextra -Werror -std=c++98`.

<a id="cap-9"></a>

## 9. CGIHandler — fork, pipes, e a guerra contra os zombies

*Fonte: `MarkdownsHandlers/CGI_HANDLER.md`*

---

Documentação de estudo do `CGIHandler` (`includes/CGIHandler.hpp` +
`srcs/CGIHandler.cpp`). Assume `HANDLER_BASE.md` (`e_status`, helpers) e
`REQUEST_RESPONSE.md` (`Request` corpo já descodificado).

Estado: **implementado e testado** — 15 testes próprios em
`test/handler_test.cpp` (81 no total no suite), com scripts Python reais.

---

### 1. O modelo de execução — síncrono hoje, NEED_CGI amanhã

```
handle()
  ├─ método GET/POST?           → 405 + Allow: GET, POST
  ├─ resolvePath()              → 403 (traversal nunca chega ao execve)
  ├─ isScript()? (extensão)     → 400 (não-CGI numa location CGI)
  ├─ isFile()?                  → 404
  ├─ interpreter configurado?   → 500
  ├─ launch()  pipe+fork+dup2+execve   falha → ERROR (servidor partido)
  ├─ feed: body → stdin do filho       (EPIPE-tolerante)
  ├─ read: stdout do filho → string    (UM deadline partilhado, cap 10MB)
  ├─ reap: waitpid (SIGKILL se passou o deadline)
  └─ parse: headers CGI + corpo → Response
```

O `handle()` corre tudo dentro da chamada: **lança, alimenta, lê,
ceifa, parseia** — e devolve `DONE`. O `NEED_CGI` (HANDLER_BASE.md §3)
existe para o modelo assíncrono em que o `Server` faz poll nos pipes
entre iterações do loop; como o `Server` ainda não despacha handlers,
a versão síncrona já está correta e testada, e o handler expõe
`getCgiPid()` / `getStdinFd()` / `getStdoutFd()` para essa integração
reutilizar o `launch()` sem redesenho. O que muda no modelo assíncrono
é **quem** faz o feed/read/reap (o loop do Server), não **como**.

### 2. O bug mais subtil do projeto — argv sem o script

A primeira versão do filho fazia:

```cpp
argv = { interpreter, NULL };        // python3, sem argumentos!
execve(interpreter, argv, envp);
```

Resultado: o `python3` arrancava **sem script**, e um interpretador sem
ficheiro lê o PROGRAMA do stdin — que é o corpo do pedido (vazio nos
GETs). Executava zero bytes, saía com 0, produzia output vazio → todos
os casos de sucesso degradavam para 502 **sem nenhum erro visível**,
porque tudo "funcionou" do ponto de vista do processo.

O padrão dos falhas denunciou: só passavam os testes que esperavam
fracasso. Correção:

```cpp
argv = { interpreter, scriptPath, NULL };   // o script É o argumento
```

Lição para a avaliação: um CGI que responde vazio com exit 0 não é
"script mal escrito" por defeito — é sintoma de execução sem argumento.
E o teste de timeout (504) também falhava *por causa disto*: o
`time.sleep(60)` nunca chegou a correr.

### 3. launch() — a dança dos pipes e do fork

```
PARENT                          CHILD
stdinPipe[1]  ──(body)──►  stdinPipe[0] → dup2 → fd 0 (stdin)
stdoutPipe[0] ◄──(out)──  stdoutPipe[1] ← dup2 ← fd 1 (stdout)
```

Detalhes que importam:

| Passo | Porquê |
|---|---|
| `dup2(pipe, 0/1)` e SÓ DEPOIS fechar os 4 fds originais no filho | o filho herdou AMBAS as pontas de AMBOS os pipes; se ficasse com `stdoutPipe[0]` aberto, o EOF nunca chegava ao pai — o `read` do pai esperava para sempre |
| stderr herdado do pai | erros de script aparecem na consola do servidor (um deploy real redireciona para log); escondê-los tornava o debugging impossível |
| `_exit(127)` se o execve falha | `exit()` (o normal) executaria os atexit/flushes do pai herdados pelo fork — corrompendo buffers duplicados. `_exit` é a saída de emergência correta num filho pós-fork |
| pai fecha as pontas que não usa, imediatamente | mesmo motivo do EOF; e `fcntl(O_NONBLOCK)` nas duas pontas que ficam |
| `pipe()` a falhar a meio → fechar o par já criado | fd leak em fd exhaustion é como se apanha: o próximo pedido nem `pipe()` consegue fazer |

### 4. SIGPIPE — o assassino silencioso do servidor

O primeiro `write()` num pipe cujo leitor morreu levanta **SIGPIPE**,
que por default **termina o processo**. Sem `signal(SIGPIPE, SIG_IGN)`,
um CGI que sai cedo matava o servidor inteiro no meio do feed — não um
500, não um log: morte instantânea. Ignorado o sinal, o `write()`
devolve `-1`/`EPIPE` e o handler responde 502, como deve ser.

`writeAllNonblocking()` trata o EPIPE como "o filho decidiu sair":
`false` → 502 a montante. Nunca crash, nunca retry infinito.

### 5. UM deadline, não vários

```cpp
time_t deadline = nowMs() + timeoutMs;   // default 5000 ms
```

Feed, read e reap partilham o **mesmo** instante-limite. Com orçamentos
por fase, um CGI malicioso que escreve 4.9s de output e depois trava
teria 4.9s novos de paciência no reap — e mais noutro ciclo. Com
deadline único: 504 assim que o total passa.

O timeout é configurável por env (`WEBSERV_CGI_TIMEOUT_MS`) EXCLUSIVAMENTE
para o teste do 504 correr em 300ms em vez de 5s — produção usa o
default de 5s.

O read é limitado em **tempo E espaço** (`CGI_MAX_OUTPUT` 10MB): um
pipe ilimitado é um vetor de exaustão de memória remota.

### 6. O ambiente do filho (RFC 3875 §4.1)

`buildEnv()` monta as metavariables:

| Variável | Valor | Quem usa |
|---|---|---|
| `REQUEST_METHOD` | `req.method` | scripts para distinguir GET/POST |
| `QUERY_STRING` | `req.query` (sem `?`) | GET com parâmetros |
| `CONTENT_LENGTH` | tamanho do body (só se > 0) | o script lê N bytes do stdin |
| `CONTENT_TYPE` | header do pedido | POST de formulários |
| `PATH_INFO` / `SCRIPT_NAME` | `req.path` | routing dentro do script |
| `SCRIPT_FILENAME` | caminho RESOLVIDO no disco | o que o interpretador abre |
| `SERVER_PORT` | **vazio hoje** — o Server preenche na integração (ver `INTEGRATION_ROUTER.md` §3) | o script sabê a porta de escuta |
| `SERVER_PROTOCOL` | `req.version` | negociação do script |
| `GATEWAY_INTERFACE` | `CGI/1.1` | o script pergunta "sou CGI?" |
| `HTTP_USER_AGENT/ACCEPT/COOKIE` | headers selecionados | namespace `HTTP_*` do RFC |

Gestão de memória sem armadilhas: as strings vivem no **membro**
`_envStore` (vector<string>, limpo a cada build) e o array devolvido só
contém `c_str()` apontando para lá. O `freeEnv()` só liberta o array de
ponteiros — nenhum leak possível entre tentativas, nenhum ponteiro
pendente no filho.

**O interpretador NUNCA vem do cliente**: vem de `loc.getCgiPath()`
(directiva `cgi_path`, config do servidor). Nem o path do
script vem cru: vem do `resolvePath()` contra a root.

### 7. Parse do output CGI (RFC 3875 §6.2)

```
Status: 404 Not Found\r\n      ← opcional; default 200
Content-Type: text/html\r\n    ← OBRIGATÓRIO
Location: /elsewhere\r\n       ← redirect gerado pelo script
X-Custom: value\r\n            ← passado ao cliente como está
\r\n
corpo, byte a byte
```

- `parseCgiOutput()` separa headers/corpo no primeiro `\r\n\r\n`
  (com tolerância a `\n\n` — scripts escritos à mão geram bare-LF);
- `Status:` define código + frase; fora de [100, 599] → 502;
- **sem `Content-Type` → 502**: o CGI/1.1 exige; um script que só faz
  `print("oi")` não produz uma resposta HTTP, e o handler diz 502 em
  vez de adivinhar;
- headers restantes passam ao cliente com a casing original.

### 8. Taxonomia de erros — 502 vs 504 vs ERROR

| Situação | Resposta | Racional |
|---|---|---|
| script sai != 0 | **502** | o "upstream" disse que falhou; RFC 7231 §6.6.3 |
| exit 0 mas sem headers / sem Content-Type | **502** | output inválido como resposta HTTP |
| output > 10MB | **502** | excedeu o cap; não é timeout, é abuso |
| passou o deadline | **504** | Gateway Timeout — exatamente o caso de uso do código |
| `pipe()`/`fork()` falha | **e_status ERROR** (+500 montado) | o SERVIDOR está sem fds/memória; a conexão atual nem resposta fiável merece (HANDLER_BASE.md §3) |
| extensão não configurada | **400** | pediu-se execução de um não-script |
| script ausente | **404** | não existe |
| sem interpreter na config | **500** | erro de configuração do servidor |

O destrutivo do `exit != 0 → 502`: mesmo que o script tenha imprimido
headers válidos antes de falhar, o output é descartado. Um CGI que
responde 200 e depois crasha não pode entregar um 200 "quase certo".

### 9. O reap — sem zombies, sem esperas infinitas

```cpp
for (;;) {
    pid_t r = waitpid(_cgiPid, &status, WNOHANG);   // não bloqueia
    if (r == _cgiPid) break;                        // saiu
    if (r < 0) break;                               // já ceifado
    if (nowMs() > deadline) { timedOut = true;
        kill(_cgiPid, SIGKILL); waitpid(_cgiPid, &status, 0); break; }
    usleep(20000);
}
```

`WNOHANG` + sono curto = poll manual (o modelo assíncrono substitui
isto pelo poll do Server). No timeout: SIGKILL e `waitpid` **bloqueante**
de seguida — o SIGKILL não pode ser ignorado, o wait devolve. O
destrutor também ceifa: um Server destruído no meio de um CGI não
deixa zombie nem fd aberto.

### 10. Segurança — postura completa

1. **Traversal**: `resolvePath()` antes de qualquer syscall (403);
2. **Extensões**: só as configuradas executam (400);
3. **Interpretador**: da config, nunca do cliente (500 se em falta);
4. **Script inexistente**: 404 antes do fork;
5. **Output limitado** em tempo e espaço;
6. **SIGPIPE ignorado**: cliente/CGI hostil não derruba o processo;
7. **Sem shell**: `execve` direto do interpretador — nada passa por
   `system()`/`sh -c`, logo injeção de comandos não tem superfície.

### 11. O que foi verificado

15 testes com Python real em `test/handler_test.cpp`:
- `ok.py` → 200 + corpo + Content-Type honrado;
- `status.py` → `Status: 404` vira 404 com corpo do script;
- `loc.py` → header `Location` passado ao cliente;
- `post.py` → corpo via stdin + `CONTENT_LENGTH` correto
  (`len=9 body=hello cgi`);
- `query.py` → `QUERY_STRING=a=1&b=two` no ambiente;
- `fail.py` (exit 3) → 502; `malformed.py` (sem headers) → 502;
- `loop.py` (sleep 60) → **504** em 300ms (env de teste);
- `readme.txt` na location CGI → 400; `ghost.py` → 404;
- `/../evil.py` → 403 e o ficheiro não tocou;
- DELETE → 405 + `Allow: GET, POST`;
- sem interpreter → 500; interpreter inexistente → 502 (exit 127).

`make` limpo com `-Wall -Wextra -Werror -std=c++98`; suite completa
**80/80**.

<a id="cap-10"></a>

## 10. CHEATSHEET — webserv

*Fonte: `MarkdownsHandlers/CHEATSHEET.md`*

---

**Parte 1** — camada de handlers (Pessoa C) · **Parte 2** — parsing HTTP, config e rede (Pessoas A e B)

Referência rápida de **todas as funções implementadas** nos handlers do webserv (C++98).
Ficheiros: `includes/*Handler*.hpp` + `includes/Handler.hpp` e os respetivos `.cpp` em `srcs/`.

Parte 2 cobre: `Request`, `Response`, `Config`, `ConfigFile`, `ConfigParser`, `ServerConfig`, `Locations`, `Client`, `Server`, `main` (em `includes/` + `srcs/`).

---

### Mapa geral

```
Handler (abstrata)
├── StaticHandler          GET/HEAD em ficheiros, index, fallback de listing
├── UploadHandler          POST (multipart ou raw) → grava em upload_store
├── DeleteHandler          DELETE de ficheiros e diretórios vazios
├── DirectoryListingHandler autoindex em HTML
├── RedirectHandler        301/302/... com Location
└── CGIHandler             fork/execve de scripts com pipes
```

Assinatura comum de entrada:

```cpp
e_status handle(const Request &req, Response &res,
                const ServerConfig &srv, const Locations &loc);
```

Root usada: `loc.getRoot()` se não vazia, senão `srv.getRoot()` (padrão em todos os handlers).

---

### 1. Handler (classe base)

#### Tipos

| Enum `e_status` | Significado |
|---|---|
| `DONE` | Resposta construída, pronta a enviar |
| `NEED_CGI` | Manter ligação aberta, fazer polling do processo CGI |
| `ERROR` | Falha interna (ex.: `fork`/`pipe`), fechar a ligação |

#### Funções

| Função | O que faz |
|---|---|
| `buildError(Response&, int)` | `res.clear()` **primeiro** (nunca deixar headers de meio-de-caminho escapar para a página de erro) + `res.setError(code)` |
| `notAllowed(Response&, const Locations&)` | 405 + header `Allow` com os métodos da location (RFC 7231 §6.5.5). Ordem: `buildError` **depois** set `Allow` |
| `resolvePath(root, requestPath, outPath)` → `bool` | Junta root + path **segmento a segmento**: colapsa `.`, resolve `..` (se escapar à root → `false`), rejeita NUL embutido. Root normalizada sem `/` final. O path **não** é percent-decoded: `%2e%2e` é o nome literal de um ficheiro, não `..` (ver `REQUEST_RESPONSE.md` §1.7) |
| `isMethodAllowed(req, loc)` → `bool` | Lista vazia de `allow_methods` = **tudo permitido** (estilo nginx). Senão, compara método a método |
| `isFile(path)` → `bool` | `stat()` + `S_ISREG` (segue symlinks). `false` se não existir |
| `isDirectory(path)` → `bool` | `stat()` + `S_ISDIR`. `false` se não existir |

#### Regras de ouro da base

1. **`buildError()` faz `clear()`** → qualquer header (ex.: `Allow`) tem de ser posto **depois** dela.
2. **405 tem duas fontes** em todos os handlers: política da location (`notAllowed()`, Allow = lista configurada) e capacidade do handler (Allow fixo, ex.: `GET, HEAD`).
3. **Path traversal morre em `resolvePath`** antes de qualquer syscall.
4. `handle()` é `= 0` → cada handler concreto implementa.

---

### 2. StaticHandler

**Fluxo do `handle()`**: método (405 ×2) → root → `resolvePath` (403) → diretório? (301 sem slash / index / autoindex / 403) → ficheiro → 200.

| Função | O que faz |
|---|---|
| `handle(req, res, srv, loc)` | Orquestra: 405 se método ∉ {GET, HEAD} (Allow: `GET, HEAD`); resolve root (location > server); 301 para diretório sem `/` final (query string sobrevive); tenta indexes (location > server); `autoindex on` → listing; senão 403. Ficheiro inexistente → 404 |
| `serveFile(filePath, req, res)` | Lê ficheiro binário via `ifstream` + `ostringstream`. Erro de open → 403 se ainda existe (permissões), 404 se desapareceu; leitura corrompida → 500. 200 + `Content-Type` pelo extension. **HEAD**: mesmos status + headers + `Content-Length`, corpo vazio (RFC 7231 §4.3.2) |
| `serveIndex(dirPath, indexes, req, res)` → `bool` | Testa cada index candidato (`dir/index[i]`), serve o primeiro que for ficheiro. `false` se nenhum existir (caller cai para autoindex/403) |
| `serveDirectoryListing(req, res, dirPath)` | Delega num `DirectoryListingHandler` temporário via `serve()` — o `dirPath` já vem resolvido |
| `guessContentType(filePath)` → `const char*` | Extensão (lowercased) → MIME. Tabela: `.html/.htm→text/html`, `.css`, `.js`, `.json`, `.png`, `.jpg/.jpeg`, `.gif`, `.svg`, `.ico`, `.txt`, `.pdf`. Desconhecida → `application/octet-stream` (browser faz download em vez de adivinhar) |

---

### 3. UploadHandler

**Fluxo do `handle()`**: método (405 ×2, Allow: `POST`) → tamanho (413) → config (500) → multipart? → parse (400) → sanitize (400) → save → 201/500.

Tamanho é verificado **antes de tocar no disco**. Sem `client_max_body_size` configurado → **fail closed** (413).

| Função | O que faz |
|---|---|
| `isBodyTooLarge(req, srv)` → `bool` | `srv.getBodySize() <= 0` → demasiado grande (sem limite = sem uploads). Senão: corpo real **ou** `Content-Length` declarado > limite → `true` |
| `extractFilename(req)` | Nome do ficheiro = último segmento do URL (raw body mode). A query nunca chega aqui (`req.path` não a inclui) |
| `saveFile(storeDir, filename, body)` → `bool` | `ofstream` binário + `trunc` em `storeDir/filename`. `false` em erro de open/write |
| `getBoundary(req)` | `boundary=` do header `Content-Type` (case: parser já normalizou). Suporta `boundary=token` e `boundary="quoted"`. Trim de espaços; token acaba em `;` ou fim. `""` se não houver |
| `extractDispositionFilename(partHeaders)` | Valor de `filename=` no `Content-Disposition` de uma part (quoted ou bare; bare acaba em `;`/CRLF). Trabalha sobre cópia **lowercased** para encontrar (indices mantêm-se, mesmo tamanho) mas extrai do original — `""` se for form field sem ficheiro |
| `parseMultipart(body, boundary, out)` → `bool` | RFC 2046 core: delimitador = `\r\n--boundary`; fim = `--boundary--`. Preamble ignorado; headers da part até `\r\n\r\n`; conteúdo até `\r\n--boundary` seguinte (o `\r\n` antes do boundary é do delimitador — não contamina o ficheiro!). Parts sem `filename=` são fields → ignoradas. Stream malformado → `false` → 400 |
| `sanitizeFilename(raw)` | **Rejeita, não reescreve**: NUL → `""`; qualquer segmento `.`/`..` → `""` (traversal hostil); senão fica só o **basename** (strip de `/` e `\`, o que mantém `C:\fakepath\x.txt` dos browsers); control chars removidos; máx. 200 bytes (baixo do `NAME_MAX` 255). `""` se nada sobrar |

Sucesso: **201 Created** + header `Location: store/nome` + corpo `Created: <paths>` (multipart com várias parts junta todas no `Location`).

---

### 4. DeleteHandler

O mais conservador dos seis — DELETE é destrutivo.

**Fluxo do `handle()`**: política de método → `resolvePath` (403) → ficheiro? (`remove`, 204) → diretório? (`rmdir`, 204) → senão 404.

| Função | O que faz |
|---|---|
| `handle(req, res, srv, loc)` | DELETE tem de estar **explicitamente** em `allow_methods` — lista vazia também recusa (fail closed, diferente dos handlers read-only!). Método diferente com DELETE permitido → `notAllowed()` (Allow = lista configurada). Lista vazia → 405 + `Allow: DELETE`. 404 antes dos checks de permissão (caso comum, não pode ser mascarado) |
| `removeFile(path)` → `bool` | `std::remove()`; `errno` preservado para mapeamento |
| `removeDirectory(path)` → `bool` | `rmdir()` — falha **por design** em diretório não vazio (`ENOTEMPTY`) = garantia "nunca recursivo" |
| `deleteError(res, err)` | `errno` → código HTTP honesto (tabela abaixo) |

**Mapa errno → HTTP:**

| errno | HTTP |
|---|---|
| `EACCES`, `EPERM`, `EROFS` | 403 |
| `ENOENT` | 404 (desapareceu entre stat e remove) |
| `ENOTEMPTY`, `EEXIST` | 409 Conflict |
| outro | 500 |

Sucesso: **204 No Content** (sem corpo, RFC 7231 §6.3.5; nginx dav também responde 204).

---

### 5. DirectoryListingHandler

**Filosofia**: buraco de propósito — confia nos inputs. O caller (StaticHandler/router) já resolveu o path com segurança e decidiu que se deve listar. O trabalho dele é só: `readdir` → sort → render.

| Função | O que faz |
|---|---|
| `handle(req, res, srv, loc)` | Root (location > server) + `resolvePath` defensivo; não é diretório → 404; senão `serve()` |
| `serve(res, dirPath, urlPath)` | Entrada para quem **já resolveu** o diretório (StaticHandler). `dirPath` = path no disco, `urlPath` = path do pedido (título + links). 200 + `text/html` |
| `listEntries(dirPath)` → `vector<string>` | `opendir`/`readdir`; descarta `.` e `..`; **insertion sort** alfabético (C++98 puro, sem `<algorithm>`). `opendir` falho → vector vazio |
| `buildHtml(dirPath, urlPath, entries)` | Página `<h1>Index of ...</h1>` + `<ul>` de links absolutos. Cada **segmento** do `urlPath` é percent-encoded separadamente (encoding do path inteiro viraria os `/` em `%2F` e partia todos os hrefs!). Normaliza `/` final. Título e nomes passam por `htmlEscape` |
| `urlEncode(raw)` *(static)* | RFC 3986 §2.3: só `A-Za-z0-9 - . _ ~` ficam; resto → `%XX` (hex uppercase). Impede espaços/`&`/`<` de quebrarem hrefs |
| `htmlEscape(raw)` *(static)* | `&`→`&amp;`, `<`→`&lt;`, `>`→`&gt;`, `"`→`&quot;` (uma passagem só). Anti-XSS: um ficheiro chamado `<script>...` renderiza como texto inerte |

Notas: entradas de diretórios ficam **sem** `/` final — o click gera pedido sem slash, que o StaticHandler responde com 301 para a forma com slash.

---

### 6. RedirectHandler

**Filosofia**: a única decisão por-location é `code` + `target` (tipo nginx `return 301 https://exemplo.com`). Chegar a este handler é responsabilidade do router.

| Função | O que faz |
|---|---|
| `handle(req, res, srv, loc)` | Usa a **primeira** entrada de `loc.getReturn()` (o menor código); mapa vazio, target vazio ou código 0 → **500** (config mal feita, não `ERROR` — a ligação está bem). Código fora de [300, 399] → 500. Senão: `res.clear()`, status 3xx + `Location` + página HTML mínima (sem corpo se HEAD) |
| `buildTarget(req, redirectConfig)` | Semântica `return` do nginx: config com `://` → usada verbatim (redirect absoluto); senão → `"http://" + header Host + target` (mesmo host). O target **nunca** é concatenado com o path do pedido. Sem Host (HTTP/1.0) → target nu (Location relativa válida) |

---

### 7. CGIHandler

**Modelo síncrono dentro de `handle()`**: launch → feed stdin → read stdout → reap → parse.

Constantes: timeout **5000 ms** (um deadline único para a troca toda, não por fase; override só para testes via `WEBSERV_CGI_TIMEOUT_MS`), cap de output **10 MB**.

**Fluxo do `handle()`**:
1. `signal(SIGPIPE, SIG_IGN)` idempotente — sem isto, o primeiro write para um filho morto mata o servidor todo
2. método ∉ {GET, POST} → 405 + `Allow: GET, POST`
3. `resolvePath` → 403 (traversal nunca chega ao `execve`)
4. `isScript` → 400 (recurso não-CGI numa location CGI)
5. `isFile` → 404
6. `getCgiPath()` vazio → 500 (interpretador vem da location, nunca do cliente)
7. `launch()` falho → 500 + **`ERROR`** (o servidor é que está partido, ex. fds esgotados)
8. feed stdin (tolerante a EPIPE) → fechar write end (EOF para o filho) → read stdout com deadline + size cap
9. reap com `waitpid(WNOHANG)` em loop de 20 ms; se passar o deadline → `SIGKILL` + `waitpid`
10. outcomes: `timedOut` → **504**; `!fed` / `tooBig` / `exitCode != 0` / output vazio → **502**; parse falho → **502**; ok → 200 (ou o `Status:` do CGI)

| Função/membro | O que faz |
|---|---|
| `isScript(filePath, extensions)` → `bool` | Recebe um `vector<string>` de extensões que o `handle` monta a partir de `loc.getCgiExtension()` (um valor por location); compara a extensão do ficheiro em minúsculas, com ponto (tolera config com ou sem `.`). Lista vazia → `false` |
| `buildEnv(interpreter, scriptPath, req)` → `char**` | Metavariáveis RFC 3875: `GATEWAY_INTERFACE=CGI/1.1`, `SERVER_SOFTWARE`, `SERVER_PROTOCOL`, `SERVER_PORT` (vazio — preenchido na integração), `REQUEST_METHOD`, `QUERY_STRING`, `PATH_INFO`, `SCRIPT_NAME`, `SCRIPT_FILENAME`, `CONTENT_LENGTH` (se body), `CONTENT_TYPE`, e passthrough `HTTP_USER_AGENT` / `HTTP_ACCEPT` / `HTTP_COOKIE`. Strings vivem em `_envStore` (membro) — o array só guarda `c_str()` |
| `freeEnv(env)` | `delete[]` só do array de ponteiros (as strings são do `_envStore`) |
| `launch(scriptPath, interpreter, req)` → `bool` | 2 × `pipe()` → `fork()`. **Filho**: `dup2` stdin←body, stdout→CGI; fecha os 4 fds originais; `argv = { interpreter, script, NULL }` (**o script tem de ser argumento do interpretador** — sem isto o `python3` lê o body como programa e tudo degrada para 502!); `execve`; falhou → `_exit(127)`. **Pai**: guarda pid, fecha extremos opostos, `fcntl O_NONBLOCK` nos dois (poll() nunca pode bloquear o servidor) |
| `~CGIHandler()` | Destrutor: `SIGKILL` + `waitpid` se houver filho vivo (nunca zombies), fecha os 4 fds |
| `_cgiPid`, `_stdinPipe[2]`, `_stdoutPipe[2]`, `_envStore` | Estado do processo CGI: pid (-1 se nenhum), pipes `[0]=read [1]=write` (-1 fechado), strings de ambiente do request atual |
| `getCgiPid()` / `getStdinFd()` / `getStdoutFd()` | Introspecção para a futura integração NEED_CGI no loop do Server (pid; write end pai→filho; read end filho→pai) |

#### Helpers `static` no `.cpp` (namespace anónimo)

| Função | O que faz |
|---|---|
| `nowMs()` | `gettimeofday` → ms |
| `closeEnd(fd&)` / `closePipePair(p)` | Fecha e põe a -1 |
| `toLowerCopy` / `trimCopy` | Cópia lowercase / trim de `" \t"` |
| `writeAllNonblocking(fd, data, deadline)` → `bool` | Escreve tudo com `poll(POLLOUT)` antes de cada write; tolera `EAGAIN`/`EINTR`; **EPIPE = decisão do filho, não crash** |
| `readAllWithDeadline(fd, deadline, timedOut&, tooBig&)` | Lê até EOF com `poll(POLLIN)`; limitado por tempo **e** tamanho (10 MB — ler pipe sem limite = vetor de exaustão de memória) |
| `parseCgiOutput(raw, res)` → `bool` | RFC 3875 §6.2: separa headers/corpo no `\r\n\r\n` (tolera `\n\n` de scripts caseiros). `Status:` → `setStatus` (100–599, senão falha); `Content-Type` → **obrigatório**; `Location:` e restantes headers → passthrough (casing original). Sem bloco de headers ou sem Content-Type → `false` → 502 |

---

### Tabela-resposta dos handlers

| Handler | Sucesso | Erros que emite |
|---|---|---|
| Static | 200 (ou 301 dir sem slash) | 403 traversal/autoindex off/sem permissão, 404, 405, 500 |
| Upload | 201 + `Location` | 400 multipart/nome, 405, 413 (fail closed!), 500 config/disco |
| Delete | 204 sem corpo | 403 traversal/permissão, 404, 405 (+Allow), 409 não vazio, 500 |
| Listing | 200 `text/html` | 404 (não é diretório / resolve falhou) |
| Redirect | 3xx + `Location` | 500 config (sem target, código fora de 3xx) |
| CGI | 200 (ou `Status:` do script) | 400 extensão, 404 script, 405, 500 config/fork+pipe, 502 exit≠0/malformado, 504 timeout, `ERROR` e_status |

### Armadilhas que os testes apanharam

1. **`Allow` depois de `buildError`** — o `clear()` apaga headers postos antes (apanhado no Static).
2. **`\r\n` do multipart pertence ao delimitador** — ficheiro terminado em newline não ganha um extra.
3. **Script como argumento do interpretador** no `execve` — sem isso, exit 0 silencioso e 502 em tudo.
4. **Fail closed** em toda a lado: sem limite de body → 413; DELETE não listado → 405; sem extensões CGI → não executa.
5. **Não reescrever nomes hostis** — `../../evil.txt` → 400, nunca `evil.txt` silenciosamente.

---

### PARTE 2 — Parsing HTTP, Config e Rede

#### 8. Request

Campos públicos: `method`, `path`, `query`, `body`, `version`, `headers` (map
de chaves **lowercase**). O estado do handshake vive em privados:
`_hasContent`, `_hasTransferEncoding`, `_contentLen`, `_body`,
`_nextRequestBytes`, `_requestLine`, `_headers`, `_server`, `_serverName`.

Não há `_valid` nem `_errorStatus`: o parser **lança** `std::runtime_error` e o
`catch` do `Server::handleRead` responde **400**. Também não há limites de 8192
(headers) nem 2048 (target) — o buffer cresce até o corpo estar completo.

| Função | O que faz |
|---|---|
| `checkHeader(buffer)` → `bool` | `true` quando o buffer contém `"\r\n\r\n"` (só CRLF: um cliente que mande `\n\n` fica preso). Lê `Content-Length` / `Transfer-Encoding` dos headers **crus** e acende `_hasContent` / `_hasTransferEncoding` |
| `readingBody(buffer)` → `void` | Valida e guarda o enquadramento: CL só com dígitos (senão lança) e TE **exatamente** `chunked` (case-sensitive; senão lança). É chamada duas vezes — aqui e dentro de `start_parsing` |
| `checkingBody_framing(buffer)` → `bool` | Content-Length: compara os bytes a seguir ao fim dos headers com `_contentLen`. Menor → `false` (espera mais `recv`); maior → imprime `"too many bytes"`, guarda `_nextRequestBytes` (que nunca é lido) e devolve `true` |
| `checkingBody_chuncked(buffer)` → `bool` | Chunked: `true` quando o terminador (`0` + CRLF + trailers) já está no buffer (grafia `chuncked` é a do código) |
| `start_parsing(buffer)` → `void` | Interpreta de facto: request line, query, headers, `Host` e corpo; preenche os campos públicos |
| `RequestLineParsing` · `QueryParsing` · `HeadersToMap` · `HostParsing` · `parseBody` · `parseChunkedBody` | Peças de `start_parsing`, públicas e chamáveis à parte |
| `MethodParsing(method)` | Nome enganador: não olha ao método (`(void)method`) — escolhe a `Locations` de **prefixo mais longo** cujo path case com fronteira de `/`, e lança `"Location not found"` se nenhuma casar |
| `getHasContentStatus()` / `getTransferEncodingStatus()` | Se vieram `Content-Length` / `Transfer-Encoding` nos headers |
| `getContentLength()` | `_contentLen`, preenchido por `readingBody` |
| `getHeader(name)` | Lookup no map; como as chaves ficam **lowercase** e o argumento não é normalizado, `getHeader("Host")` devolve `""` |

Não existem `parse()`, `isComplete()`, `peekContentLength()`, `hasBody()`,
`urlDecode()`, `isValid()`, `getErrorStatus()` nem `clear()`: a API que a versão
anterior desta tabela descrevia nunca foi escrita (ver `REQUEST_RESPONSE.md`
§1.2). O único código de erro do parser é **400** — 411/414/431/501/505 não são
emitidos.

### 9. Response

Campos públicos: `statusCode`, `reasonPhrase`, `headers` (map — saem alfabéticos na serialização), `body`.

| Função | O que faz |
|---|---|
| `setStatus(code, reason)` | reason vazio → frase standard |
| `setHeader` / `setBody` / `hasLocation()` | `hasLocation` é case-insensitive |
| `clear()` | Volta a 200 OK vazio (por isso `Allow` tem de ir **depois** de `buildError`!) |
| `getReasonPhrase(code)` *(static)* | Tabela ~30 códigos (200 → 505, inclui 418 "I'm a teapot"); fora → `"Unknown"` |
| `setError(code)` | Página HTML de erro default (estilo `<center>webserv</center>`) |
| `setError(code, customBody)` | Igual com body próprio (para `error_page` no futuro); impõe `Content-Type: text/html` + `Content-Length` |
| `toString()` | Serializa: status line + headers + body. **Auto-preenche** o que o handler não pôs: `Content-Length`, `Server: webserv/1.0`, `Date` em GMT (`strftime "%a, %d %b %Y %H:%M:%S GMT"`) |

#### 10. Locations

Um bloco `location { }`. Documentação da Parte 1 cobre como os handlers a usam; aqui os acessores/setters:

| Par | Uso |
|---|---|
| `getPath` / `setPath` | prefixo da location (fill pelo parser) |
| `getAllowedMethods` / `setAllowedMethod` | um por token de `allow_methods` (o nome é `set`, mas faz `push_back`) |
| `getReturn` / `setReturn(code, target)` | `map<int, string>`; uma chamada a `setReturn` por `return <code> <target>` (estilo nginx) |
| `getRoot`/`setRoot` · `getIndexes`/`setIndex` · `getAutoIndex`/`setAutoIndex` | static + listing |
| `getUploadStore` / `setUploadStore` | upload |
| `erase()` | Limpa todos os campos antes de um novo bloco `location` (`handleLocation` chama-a) |
| `getCgiExtension`/`setCgiExtension` · `getCgiPath`/`setCgiPath` | CGI (um valor por location, não vectores) |

Convenção: vazio/0/false = herdar do server — **excepto DELETE** (fail closed, §4).

#### 11. Cascata de Config (Pessoa A)

`Config.parse(filename)` → `ConfigFile.read_and_append` → `Tokenize::tokenize` →
`parseServers` → `vector<ServerConfig>`.

| Classe | Função | O que faz |
|---|---|---|
| `Config` | `parse(filename)` | Orquestra as três classes e guarda o resultado em `_servers`; é **`void`** — não devolve estado, lança em erro |
| `ConfigFile` | `openFile` / `read_and_append` | `open(O_RDONLY)` + loop `read` de 1KB até EOF; tudo vai para `_inputText`; `get_str` devolve a referência |
| `ConfigParser` | `tokenize` | `{`, `}` e `;` viram tokens isolados; whitespace delimita |
| `ConfigParser` | `parseServers(tokens)` | Exige um bloco `server` em cada iteração (senão lança) e lança se não houver nenhum |
| `ConfigParser` | `parseServerInfo(tokens, i)` | `bracket_count` para `{}` aninhados; directives: `listen` (só dígitos, 1–65535, senão **lança**), `root` (valor único), `index` e `server_name` (listas até `;`), `client_max_body_size`, `error_page <código> <ficheiro>;` (código limitado a 400/403/404/405/413/500/501) e `location <path> { … }`. Token desconhecido → `"Directive not allowed in server"`, não é ignorado |
| `ServerConfig` | getters/setters | port, bodySize, root, index e serverName (vectors), errorPages (`map<int, string>`) e locations (`addLocation` / `getLocations`) |

`client_max_body_size` passa por `parseBodySizeWeight`: dígitos mais um sufixo
opcional **K**, **M** ou **G** com multiplicadores **decimais** (×1000, ×10⁶, ×10⁹ —
não 1024). `0`, sufixo desconhecido e overflow de `size_t` lançam.

**Dentro de `location <path> { … }`** o parser preenche uma `Locations` completa:
`handleLocation` faz `_loc.erase()` no início, consome as directivas permitidas e
`handleLocationDirective` guarda-a com `_serv.addLocation(...)`.

| Directiva de location | Efeito |
|---|---|
| `root` | `setRoot` — ausente, o handler herda o root do server |
| `index` | `setIndex` por token (lista, até `;`) |
| `allow_methods` | `setAllowedMethod` por token; só `GET`, `POST` e `DELETE` — outro método **lança** |
| `return <código> <target>` | `setReturn` (`map<int, string>`); código 100–599 e **target obrigatório** |
| `autoindex on` | `setAutoIndex(true)`; `autoindex off` não faz nada (o default já é `false`); outro valor **lança** |
| `upload_store` | `setUploadStore` |
| `cgi_extension` / `cgi_path` | `setCgiExtension` / `setCgiPath` |

Directiva fora desta lista dentro de um `location` → `"Directive not allowed in
location"`. Duas pontas soltas: `setLocations()` continua declarado em
`ServerConfig.hpp` **sem definição nem uso** (resíduo — quem preenche é
`addLocation`), e `error_page` é **guardado** em `_errorPages` mas nenhum handler lê
`getErrorPages()` (só o `debugServers`); o `buildError` continua a usar o HTML fixo
do `Response`.

O parser de config é **silencioso**: a impressão do ficheiro em `ConfigFile`, as chamadas a
`debugServers()` / `debugLocations()` e a `Tokenize::debug()` estão todas comentadas — `Tokenize::debug()` existe mas ninguém a chama.

#### 12. Server + Client (rede, Pessoa A)

`main.cpp`: lê `argv[1]` e recusa `argc < 2` / `argc > 2` com mensagem em
`stderr`; depois `config.parse(argv[1])`, `signal(SIGINT, sig_handler)`,
`Server server(config)`, `server.init()` e `server.run()` — **arranca o servidor**.
Tudo o que corre mal (`socket`, `bind`, `listen`, `accept`, `poll`, `send`, config)
chega ao `catch` do `main` como `std::runtime_error`, que imprime e devolve −1.

Estrutura: `Listener` = `{ fd, port, vector<ServerConfig> configs }`, um por
**porta**. O `Server` guarda `_listeners`, `_server_fds`, `_clients`, `_pollfds`,
`_removeClients` e a `_config`.

| Server | O que faz |
|---|---|
| `init()` | Por cada `ServerConfig`: `FindListener(port)` reutiliza o fd se a porta já existe e só acrescenta o config à lista (é assim que dois `server {}` na mesma porta coexistem). Porta nova: `socket` → `setsockopt(SO_REUSEADDR)` → `fcntl(O_NONBLOCK)` → `bind` (`INADDR_ANY`) → `listen(SOMAXCONN)` → `Listener` + `pollfd` POLLIN |
| `run()` | `while (g_running)`: `enablePollOut()` (liga/desliga POLLOUT conforme `getResponseReady()`) → `poll(-1)` → itera sobre um **snapshot** de `_pollfds.size()` (o `acceptClient` faz `push_back` durante o ciclo) → POLLIN: fd de servidor ? `acceptClient` : `handleRead`; POLLOUT → `handleWrite` → `removeClients()`. Nota: `poll` devolver −1 **lança** também no SIGINT (`"Process finished by SIGINT"`), por isso Ctrl-C sai pelo `catch` do `main` com −1 |
| `acceptClient(fd)` | `accept` → `fcntl(O_NONBLOCK)` → encontra o `Listener` pelo fd (senão lança `"Listener not found"`) → `Client(fd, listener)` + `pollfd` POLLIN |
| `handleRead(client)` | `recv` 4KB → `appendRequest` → **um `Request` novo por `recv`** (entre chamadas só persiste `Client::_requestBuffer`) → `checkHeader` → CL+TE juntos → exceção → `readingBody` → `checkingBody_framing` / `checkingBody_chuncked` → `start_parsing` → dispatch → `response.toString()` + `appendResponse` + `setResponseReady(true)`. Exceção do parser → `Response::setError(400)` enviado logo, sem handler. `recv == 0` → `_removeClients`; `recv < 0` só imprime |
| dispatch (inline em `handleRead`) | Escolhe a `Locations` de **prefixo mais longo** com fronteira de `/`: `return` configurado → `RedirectHandler`; método fora de `allow_methods` → `StaticHandler` (para produzir o 405); extensão CGI → `CGIHandler`; `GET`/`HEAD` → `StaticHandler`; `POST` → `UploadHandler`; `DELETE` → `DeleteHandler`; resto → `StaticHandler`. O `else` "sem location" (StaticHandler com `Locations()` default) é código morto na prática: sem locations, `Request::MethodParsing` já lançou `Location not found` → 400 |
| `handleWrite(client)` | `send` a partir de `_responseOffset` (−1 lança `"Error on send()"`); ao chegar ao fim → `disablePollOut`; parcial continua no próximo POLLOUT |
| `removeClients()` / `getClientById(fd)` / `FindListener(port)` / `isServerFd(fd)` | Fecha e apaga os fds marcados (cliente + `pollfd`) / procura linear / reutilização de fd por porta / distingue fds de escuta dos de cliente |
| `shut_down()` | Chamado pelo **destrutor** do `Server` (o `main` não o chama): fecha clientes, todos os `pollfd` e os `_server_fds`. Os mesmos fds aparecem em listas sobrepostas, por isso há `close()` repetido — silencioso, o retorno não é verificado |

| Client | O que faz |
|---|---|
| campos | `_fd`, `_requestBuffer` (bruto, nunca aparado), `_responseBuffer` (a enviar), `_responseOffset`, `_responseReady`, `_listener`, `_config` |
| `appendRequest` / `appendResponse` | `append(buffer, total_bytes)`; ignoram `buffer == NULL` |
| getters/setters | fd, buffers, offset, ready, `listener`, `config` — `_config` é **ponteiro**: `getConfig()` antes de `setConfig()` desreferencia NULL |
| `requestComplete()` | Declarado em `Client.hpp` e **nunca definido** no `.cpp`, nem chamado por ninguém: resíduo da API antiga |

O que **não** existe: `mirror()` (o echo provisório foi removido) e keep-alive /
pipelining — `_requestBuffer` e `_responseBuffer` nunca são limpos, cada `recv`
volta a varrer o buffer desde o início e a resposta só fica pronta com o pedido
completo.

#### Estado de ligação entre camadas

1. **`Server::handleRead` → `Request`/handlers**: **feito**, com o dispatch inline
   em `handleRead`. O mesmo cálculo de prefixo mais longo existe também em
   `Request::MethodParsing`, chamado por `start_parsing` — aí só valida (e lança);
   quem escolhe o handler é o `Server`.
2. **Múltiplos servers/listen**: **feito** por porta, com um `Listener` partilhado
   por porta; a escolha do `ServerConfig` pelo `Host` acontece dentro do `Request`
   (`HostParsing`), não no `Server`.
3. **`SERVER_PORT` do CGI**: continua vazio porque `Request::version` nunca é
   preenchido (ver `REQUEST_RESPONSE.md` §1.1) — não é integração que falte.

---

<a id="cap-ref"></a>
## Referências de estudo

Material escolhido por se aplicar diretamente a código que existe neste
projeto — cada entrada diz onde é usada aqui.

**Especificações HTTP**

1. **RFC 9112 — HTTP/1.1: Message Syntax and Routing**
   <https://www.rfc-editor.org/rfc/rfc9112.html>
   *Usada em:* `Request::checkHeader` → `readingBody` → `checkingBody_*` →
   `start_parsing` — request line com três tokens, `Content-Length` vs
   `Transfer-Encoding` (os dois juntos → 400), chunked, CRLF obrigatório.
   Os 431/414/411 da norma **não** estão implementados. É a norma que substitui
   o RFC 7230 citado nos documentos antigos.

2. **RFC 9110 — HTTP Semantics**
   <https://www.rfc-editor.org/rfc/rfc9110.html>
   *Usada em:* todos os handlers — métodos e idempotência, 405 + header
   `Allow` (§15.5.6), 204 sem corpo no DELETE, 201 + `Location` no upload,
   `Location` obrigatório em 3xx, HEAD com headers mas sem corpo. Substitui o
   RFC 7231.

3. **RFC 3875 — The Common Gateway Interface (CGI) Version 1.1**
   <https://www.rfc-editor.org/rfc/rfc3875.html>
   *Usada em:* `CGIHandler::buildEnv()` (§4.1, meta-variáveis
   `GATEWAY_INTERFACE`, `SCRIPT_FILENAME`, `PATH_INFO`, `QUERY_STRING`) e
   `parseCgiOutput()` (§6.2, bloco de headers + `Status:` + `Content-Type`
   obrigatório).

4. **RFC 7578 — Returning Values from Forms: multipart/form-data**
   <https://www.rfc-editor.org/rfc/rfc7578.html>
   *Usada em:* `UploadHandler` — `Content-Disposition` com `name` e
   `filename`, `boundary` no `Content-Type`, partes sem ficheiro tratadas como
   campos de formulário.

5. **RFC 2046 — MIME Part Two: Media Types (multipart)**
   <https://www.rfc-editor.org/rfc/rfc2046.html>
   *Usada em:* `parseMultipart()` — a gramática do delimitador
   (`CRLF--boundary`, fecho `--boundary--`), a razão pela qual o `\r\n`
   anterior ao boundary pertence ao delimitador e não ao ficheiro.

6. **RFC 3986 — Uniform Resource Identifier (URI): Generic Syntax**
   <https://www.rfc-editor.org/rfc/rfc3986.html>
   *Usada em:* `Request::urlDecode()` e `DirectoryListingHandler::urlEncode()`
   — conjunto não reservado (§2.3) e percent-encoding por segmento de path.

**Rede e processos (a base do `Server` e do `CGIHandler`)**

7. **Beej's Guide to Network Programming**
   <https://beej.us/guide/bgnet/>
   *Usada em:* `Server::init()`/`acceptClient()`/`handleRead()`/`handleWrite()`
   — ciclo de vida do socket (`socket`, `bind`, `listen`, `accept`), `send`/
   `recv` com envio parcial, sockets não bloqueantes e `SIGPIPE`.

8. **`poll(2)` — Linux manual page**
   <https://man7.org/linux/man-pages/man2/poll.2.html>
   *Usada em:* `Server::run()` (`pollfd`, POLLIN/POLLOUT, `revents`) e no
   `CGIHandler` (`poll(POLLOUT)` antes de escrever, `poll(POLLIN)` com
   deadline). Inclui as notas sobre `EINTR` e fds negativos.

9. **Beej's Guide to Interprocess Communication**
   <https://beej.us/guide/bgipc/>
   *Usada em:* `CGIHandler::launch()` — `pipe()`, `fork()`, `dup2()` para
   ligar stdin/stdout do filho e `execve()`/`waitpid()` no reap; explica por
   que se fecham os fds originais depois do `dup2`.

**Comportamento de referência (o webserv imita o nginx)**

10. **nginx — Module `ngx_http_core_module`**
    <https://nginx.org/en/docs/http/ngx_http_core_module.html>
    *Usada em:* semântica das directivas que a config implementa ou vai
    implementar — `root`, `index`, `autoindex`, `client_max_body_size`,
    `error_page`, `try_files`, `location`, e a forma exacta do `return 301`
    que o `RedirectHandler::buildTarget()` reproduz.

> Nota de versionamento: os documentos citam **RFC 7230/7231**, entretanto
> obsoletados por **RFC 9112/9110**. As secções mudaram de número; para os
> códigos aqui usados (405/`Allow`, 204, 201, 3xx/`Location`) a regra é a
> mesma, mas nas referências novas procure os números acima.
