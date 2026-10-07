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
| `resolvePath(root, requestPath, outPath)` → `bool` | Junta root + path **segmento a segmento**: colapsa `.`, resolve `..` (se escapar à root → `false`), rejeita NUL embutido. Root normalizada sem `/` final. O path já chega percent-decoded do parser |
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
| `handle(req, res, srv, loc)` | `loc.getRedirect()` vazio ou `getRedirectCode()` == 0 → **500** (config mal feita, não é `ERROR` — a ligação está bem). Código fora de [300, 399] → 500. Senão: status 3xx + `Location` + página HTML mínima (sem corpo se HEAD) |
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
6. `getCgiInterpreter()` vazio → 500 (interpretador vem da config do **servidor**, nunca do cliente)
7. `launch()` falho → 500 + **`ERROR`** (o servidor é que está partido, ex. fds esgotados)
8. feed stdin (tolerante a EPIPE) → fechar write end (EOF para o filho) → read stdout com deadline + size cap
9. reap com `waitpid(WNOHANG)` em loop de 20 ms; se passar o deadline → `SIGKILL` + `waitpid`
10. outcomes: `timedOut` → **504**; `!fed` / `tooBig` / `exitCode != 0` / output vazio → **502**; parse falho → **502**; ok → 200 (ou o `Status:` do CGI)

| Função/membro | O que faz |
|---|---|
| `isScript(filePath, extensions)` → `bool` | Extensão do ficheiro (lowercased, com ponto) ∈ `cgi_extensions` (tolera config com ou sem `.`). Lista vazia → `false` |
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

Estado interno: `_valid` (último parse ok?) e `_errorStatus` (default 400).

Limites: secção de headers **8192 bytes** (`MAX_HEADER_BYTES`); target **2048 bytes** (`MAX_URI_BYTES`).

| Função | O que faz |
|---|---|
| `parse(raw)` → `bool` | Fluxo: `findHeaderEnd` (tolera `\r\n\r\n` e `\n\n`) → headers >8k → **431** → request line têm de ter **exatamente 3 tokens** (`method target version`) → método ∈ {GET, HEAD, POST, PUT, DELETE, OPTIONS}, senão **501** → versão `HTTP/1.0`/`HTTP/1.1`; `HTTP/algo` → **505** → target ≤ 2048 → **414** → absolute-form `http(s)://` é tolerado (host extraído para o header `host`, path reescrito) mas depois só serve origin-form `/<path>` (asterisk/authority → 400) → split path/query no primeiro `?` → `urlDecode(path)` (escape malformado → 400) → headers: nome **lowercased**, valor casing original, repetidos combinados com `", "`; linha que começa por space/tab (folding obsoleto) → 400 → framing: Content-Length **e** Transfer-Encoding juntos → 400 (anti request smuggling); chunked → `walkChunked`; body decodificado para `body`; sem framing em POST/PUT → **411**; Content-Length inválido/não-dígitos → 400 |
| `isComplete(buf)` *(static)* | Diz ao Server se o buffer já contém um request completo (headers + body). Headers incompletos → devolve `true` quando o buffer passa 8k (o server responde 431 em vez de bufferizar para sempre). Chunked → `walkChunked == OK`. Content-Length → `buf.size() - bodyStart >= length`; CL > 12 dígitos (~1TB) nunca está completo |
| `peekContentLength(buf)` *(static)* | Content-Length declarado, a partir de headers possivelmente incompletos: 0 se nada recebido/valor inválido; > 12 dígitos → sentinel `size_t(-1)`. Serve para o Server aplicar `client_max_body_size` **antes** de bufferizar o corpo |
| `getHeader(name)` | Lookup case-insensitive (map já está lowercase, devolve `""` se ausente) |
| `hasBody()` | body não vazio |
| `getContentLength()` | Content-Length do header; 0 se ausente/inválido/> 12 dígitos |
| `urlDecode(s, plusAsSpace)` *(static)* | `%XX` → byte; escape incompleto/inválido → `""`; `+` → espaço só quando `plusAsSpace` (convenção da query) |
| `isValid()` / `getErrorStatus()` / `clear()` | Estado do último parse / reset de todos os campos |

Helpers internos (namespace anónimo no `.cpp`): `toLower`, `trim`, `isToken`/`isTokenChar` (RFC token), `isDigits`, `isKnownMethod`, `splitLines` (partir em `\n` + tirar um `\r` final por linha), `splitTokens`, `splitHeader` (`Name: value`, nome tem de ser token), `findHeaderEnd`, `getFramingHeaders`.

`walkChunked(s, pos, bodyOut, endPos)` — enum `CHUNK_OK` / `CHUNK_INCOMPLETE` / `CHUNK_BAD`: linha de tamanho hex (≤ 8 dígitos, extensões de chunk após `;` ignoradas), `0\r\n` termina (trailer opcional até linha vazia), cada chunk seguido de CRLF obrigatório. Incompleto = precisamos de mais bytes.

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
| `getAllowedMethods` / `addAllowedMethod` | um por token de `allow_methods` |
| `getRedirect` / `getRedirectCode` / `setRedirect(target, code)` | uma chamada por `return <code> <target>` (nginx) |
| `getRoot`/`setRoot` · `getIndexes`/`addIndex` · `getAutoindex`/`setAutoindex` | static + listing |
| `getUploadStore` / `setUploadStore` | upload |
| `getCgiExtensions`/`addCgiExtension` · `getCgiInterpreter`/`setCgiInterpreter` | CGI |

Convenção: vazio/0/false = herdar do server — **excepto DELETE** (fail closed, §4).

## 11. Cascata de Config (Pessoa A)

`Config.parse(filename)` → `ConfigFile.read_and_append` → `ConfigParser.tokenize` → `parseServers` → `vector<ServerConfig>`.

| Classe | Função | O que faz |
|---|---|---|
| `Config` | `parse(filename)` | Orquestra as três classes; guarda `_servers` (devolve sempre `true`!) |
| `ConfigFile` | `openFile` / `read_and_append` | `open(O_RDONLY)` + loop `read` de 1KB; tudo vai para `_inputText`; `get_str` devolve a ref |
| `ConfigParser` | `tokenize(input)` | `{`, `}` e `;` viram tokens isolados; whitespace é delimitador |
| `ConfigParser` | `parseServers(tokens)` | Percorre tokens; a cada `server` consome o bloco com `parseServerInfo` |
| `ConfigParser` | `parseServerInfo(tokens, i)` | `bracket_count` trata `{}` aninhados; directives: `listen` (1–65535, senão msg de erro **e continua**), `client_max_body_size` (via `atoi` — **sem suporte a sufixos k/m**), `server_name` e `index` (listas até `;`), `root` |
| `ServerConfig` | getters/setters | port, bodySize, root, host, index (vector: aceita múltiplos), serverName |

**Ainda por fazer no parser**: locations não são preenchidas (`setLocations()` é um placeholder sem corpo em `ServerConfig`) — redirect, autoindex, `upload_store`, `cgi_*` e `error_pages` não têm directiva. Os handlers degradam corretamente (alterna herança do server, redirect/upload → 500).

⚠️ **Debug output ativo**: `ConfigFile` imprime o ficheiro todo e `parseServerInfo` imprime port/names/root/indexes — era código comentado, a limpeza transformou-o em comportamento. Silenciar antes da eval.

## 12. Server + Client (rede, Pessoa A)

`main.cpp` (estado atual): lê `argv[1]`, chama `config.parse` e **não arranca servidor nenhum** — o bloco `sig_handler + init + run` estava comentado e foi removido pela limpeza. Ligação: `INTEGRATION_ROUTER.md`.

| Server | O que faz |
|---|---|
| `init()` | `socket` → `fcntl(O_NONBLOCK)` → `bind` (INADDR_ANY, port via `htons`) → `listen(SOMAXCONN)` → primeiro `pollfd` (server fd, POLLIN) |
| `run()` | Enquanto `g_running`: `enablePollOut()` (liga POLLOUT a quem tem resposta pronta) → `poll(-1)` bloqueante → por cada revent: POLLIN (fd do server ? `acceptClient` : `handleRead`), POLLOUT → `handleWrite` → `removeClients` → `shut_down` no fim |
| `acceptClient()` | `accept` → nonblock → push `Client` + `pollfd` POLLIN |
| `handleRead(client)` | `recv` 4KB → `>0`: append + **`mirror()` + `setResponseReady(true)`** (echo-server provisório!) — é aqui que entra o router: `Request::isComplete` → `parse` → server por Host/porta → location → handler; `==0`: marcar para remoção |
| `handleWrite(client)` | `send` a partir de `_responseOffset`; parcial continua no próximo POLLOUT; completo → `disablePollOut` |
| `removeClients()` / `getClientById(fd)` / `shut_down()` | Elimina clientes mortos (fecha fd, limpa pollfds) / procura linear por fd / fecha tudo (clients, pollfds, server fd) |

| Client | O que faz |
|---|---|
| buffers + `_responseOffset`/`_responseReady` | requestBuffer (bruto), responseBuffer (a enviar) |
| `appendRequest` / `appendResponse` | `append(buffer, bytes)` |
| getters/setters | fd, buffers, offset, ready |
| `mirror()` | `_responseBuffer = _requestBuffer` — marcado para apagar quando o router entrar |

### Estado de ligação entre camadas (a integrar)

1. **`Server::handleRead` → Request/Response/handlers**: hoje só echo. Substituir `mirror()` por: `isComplete(reqBuf)` → `parse` → seleção de server/location → `handle(...)` → `appendResponse(res.toString())` + `setResponseReady`.
2. **Múltiplos servers/listen**: `Server` tem um único fd; `Config` já devolve uma lista — um `Server` por `ServerConfig` ou um dispatcher único.
3. **`SERVER_PORT` do CGI** está vazio à espera desta integração.
