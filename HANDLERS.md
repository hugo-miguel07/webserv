# Camada de Handlers de Requisição

Estrutura base (scaffolding) para os seis handlers de requisição
(`StaticHandler`, `UploadHandler`, `DeleteHandler`, `DirectoryListingHandler`,
`RedirectHandler`, `CGIHandler`). Os cabeçalhos, a estrutura das classes e as
declarações de funções já estão no lugar — as implementações são stubs a serem
preenchidos um por um.

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
| `StaticHandler` | `includes/StaticHandler.hpp` | `handle`, `isFile`, `isDirectory`, `serveFile`, `serveIndex` (resolução de index), `serveDirectoryListing` |
| `UploadHandler` | `includes/UploadHandler.hpp` | `handle`, `isBodyTooLarge` (vs `client_max_body_size`), `extractFilename`, `saveFile` (em `upload_store`) |
| `DeleteHandler` | `includes/DeleteHandler.hpp` | `handle`, `removeFile`, `removeDirectory` |
| `DirectoryListingHandler` | `includes/DirectoryListingHandler.hpp` | `handle`, `listEntries` (ordenado), `buildHtml` |
| `RedirectHandler` | `includes/RedirectHandler.hpp` | `handle`, `buildTarget` (a partir da config de redirect da location) |
| `CGIHandler` | `includes/CGIHandler.hpp` | `handle`, `isScript` (vs `cgi_extension`), `buildEnv` / `freeEnv` (variáveis de ambiente do CGI), `launch` (fork/dup2/execve), membro `_cgiPid` |

Cada handler tem um `.cpp` stub correspondente em `srcs/` que compila limpo —
os corpos apenas fazem `(void)` nos parâmetros e retornam valores padrão.

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
  (nomenclatura `_member`, acessores `get_x()`, casts `(void)` nos stubs).

## Próximos passos conhecidos

`handle()` atualmente não recebe configuração. Ao conectar o roteamento no
`Server`, passe o `ServerConfig` + `Locations` correspondentes para `handle()`
(ou defina-os no handler antes de chamá-lo).
