# Camada de Handlers de Requisição

Estrutura dos seis handlers de requisição
(`StaticHandler`, `UploadHandler`, `DeleteHandler`, `DirectoryListingHandler`,
`RedirectHandler`, `CGIHandler`). Os cabeçalhos e a estrutura das classes
chegaram aqui como scaffolding; **todas as implementações estão agora
completas e testadas** (ver tabela de estado no fim deste documento).

O projeto compila limpo com `make` após essas mudanças.

---

## Novos arquivos

### Tipos compartilhados (placeholders para a camada HTTP)

| Arquivo | Finalidade |
|---|---|
| `includes/Request.hpp` / `srcs/Request.cpp` | Requisição HTTP parseada: método, caminho, query, versão, mapa de cabeçalhos, corpo. Preenchido depois pelo parser de requisições. |
| `includes/Response.hpp` / `srcs/Response.cpp` | Resposta em construção: código de status, frase de razão (reason phrase), mapa de cabeçalhos, corpo. Serializada e transmitida depois por `Server` através do buffer de resposta do `Client`. |

### Hierarquia dos handlers

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
- `virtual handle(const Request&, Response&) = 0` — ponto de entrada principal
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

## Correções em arquivos existentes

- **`includes/ServerConfig.hpp`** — adicionados `_host`, `getHost()`, `setHost()`,
  aos quais `ServerConfig.cpp` já fazia referência (incompatibilidade pré-existente).
- **`includes/Client.hpp` / `srcs/Client.cpp`** — adicionado `getRequestBuffer()`
  (os handlers precisam de acesso à requisição bruta).
- **`Makefile`** — adicionados os 10 novos arquivos `.cpp` a `SRC_FILES`.

---

## Decisões de design

- Os handlers são **stateless** (sem estado), exceto `CGIHandler`, que rastreia
  o pid do seu processo filho. Eles são instanciados por servidor, não por
  requisição.
- Tudo é compatível com **C++98** e segue as convenções do projeto
  (nomenclatura `_member`, acessores `get_x()`, casts `(void)` em
  parâmetros não usados).

## Próximos passos conhecidos

~~`handle()` atualmente não recebe configuração~~ — **resolvido**: `handle()`
passa a receber `(const Request&, Response&, const ServerConfig&, const
Locations&)`; os helpers partilhados (`buildError`, `resolvePath`,
`isMethodAllowed`) já estão implementados. Ver `HANDLER_BASE.md` para a
explicação completa.

## Estado dos handlers concretos

| Handler | Estado | Documento |
|---|---|---|
| `RedirectHandler` | **implementado + testado** | `REDIRECT_HANDLER.md` |
| `StaticHandler` | **implementado + testado** | `STATIC_HANDLER.md` |
| `DirectoryListingHandler` | **implementado + testado** | `DIRECTORY_LISTING_HANDLER.md` |
| `UploadHandler` | **implementado + testado** | `UPLOAD_HANDLER.md` |
| `DeleteHandler` | **implementado + testado** | `DELETE_HANDLER.md` |
| `CGIHandler` | **implementado + testado** (síncrono; NEED_CGI p/ integração futura) | `CGI_HANDLER.md` |

Regressão: `test/handler_test.cpp` (compilar à parte, instruções no
cabeçalho do ficheiro). 80 testes, todos verdes.

Ligação ao `Server` (despacho, ordem de checks, smoke tests):
ver **`INTEGRATION_ROUTER.md`** — o checklist para quem faz o router.

`Locations` ganhou os acessores stub acordados (`getRedirect`,
`getRedirectCode`, `getRoot`, `getIndexes`, `getAutoindex`,
`getUploadStore`, `getCgiExtensions` + setters) — o parser de config
preenche-os; enquanto não o fizer, os handlers degradam com respostas
correctas (500 no redirect, herança do server no static).

Também na base (`Handler`): `notAllowed()` (405 + `Allow`), `isFile()`,
`isDirectory()` — partilhados por todos os handlers que mexem em ficheiros.
**Atenção à ordem**: headers pós-`buildError()`, que o `clear()` apaga
o que veio antes (apanhado pelos testes; ver `STATIC_HANDLER.md` §2).
