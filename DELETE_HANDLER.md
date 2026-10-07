# DeleteHandler — o destrutivo, e por isso o mais conservador

Documentação de estudo do `DeleteHandler` (`includes/DeleteHandler.hpp` +
`srcs/DeleteHandler.cpp`). Assume `HANDLER_BASE.md` (helpers partilhados) e
`STATIC_HANDLER.md`/`UPLOAD_HANDLER.md` (padrões de 405 e de ordem de checks).

Estado: **implementado e testado** — 14 testes próprios em
`test/handler_test.cpp` (80 no total no suite).

---

## 1. Contrato — códigos e quem os provoca

| Código | Quando | É erro de quem? |
|---|---|---|
| **204 No Content** | ficheiro OU diretório vazio removido | — |
| **403** | traversal bloqueada; sem permissão (EACCES/EPERM/EROFS); DELETE não ativado na location (via 405 não — ver §2) | cliente / config |
| **404** | alvo não existe (ou desapareceu entre `stat()` e `remove()`) | cliente |
| **405** + `Allow` | DELETE não está na lista configurada; ou método não-DELETE | cliente |
| **409 Conflict** | diretório não vazio (ENOTEMPTY) | estado do recurso |
| **500** | errno desconhecido | servidor |

## 2. A decisão central — lista vazia NÃO permite (fail closed)

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

## 3. Fluxo e ordem dos checks

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

## 4. errno → HTTP: a resposta mais honesta possível

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

## 5. Diretórios: vazio só, nunca recursivo

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

## 6. 204, não 200

```
HTTP/1.1 204 No Content\r\n
\r\n
```

RFC 7231 §6.3.5: a resposta a um DELETE bem-sucedido "has no payload"
salvo relatório pedido. 200 com corpo ("File deleted") também é legal,
mas 204 é o idiomático — e o que o módulo dav do nginx responde. O
`res.clear()` antes do `setStatus()` garante que nenhum header prévio
escapa (mesma disciplina do `buildError`, aqui à mão porque não é erro).

## 7. removeFile() — `std::remove`, não `unlink`?

`std::remove(path)` (cstdio) chama `unlink(2)` para ficheiros e é
portátil C++98. Para ficheiros regulares é idêntico ao unlink. O
`isFile()` anterior já garantiu que era regular — symlinks resolvidos
pelo `stat()` do `isFile` (segue links): apagar `link.txt` apaga o
ALVO do link, não o link. Decisão consciente: para o efeito do webserv
(o cliente vê o caminho e quer o conteúdo desaparecido) é o
comportamento esperado; documentado aqui para ninguém estranhar.

## 8. O que foi verificado

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
