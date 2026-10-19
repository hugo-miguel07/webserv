# UploadHandler — POST de ficheiros sem abrir buracos

Documentação de estudo do `UploadHandler` (`includes/UploadHandler.hpp` +
`srcs/UploadHandler.cpp`). Assume `HANDLER_BASE.md` (helpers partilhados) e
`STATIC_HANDLER.md` (o padrão dos dois 405, lá explicado — aqui reutilizado).

Estado: **implementado e testado** — 15 testes próprios em
`test/handler_test.cpp` (80 no total no suite).

---

## 1. Contrato — os códigos de resposta e quem os provoca

| Código | Quando | É erro de quem? |
|---|---|---|
| **201 Created** + `Location` | ficheiro(s) gravado(s) | — |
| **400** | multipart malformado, sem part com `filename=`, nome hostil (`..`, NUL, vazio, >200 bytes) | cliente |
| **405** + `Allow: POST` | método não-POST (policy da location *ou* capacidade do handler) | cliente |
| **413** | corpo/Content-Length acima de `client_max_body_size`, **ou limite não configurado** | cliente / config |
| **500** | location sem `upload_store`; erro de disco a gravar | servidor |

## 2. Ordem dos checks — cada um mais caro que o anterior

```
handle()
  ├─ 1. método (policy + capacidade)   → 405        [barato, sem I/O]
  ├─ 2. tamanho do corpo               → 413        [antes de PARSE e de DISCO]
  ├─ 3. upload_store configurado?      → 500
  ├─ 4. multipart? parse               → 400
  ├─ 5. sanitize do nome               → 400
  └─ 6. saveFile()                     → 201 | 500
```

O princípio: **rejeitar o mais cedo possível**. Um corpo de 10 GB num
pedido sem `Content-Length` honesto nunca chega a ser parseado nem a
tocar no disco — o 413 do passo 2 corta antes. É a mesma lógica do
`resolvePath` antes do `stat()` no `StaticHandler`: validação barata
primeiro, I/O só no fim.

### 413 com limite não configurado — fail closed

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

Repara também: o check conta o `Content-Length` **declarado** (o Server
pode rejeitar antes de receber o corpo todo — `Request::peekContentLength`
existe para isso) *e* o tamanho **real** do corpo (a verdade quando o
cliente mente no header). Os dois têm de caber no limite.

## 3. Dois modos de upload

### Modo raw (sem boundary no Content-Type)

O último segmento do URL nomeia o ficheiro:

```
POST /upload/notas.txt        body = "conteúdo"
→ grava <upload_store>/notas.txt, 201 + Location: <store>/notas.txt
```

`extractFilename()` faz `rfind('/')` no `req.path` (já
percent-descodificado; a query string não faz parte do path — ver
`REQUEST_RESPONSE.md` §1). Nome vazio (`POST /upload/`) → 400.

### Modo multipart (`boundary=` presente no Content-Type)

Um ficheiro gravado por cada part com `filename=`; form fields simples
são ignorados. Vantagem: um único POST pode trazer vários ficheiros, e
o nome vem do próprio corpo (como os browsers mandam) em vez do URL.

## 4. Anatomia do multipart — o que o parser espera

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

## 5. Boundary — onde vive e as suas duas formas

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

## 6. O bug que os testes apanharam — case sensitivity dos headers de part

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

## 7. Política de filenames — reject, don't rewrite

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

## 8. saveFile() e a resposta 201

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

## 9. Limitações conhecidas (documentadas, não escondidas)

- **Corpo inteiro em memória**: o `Request` já bufferiza tudo; o handler
  grava de uma vez. Streaming para ficheiro durante a receção seria
  mudança na camada `Server`/`Request`, não aqui.
- **Sem `filename=""` explícito**: `filename=""` devolve `""` e a part é
  tratada como field → skip. Alinhado com o RFC (filename vazio = não é
  upload).
- **Sem quota por store**: gravações repetidas com o mesmo nome
  substituem. Versionar/unicidade ficaria para o router/config decidir.

## 10. O que foi verificado

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
