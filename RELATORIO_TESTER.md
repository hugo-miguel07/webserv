# Correções para o tester

**Os oito pontos foram corrigidos e passaram numa execução completa do `tester` (código 0).**

| Ponto | Correção aplicada | Ficheiros principais |
| --- | --- | --- |
| 1 | Limite de corpo por location, com rejeição antecipada de Content-Length e chunks acima do limite. | `ConfigParser`, `Locations`, `Request`, `Server` |
| 2 | Reconhecer HEAD e responder 405 com Allow: GET na raiz. | `Request.cpp` |
| 3 | Executar CGI nos POSTs pela extensão, incluindo URIs virtuais; manter GET estático e herdar raízes e limites. | `ServerConfig`, `Request`, `Server`, `CGIHandler` |
| 4 | `return 200 OK;` devolve HTTP 200 e corpo `OK`, sem redirecionar. | `RedirectHandler.cpp` |
| 5 | Diretórios sem índice e com autoindex desligado devolvem 404; os erros de acesso continuam a devolver 403. | `StaticHandler.cpp` |
| 6 | Corrigir PATH_INFO/SCRIPT_NAME, preencher SERVER_PORT e encaminhar cabeçalhos HTTP personalizados para o CGI. | `CGIHandler.cpp` |
| 7 | Alimentar stdin e ler stdout do CGI em simultâneo; aumentar o limite de saída para 128 MiB. | `CGIHandler.cpp` |
| 8 | Guardar o parser por ligação, receber buffers por referência, retomar a leitura dos chunks e libertar os buffers após processamento. Evitar cópias na remoção de clientes e retirar logs por ligação. | `Request`, `Client`, `Server` |

## Validação

- Compilação C++98 com `-Wall -Wextra -Werror`: concluída.
- Testes automáticos: 58 do parser, 81 dos handlers, 40 de integração, 32 de limites, 21 de encaminhamento CGI e 9 do ambiente/transferência CGI: **241 passaram**.
- As regressões incluem corpos binários, fragmentação byte a byte, limites cumulativos, cabeçalhos personalizados e um CGI que escreve antes de ler, com resposta superior a 10 MiB.
- `tester` fornecido, validação completa anterior: **código 0**. Incluiu POSTs de 100 MB para ficheiros e URIs virtuais, cabeçalho especial CGI, limites de `/post_body`, 100 000 GETs à raiz, 128 clientes em `/directory/nop` e 20 clientes a repetir POSTs de 100 MB.

A carga usa cópias do executável e dos mesmos ficheiros em `/tmp`, numa porta temporária. Apenas a porta do config é alterada para o teste. O acesso a ficheiros em `/mnt/c` pode tornar a execução mais lenta.

Para repetir esta execução: `python3 -B test/run_tester_linux.py`. O comando compila o projeto, prepara as cópias em `/tmp`, usa uma porta livre e termina os processos temporários no fim ou com Ctrl+C. Mostra o tempo decorrido a cada 30 segundos.

Na comparação com 20 clientes, os mesmos 1000 GETs demoraram 5,02 s com os ficheiros em `/mnt/c` e 0,96 s em `/tmp`. O servidor ativo em `/mnt/c` foi observado em espera por disco (`p9_client_rpc`). Os 20 × 5000 pedidos desta fase podem, por isso, levar vários minutos.

Após esta validação foi corrigido um reset intermitente nas respostas 413: o servidor envia a resposta e descarta durante um intervalo limitado os bytes ainda em trânsito antes de fechar a ligação. A regressão foi reproduzida antes da correção e passou depois, para corpos fixos e chunked. Também foi confirmada a libertação de sockets quando o cliente fica inativo.

Validação desta alteração: 32 testes de limites, 33 de integração, 21 de encaminhamento CGI e o `tester` real até ultrapassar os 100 000 GETs. Esta última execução foi interrompida de propósito na fase seguinte para validar a limpeza com Ctrl+C; a carga final de POSTs de 100 MB não foi repetida nesta execução.

A leitura estática foi otimizada para a execução direta em `/mnt/c`: ficheiros até 256 KiB ficam numa cache de no máximo 32 entradas. Cada GET valida os metadados do ficheiro; alterações, substituições e remoções invalidam a entrada. Para um diretório com um único índice já em cache, basta uma verificação de metadados, sem reabrir e reler o índice.

Após esta otimização passaram 81 testes dos handlers, 40 de integração e 21 de encaminhamento CGI. As regressões incluem alteração com o mesmo tamanho e mtime, substituição atómica, remoção/recriação e prioridade entre índices. Os 1000 GETs em `/mnt/c` passaram em 1,83 s, contra 5,02 s na medição anterior. **O `tester` real ultrapassou a fase dos 100 000 GETs em 104,89 segundos, com os ficheiros em `/mnt/c`.** A execução foi interrompida na fase seguinte, sem repetir a carga final de POSTs de 100 MB.
