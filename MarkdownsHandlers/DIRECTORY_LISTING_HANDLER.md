# DirectoryListingHandler — autoindex seguro e barato

Documentação de estudo do `DirectoryListingHandler`
(`includes/DirectoryListingHandler.hpp` + `srcs/DirectoryListingHandler.cpp`).
Complementa o `STATIC_HANDLER.md`: este handler é o **fallback de
autoindex** — raramente chamado diretamente, quase sempre via
`StaticHandler::serveDirectoryListing()` quando `autoindex on` e nenhum
index correspondeu.

Estado: **implementado e testado** (8 testes próprios; suite completa de
80 em `test/handler_test.cpp`).

---

## 1. Dois pontos de entrada — porquê

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

## 2. listEntries() — readdir + ordenação manual

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

## 3. buildHtml() — a página e os seus hrefs

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

## 4. urlEncode() — o conjunto seguro mínimo

```cpp
static std::string urlEncode(const std::string &raw);
```

Codifica tudo o que **não** está no conjunto não-reservado do
RFC 3986 §2.3 (`ALPHA / DIGIT / - . _ ~`) como `%XX` hexuppercase.
Ser conservador aqui é grátis: um espaço vira `%20`, um `&` vira `%26`
(dentro de um atributo href isso importa), um `<` nunca parti o HTML do
link. Não se codifica o `urlPath` duas vezes — ele chega já
percent-descodificado do parser e é re-codificado uma única vez aqui.

## 5. handle() direto — defesa em profundidade

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

## 6. Interação com o StaticHandler (quem chama quê)

```
GET /sub/  (autoindex on, sem index)
  StaticHandler
    ├─ método OK, resolvePath OK, isDirectory OK, slash OK
    ├─ serveIndex() → false (não há index.html)
    └─ loc.getAutoindex() == true
         └─ DirectoryListingHandler::serve(res, "/www/sub", "/sub/")
              └─ 200 + text/html + página
```

O `DirectoryListingHandler` **não verifica métodos**: quem chega ao
autoindex já passou pelo check GET/HEAD do `StaticHandler`. Se um dia
for exposto diretamente no router, precisa do mesmo check — está
documentado aqui para não ser esquecido.

## 7. O que foi verificado

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
- 80 testes totais, `make` limpo com `-Wall -Wextra -Werror -std=c++98`.
