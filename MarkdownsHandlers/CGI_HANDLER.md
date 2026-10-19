# CGIHandler — fork, pipes, e a guerra contra os zombies

Documentação de estudo do `CGIHandler` (`includes/CGIHandler.hpp` +
`srcs/CGIHandler.cpp`). Assume `HANDLER_BASE.md` (`e_status`, helpers) e
`REQUEST_RESPONSE.md` (`Request` corpo já descodificado).

Estado: **implementado e testado** — 15 testes próprios em
`test/handler_test.cpp` (80 no total no suite), com scripts Python reais.

---

## 1. O modelo de execução — síncrono hoje, NEED_CGI amanhã

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

## 2. O bug mais subtil do projeto — argv sem o script

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

## 3. launch() — a dança dos pipes e do fork

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

## 4. SIGPIPE — o assassino silencioso do servidor

O primeiro `write()` num pipe cujo leitor morreu levanta **SIGPIPE**,
que por default **termina o processo**. Sem `signal(SIGPIPE, SIG_IGN)`,
um CGI que sai cedo matava o servidor inteiro no meio do feed — não um
500, não um log: morte instantânea. Ignorado o sinal, o `write()`
devolve `-1`/`EPIPE` e o handler responde 502, como deve ser.

`writeAllNonblocking()` trata o EPIPE como "o filho decidiu sair":
`false` → 502 a montante. Nunca crash, nunca retry infinito.

## 5. UM deadline, não vários

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

## 6. O ambiente do filho (RFC 3875 §4.1)

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

**O interpretador NUNCA vem do cliente**: vem de
`Locations::setCgiInterpreter()` (config do servidor). Nem o path do
script vem cru: vem do `resolvePath()` contra a root.

## 7. Parse do output CGI (RFC 3875 §6.2)

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

## 8. Taxonomia de erros — 502 vs 504 vs ERROR

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

## 9. O reap — sem zombies, sem esperas infinitas

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

## 10. Segurança — postura completa

1. **Traversal**: `resolvePath()` antes de qualquer syscall (403);
2. **Extensões**: só as configuradas executam (400);
3. **Interpretador**: da config, nunca do cliente (500 se em falta);
4. **Script inexistente**: 404 antes do fork;
5. **Output limitado** em tempo e espaço;
6. **SIGPIPE ignorado**: cliente/CGI hostil não derruba o processo;
7. **Sem shell**: `execve` direto do interpretador — nada passa por
   `system()`/`sh -c`, logo injeção de comandos não tem superfície.

## 11. O que foi verificado

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
