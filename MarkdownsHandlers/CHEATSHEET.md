# CHEATSHEET — webserv

**Parte 1** — camada de handlers (Pessoa C) · **Parte 2** — parsing HTTP, config e rede (Pessoas A e B)

Referência rápida de **todas as funções implementadas** nos handlers do webserv (C++98).
Ficheiros: `includes/*Handler*.hpp` + `includes/Handler.hpp` e os respetivos `.cpp` em `srcs/`.

Parte 2 cobre: `Request`, `Response`, `Config`, `ConfigFile`, `ConfigParser`, `ServerConfig`, `Locations`, `Client`, `Server`, `main` (em `includes/` + `srcs/`).

---

## Mapa geral

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

## 1. Handler (classe base)

### Tipos

| Enum `e_status` | Significado |
|---|---|
| `DONE` | Resposta construída, pronta a enviar |
| `NEED_CGI` | Manter ligação aberta, fazer polling do processo CGI |
| `ERROR` | Falha interna (ex.: `fork`/`pipe`), fechar a ligação |

### Funções

| Função | O que faz |
|---|---|
| `buildError(Response&, int)` | `res.clear()` **primeiro** (nunca deixar headers de meio-de-caminho escapar para a página de erro) + `res.setError(code)` |
| `notAllowed(Response&, const Locations&)` | 405 + header `Allow` com os métodos da location (RFC 7231 §6.5.5). Ordem: `buildError` **depois** set `Allow` |
| `resolvePath(root, requestPath, outPath)` → `bool` | Junta root + path **segmento a segmento**: colapsa `.`, resolve `..` (se escapar à root → `false`), rejeita NUL embutido. Root normalizada sem `/` final. O path **não** é percent-decoded: `%2e%2e` é o nome literal de um ficheiro, não `..` (ver `REQUEST_RESPONSE.md` §1.7) |
| `isMethodAllowed(req, loc)` → `bool` | Lista vazia de `allow_methods` = **tudo permitido** (estilo nginx). Senão, compara método a método |
| `isFile(path)` → `bool` | `stat()` + `S_ISREG` (segue symlinks). `false` se não existir |
| `isDirectory(path)` → `bool` | `stat()` + `S_ISDIR`. `false` se não existir |

### Regras de ouro da base

1. **`buildError()` faz `clear()`** → qualquer header (ex.: `Allow`) tem de ser posto **depois** dela.
2. **405 tem duas fontes** em todos os handlers: política da location (`notAllowed()`, Allow = lista configurada) e capacidade do handler (Allow fixo, ex.: `GET, HEAD`).
3. **Path traversal morre em `resolvePath`** antes de qualquer syscall.
4. `handle()` é `= 0` → cada handler concreto implementa.

---

## 2. StaticHandler

**Fluxo do `handle()`**: método (405 ×2) → root → `resolvePath` (403) → diretório? (301 sem slash / index / autoindex / 403) → ficheiro → 200.

| Função | O que faz |
|---|---|
| `handle(req, res, srv, loc)` | Orquestra: 405 se método ∉ {GET, HEAD} (Allow: `GET, HEAD`); resolve root (location > server); 301 para diretório sem `/` final (query string sobrevive); tenta indexes (location > server); `autoindex on` → listing; senão 403. Ficheiro inexistente → 404 |
| `serveFile(filePath, req, res)` | Lê ficheiro binário via `ifstream` + `ostringstream`. Erro de open → 403 se ainda existe (permissões), 404 se desapareceu; leitura corrompida → 500. 200 + `Content-Type` pelo extension. **HEAD**: mesmos status + headers + `Content-Length`, corpo vazio (RFC 7231 §4.3.2) |
| `serveIndex(dirPath, indexes, req, res)` → `bool` | Testa cada index candidato (`dir/index[i]`), serve o primeiro que for ficheiro. `false` se nenhum existir (caller cai para autoindex/403) |
| `serveDirectoryListing(req, res, dirPath)` | Delega num `DirectoryListingHandler` temporário via `serve()` — o `dirPath` já vem resolvido |
| `guessContentType(filePath)` → `const char*` | Extensão (lowercased) → MIME. Tabela: `.html/.htm→text/html`, `.css`, `.js`, `.json`, `.png`, `.jpg/.jpeg`, `.gif`, `.svg`, `.ico`, `.txt`, `.pdf`. Desconhecida → `application/octet-stream` (browser faz download em vez de adivinhar) |

---

## 3. UploadHandler

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

## 4. DeleteHandler

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

## 5. DirectoryListingHandler

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

## 6. RedirectHandler

**Filosofia**: a única decisão por-location é `code` + `target` (tipo nginx `return 301 https://exemplo.com`). Chegar a este handler é responsabilidade do router.

| Função | O que faz |
|---|---|
| `handle(req, res, srv, loc)` | Usa a **primeira** entrada de `loc.getReturn()` (o menor código); mapa vazio, target vazio ou código 0 → **500** (config mal feita, não `ERROR` — a ligação está bem). Código fora de [300, 399] → 500. Senão: `res.clear()`, status 3xx + `Location` + página HTML mínima (sem corpo se HEAD) |
| `buildTarget(req, redirectConfig)` | Semântica `return` do nginx: config com `://` → usada verbatim (redirect absoluto); senão → `"http://" + header Host + target` (mesmo host). O target **nunca** é concatenado com o path do pedido. Sem Host (HTTP/1.0) → target nu (Location relativa válida) |

---

## 7. CGIHandler

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

### Helpers `static` no `.cpp` (namespace anónimo)

| Função | O que faz |
|---|---|
| `nowMs()` | `gettimeofday` → ms |
| `closeEnd(fd&)` / `closePipePair(p)` | Fecha e põe a -1 |
| `toLowerCopy` / `trimCopy` | Cópia lowercase / trim de `" \t"` |
| `writeAllNonblocking(fd, data, deadline)` → `bool` | Escreve tudo com `poll(POLLOUT)` antes de cada write; tolera `EAGAIN`/`EINTR`; **EPIPE = decisão do filho, não crash** |
| `readAllWithDeadline(fd, deadline, timedOut&, tooBig&)` | Lê até EOF com `poll(POLLIN)`; limitado por tempo **e** tamanho (10 MB — ler pipe sem limite = vetor de exaustão de memória) |
| `parseCgiOutput(raw, res)` → `bool` | RFC 3875 §6.2: separa headers/corpo no `\r\n\r\n` (tolera `\n\n` de scripts caseiros). `Status:` → `setStatus` (100–599, senão falha); `Content-Type` → **obrigatório**; `Location:` e restantes headers → passthrough (casing original). Sem bloco de headers ou sem Content-Type → `false` → 502 |

---

## Tabela-resposta dos handlers

| Handler | Sucesso | Erros que emite |
|---|---|---|
| Static | 200 (ou 301 dir sem slash) | 403 traversal/autoindex off/sem permissão, 404, 405, 500 |
| Upload | 201 + `Location` | 400 multipart/nome, 405, 413 (fail closed!), 500 config/disco |
| Delete | 204 sem corpo | 403 traversal/permissão, 404, 405 (+Allow), 409 não vazio, 500 |
| Listing | 200 `text/html` | 404 (não é diretório / resolve falhou) |
| Redirect | 3xx + `Location` | 500 config (sem target, código fora de 3xx) |
| CGI | 200 (ou `Status:` do script) | 400 extensão, 404 script, 405, 500 config/fork+pipe, 502 exit≠0/malformado, 504 timeout, `ERROR` e_status |

## Armadilhas que os testes apanharam

1. **`Allow` depois de `buildError`** — o `clear()` apaga headers postos antes (apanhado no Static).
2. **`\r\n` do multipart pertence ao delimitador** — ficheiro terminado em newline não ganha um extra.
3. **Script como argumento do interpretador** no `execve` — sem isso, exit 0 silencioso e 502 em tudo.
4. **Fail closed** em toda a lado: sem limite de body → 413; DELETE não listado → 405; sem extensões CGI → não executa.
5. **Não reescrever nomes hostis** — `../../evil.txt` → 400, nunca `evil.txt` silenciosamente.

---

# PARTE 2 — Parsing HTTP, Config e Rede

## 8. Request

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

## 9. Response

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

## 10. Locations

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

## 11. Cascata de Config (Pessoa A)

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

## 12. Server + Client (rede, Pessoa A)

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

### Estado de ligação entre camadas

1. **`Server::handleRead` → `Request`/handlers**: **feito**, com o dispatch inline
   em `handleRead`. O mesmo cálculo de prefixo mais longo existe também em
   `Request::MethodParsing`, chamado por `start_parsing` — aí só valida (e lança);
   quem escolhe o handler é o `Server`.
2. **Múltiplos servers/listen**: **feito** por porta, com um `Listener` partilhado
   por porta; a escolha do `ServerConfig` pelo `Host` acontece dentro do `Request`
   (`HostParsing`), não no `Server`.
3. **`SERVER_PORT` do CGI**: continua vazio porque `Request::version` nunca é
   preenchido (ver `REQUEST_RESPONSE.md` §1.1) — não é integração que falte.
