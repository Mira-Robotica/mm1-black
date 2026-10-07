# Incremento 4: calibração manual da IMU por Wi-Fi

Atualização de 07/10/2026: o incremento 5 substitui o transporte do laboratório por SparkFun 1.0.6/I²C1 e controla NRST no GPIO32. O protocolo de calibração permanece. Com esta versão, o usuário relatou testes promissores: a IMU respondeu bem, foi possível calibrar o magnetômetro e o índice de qualidade do Rotation Vector aumentou para **3/3**. Nem tudo funcionou 100%; as falhas remanescentes ainda precisam ser caracterizadas. Ver [LAB_IMU_SPARKFUN_P4.md](LAB_IMU_SPARKFUN_P4.md).

Implementado em 01/10/2026 no alvo `mm1_p4_lab`. Build P4, testes nativos e cliente TCP verificados em software. **Há sucesso parcial de calibração na placa, informado pelo usuário em 07/10/2026. O aceite completo do procedimento, a estabilidade sob carga de relatórios e a persistência após desligar/ligar permanecem pendentes.** O relato não confirma SAVE/restauração nem persistência DCD, e qualidade 3/3 não substitui uma medição de precisão angular. A ligação do lab continua SDA31/SCL30.

O guia segue as seis etapas do [BNO08X Sensor Calibration Procedure, revisão 1.6](datasheets/BNO08X-Sesnor-Calibration-Procedure.pdf), seções 2.1, 2.2 e 3. A calibração ocorre no BNO, com salvamento explícito do DCD na flash dele. O lab não usa NVS de heading, offsets, declinação, tare ou trim do laser. O laser fica desligado durante a sessão; o CSV de captura continua usando o Rotation Vector magnético, na mesma ordem w,x,y,z e com as mesmas colunas.

## Compilar e abrir o monitor

Compile e grave `mm1_p4_lab` pelo procedimento já usado na bancada. Depois, a calibração inteira funciona por Wi-Fi, sem USB conectado à trena:

```sh
pio run -e mm1_p4_lab
docker compose -f docker/compose.yaml build calibrate-imu
docker compose -f docker/compose.yaml run --rm calibrate-imu --host 192.168.0.10
```

Use o IP atual da placa. `--env-file docker/.env` pode fornecer `LAB_HOST`, `LAB_PORT`, `LAB_TIMEOUT`, `LAB_UID` e `LAB_GID`, como no cliente STATUS. O serviço herda rede host, usuário e volume `docker/data:/data`, com `stdin_open` e `tty` habilitados. Feche outros clientes TCP: só uma conexão é atendida por vez.

Abrir o monitor apenas consulta `CAL_IMU STATUS`; **não inicia nem salva calibração**. Digite `iniciar` para seis posições, ou `iniciar 4` / `iniciar 5` / `iniciar 6`. Também é possível escolher o padrão com `--positions 4`. A consulta a 5 Hz continua enquanto o terminal espera sua entrada. `--plain` produz texto comum sem atualização ANSI.

O registro JSONL é obrigatório, com nome único em `/data/cal-imu-<id>.jsonl`, preservado em `docker/data/` no PC. `--log /data/ensaio.jsonl` ou `LAB_CAL_LOG` escolhe outro arquivo. Cada comando e resposta, instrução apresentada, qualidade, configuração e resultado recebe horário UTC do PC. Falha ao abrir o registro impede o início; falha durante a sessão fecha a conexão. Não há reenvio automático de comandos.

Sem Docker, com Python 3.12 e somente a biblioteca padrão:

```sh
python3 docker/scripts/calibrate_imu.py --host 192.168.0.10 --log docker/data/cal-imu.jsonl
```

## Executar as seis etapas

1. **Ambiente:** afaste a trena montada de fontes de interferência magnética, como estruturas metálicas magnéticas, PC e monitores. Confirme com Enter.
2. **Observar Magnetic Field:** acompanhe o status do magnetômetro. Enter só é aceito com relatório recente; não é necessário status alto nesta etapa.
3. **Acelerômetro:** faça as 4–6 posições escolhidas, mantendo cada uma por aproximadamente 1 s. As faces de um cubo são uma sugestão; ordem e alinhamento exato não são obrigatórios. Confirme cada posição separadamente.
4. **Giroscópio:** apoie a trena numa superfície imóvel por aproximadamente 2–3 s e confirme o repouso.
5. **Magnetômetro:** realize roll, pitch e yaw separadamente: gire aproximadamente 180° e volte ao início, em aproximadamente 2 s por eixo. Confirme cada movimento. Após yaw, se o status magnético não estiver recente em 2 ou 3, o guia apresenta uma nova rodada com novos identificadores. Qualidade alta antecipada não dispensa nenhum movimento.
6. **Salvar DCD:** digite **`salvar`**, depois de concluir os movimentos e observar qualidade magnética recente em 2 ou 3. Enter não salva. Aguarde `dcd=SAVED` e o término da restauração.

O firmware registra a declaração do operador; não reconhece os movimentos nem avança por cronômetro ou qualidade crescente. Não há janela adicional obrigatória de qualidade alta. Se a qualidade cair antes do SAVE, o guia volta às rotações. Se o relatório envelhecer, o SAVE fica bloqueado até haver feedback recente adequado.

O monitor separa Magnetic Field, Game Rotation Vector e Rotation Vector. Mostra status 0–3, idade, novidade e `accuracy_rad` do RV. Zero significa não confiável; 1 baixo, 2 médio e 3 alto. Esses números não são porcentagem de conclusão, não têm evolução necessariamente crescente e não fornecem qualidades individuais de acelerômetro/giroscópio. `accuracy_rad` também não garante erro contra uma referência externa.

`cancelar`, `sair` e Ctrl+C encerram a sessão e aguardam a restauração quando a conexão permite. Antes de SAVE, não enviam gravação. Durante SAVE, aguardam o resultado limitado: cancelar não desfaz uma gravação enviada. Cancelamento não restaura os coeficientes antigos em RAM. O monitor retorna 0 após gravação confirmada e restauração, 2 para cancelamento, 1 para erro/timeout e 130 para Ctrl+C; fechar um monitor que não iniciou sessão retorna 0.

## Protocolo compartilhado entre TCP e serial

Todos os comandos terminam com newline. TCP e serial chamam a mesma máquina de estados. A interface que iniciou a sessão controla confirmações, cancelamento e gravação; a outra pode consultar o status. IDs são locais ao boot, que é identificado pelo `boot_id` do HELLO. A serial continua sendo uma alternativa, não um requisito para o guia remoto.

| Comando | Resposta/efeito |
| --- | --- |
| `CAL_IMU START [4\|5\|6]` | Aceita em IDLE, com IMU pronta e política de autosave enviada sem erro. Padrão 6. Retorna `session_id`, `code=ACCEPTED`, `state=CALIBRATING phase=PREPARING`. |
| `CAL_IMU STATUS` | Consulta estado, instrução, qualidade, ações permitidas e último resultado, sem avançar etapas. |
| `CAL_IMU CONFIRM <session_id> <step_id>` | Confirma apenas a etapa corrente. ACK contém a próxima instrução; duplicatas não avançam. |
| `CAL_IMU SAVE <session_id>` | Só envia Save DCD depois de todas as confirmações e do critério magnético. `ACCEPTED` indica início, não persistência. |
| `CAL_IMU CANCEL <session_id>` | Cancela sem iniciar SAVE. Se já estiver salvando, responde `SAVE_IN_PROGRESS`. |
| `STATUS` | Continua disponível; informa `calibration`, `calibration_blocked` e os contadores acumulados da IMU. |

Respostas são uma linha ASCII `# OK CAL_IMU action=... code=...` ou `# ERR CAL_IMU action=... code=...`, seguidas pelos mesmos campos de status. `cal_schema=1 cal_line_max=2048` é anunciado no META. Comandos continuam limitados a 128 caracteres; há buffers fixos de 2048 bytes para respostas, sem stream espontâneo de relatórios no TCP.

Campos principais:

- `state`: `IDLE`, `CALIBRATING`, `READY_TO_SAVE`, `SAVING`. `phase` distingue `PREPARING`, `RESTORING` e `NONE`; a sessão permanece exclusiva durante preparação/restauração.
- `instruction`: `ENVIRONMENT`, `OBSERVE_MAG`, `ACCEL_POSITION`, `GYRO_REST`, `MAG_ROLL`, `MAG_PITCH`, `MAG_YAW`, `SAVE_DCD`, além dos estados de espera. `stage`, `position`, `positions`, `round`, `step_id` e `confirmed` contextualizam a instrução; `allowed` informa as ações permitidas. Na etapa de SAVE, `step_id=0`: a gravação usa somente `session_id` e não é uma confirmação de movimento.
- `mag_*`, `game_*` e `rv_*`: status, idade de recepção local em ms, geração e sequência. Status/idade `-1` indicam ausência; idade maior ou igual a `fresh_ms` indica dado desatualizado. Duplicatas de sequência não renovam a idade. Não são timestamps sincronizados de aquisição.
- `result`: `NONE`, `SAVED`, `CANCELLED`, `TIMEOUT`, `ERROR`. `detail` explica a causa, conservada após voltar a IDLE. `dcd`: `NOT_SAVED`, `PENDING`, `SAVED`, `FAILED`, `UNKNOWN`, independente da restauração.
- `read_rc`, `config_rc`, `verify_rc`, `save_rc`, `restore_rc`: retornos SH-2; 1 significa ainda não concluído, 0 sucesso, valores negativos erro (`-5` rejeição pelo hub, `-6` timeout). `rc` é o retorno mais recente, inclusive configuração de relatórios. `original_mask`, `effective_mask`, taxas iniciais/atuais, qualidade RV inicial e `part/build/version` registram a configuração e identificação.
- `resets`, `io_errors`, `decode_errors`, `rv_gaps`, `game_gaps`, `mag_gaps`: diagnóstico acumulado. Os contadores de sequência são separados por relatório e não medem, isoladamente, a causa de erros ou a precisão.

Exemplo de comandos manuais, sempre usando os IDs devolvidos pela placa:

```text
CAL_IMU START 6
CAL_IMU STATUS
CAL_IMU CONFIRM 1 1
CAL_IMU STATUS
```

Continue confirmando a etapa anunciada. Não copie uma sequência fixa de IDs de outra sessão. `SAVE` antecipado, outra sessão, outra interface e etapa fora de ordem são recusados. `CAPTURE` é recusado durante calibração; START é recusado durante aquisição. Ao terminar, STATUS/captura continuam compatíveis com o cliente existente.

## Configuração, prazos e persistência

Ao preparar, o firmware lê a máscara ME original, habilita a máscara manual `0x07` (acelerômetro, giroscópio, magnetômetro) e confere a leitura de volta. Preserva também os bits planar/on-table na máscara original para restaurá-los no final. Não chama `sh2_startCal()`/`sh2_finishCal()`.

Taxas solicitadas na calibração: Magnetic Field **50 Hz** (`20000 us`), Game RV 20 Hz (`50000 us`) e RV 10 Hz (`100000 us`). O magnetômetro segue a exigência do procedimento; as outras taxas são escolhas iniciais a validar no transporte real. Uma leitura SHTP vazia após a configuração estabelece a barreira de feedback; todos os caches são invalidados e somente relatórios posteriores contam. Falha de drenagem termina com `IMU_BACKLOG`.

Get/Set ME e Save DCD usam operações cooperativas no SH-2, com apenas uma operação ativa no loop. O envio ocorre uma vez, o polling normal entrega a resposta e o prazo é verificado antes de aceitar um ACK. São os mesmos comandos e decoders da biblioteca local, sem espera síncrona por respostas nem tarefas concorrentes. As operações síncronas usadas no boot também ganham um limite de espera no lab. O orçamento HAL continua sendo 100 ms por leitura/escrita.

Parâmetros opcionais em `include/lab_config.h`, com padrões também aplicados a configurações locais antigas:

| Parâmetro | Padrão | Faixa |
| --- | --- | --- |
| `LAB_CAL_SESSION_TIMEOUT_MS` | 300000 (5 min) | 1000–1800000 ms |
| `LAB_CAL_OP_TIMEOUT_MS` | 2000 | 100–5000 ms |
| `LAB_CAL_FRESH_MS` | 1000 | 100–2000 ms |

O prazo de sessão é da aplicação, aparece no monitor e nunca confirma movimentos ou inicia gravação. A conexão TCP tem o watchdog já existente de 60 s sem comandos. EOF/falha de rede percebida cancela imediatamente; perda silenciosa é detectada no erro de socket/watchdog. Serial usa o prazo de sessão e CANCEL explícito.

Depois de inicialização/reset, o lab envia `sh2_setDcdAutoSave(false)` e registra `autosave_rc`. **Configure Periodic DCD Save (0x09) não possui resposta**, segundo o [manual SH-2 v1.9, §6.4.7](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf). Por isso `autosave_ack=NOT_DEFINED`: zero comprova o envio aceito pelo transporte, sem inventar um ACK do BNO. Erro nesse envio impede START. A efetividade da política no firmware do BNO086 ainda deve ser confirmada na bancada.

Save DCD (0x06) tem resposta do sensor. Apenas ACK de sucesso gera `dcd=SAVED`; rejeição do hub gera `FAILED`; timeout, perda de confirmação ou reset durante SAVE geram `UNKNOWN`. Não há repetição automática. É possível `dcd=SAVED result=ERROR detail=RESTORE_FAILED`: a gravação foi confirmada, mas a restauração falhou. DCD salvo pertence à flash do BNO, separado da NVS do ESP.

Todo encerramento tenta desligar relatórios auxiliares, restaurar/verificar a máscara ME original, voltar ao RV a 50 Hz e invalidar caches. Uma restauração falha mantém CAPTURE/START bloqueados; reinicialize o conjunto e investigue o erro. Um reset observado permite nova tentativa limitada de restauração, sem retomar movimentos nem repetir SAVE. Desligar/ligar fisicamente após `SAVED` é necessário para validar persistência, mas não ocorre automaticamente.

## Verificação e aceite de bancada

Testes em software:

```sh
sh tests/lab/run.sh
docker compose -f docker/compose.yaml run --rm --entrypoint python status -m unittest discover -s tests -v
```

O conjunto C/C++ usa implementações reais de aquisição, driver, comandos SH-2 e sessão, com HAL/sensores simulados. Verifica seleção e ordem dos movimentos, 4/5/6 posições, qualidade baixa/ausente/desatualizada, duplicatas, exclusividade, prazos, reset, máscara com on-table, retorno de SAVE e restauração, inclusive falhas. Compila o driver também sem `MM1_LAB`. Os 35 testes Python incluem fragmentação TCP, ACK incorreto, timeout/EOF, consulta durante espera por entrada, registro JSONL e ausência de ações implícitas. Os testes nativos usam ASan/UBSan; em ambientes com ptrace que impeçam LeakSanitizer, `ASAN_OPTIONS=detect_leaks=0 sh tests/lab/run.sh` mantém os demais verificadores.

Na placa, executar ainda:

1. Sequência completa por Wi-Fi com USB desconectado, nas opções 4/5/6; repetir pelos mesmos comandos na serial. Conferir o produto/firmware registrado, taxas solicitadas e feedback recente sob a carga de 50/20/10 Hz.
2. Falta/duplicata de confirmação, IDs inválidos, SAVE antecipado, qualidade alta antes de terminar, qualidade oscilando, relatório ausente, timeout, disputa serial/TCP e bloqueio mútuo com CAPTURE.
3. Cancelamento, queda de Wi-Fi, reset, falha/timeout de comando e SAVE. Conferir o último resultado ao reconectar, ausência de reenvio e retorno ao RV a 50 Hz. Investigar os contadores de I²C/decodificação/sequência sem atribuir automaticamente sua causa à calibração.
4. Após SAVE confirmado, desligar/ligar o conjunto, registrar dados antes/depois e verificar conservação do DCD. Testar também a política sem autosave; retorno de software não substitui esse ensaio.
5. Captura unitária posterior com CSV preservado, azimute magnético sem offset e laser sem trim. Avaliação metrológica contra uma referência e lotes continuam nos incrementos seguintes.


## Revisão das mudanças posteriores à implementação inicial (02/10/2026)

O procedimento de bancada adotado pelo usuário é desligar e religar **todo o dispositivo** antes do ensaio. A investigação de reset apenas do P4 foi encerrada. Recuperação do barramento, alterações de monitor serial e o script de reconexão retirados pelo usuário não foram reaplicados.

| Mudança posterior | Decisão e motivo |
| --- | --- |
| Abertura SH-2 aguarda reset e anúncio/canal válido, com prazo | Manter. Evita enviar comandos com canal indefinido; o teste de ordenação reproduz o defeito anterior. Não constitui prova da causa dos boots físicos que falharam. |
| Propagação de erro de abertura da HAL e fechamento tolerante a transporte nulo | Manter. Não declarar transporte disponível após erro; liberar a instância sem dereferenciar ponteiro nulo. |
| Etapa/código de inicialização e preservação da falha no endereço que respondeu | Manter. São diagnósticos pequenos que evitam atribuir uma falha SH-2 a um NACK posterior do endereço alternativo. |
| Tamanhos definidos pelo protocolo para os relatórios usados no lab e rejeição de truncados | Manter. GET_CAL, SAVE e feedback dependem desses parsers, além do boot. Os tamanhos coincidem com a [implementação SH-2 da CEVA](https://github.com/ceva-dsp/sh2/blob/main/sh2.c). Os demais relatórios mantêm os comprimentos anunciados. |
| Rastreamento temporário de pacotes enviados/recebidos na HAL durante o boot (`BootIo`) | Remover. A investigação de reconexão da IMU foi encerrada; não é necessário instrumentar cada transferência de boot. Permanece um resumo SH-2 apenas se a identificação falhar. |
| Testes de abertura SH-2, erros SHTP e integração dos parsers | Manter. Validam as proteções acima independentemente de a bancada usar power-on reset. |

O timeout de operações síncronas sem prazo próprio e a implementação cooperativa de calibração já faziam parte do incremento 4. Permanecem necessários para atender STATUS/CANCEL e evitar travamento. Não foram adicionados reenvios automáticos, mudanças de pinagem ou tentativas de reset do sensor nesta revisão.

## Falha observada ao iniciar a calibração

O arquivo `docker/data/cal-imu-1790975398341376374.jsonl` registrou um único `CAL_IMU START 6`. A preparação terminou cerca de 2 s depois com:

```text
read_rc=-6 config_rc=1 verify_rc=1 save_rc=1
original_mask=-1 result=ERROR detail=SH2_COMMAND_FAILED
restore=OK rc=0
```

Isso identifica timeout de **GET_CAL ao ler a máscara original**, antes de qualquer Configure ME ou SAVE. `1` nos códigos por operação significa não executada. `rc=0` se referia à última ação de limpeza; `restore=OK` não significava sucesso da calibração. A máscara nunca havia sido alterada. As execuções seguintes do cliente só consultaram STATUS e exibiram o resultado retido da sessão 1; não eram novas falhas nem novos comandos de calibração.

O cliente agora imprime o resultado uma vez por sessão/resultado, mantendo apenas as linhas de feedback atualizadas. Mostra a operação que falhou e seu código, inclusive ao ler os campos do firmware anterior. O teste simula 80 consultas em modos ANSI e texto simples e verifica uma única mensagem, seguida de nova mensagem quando outra sessão termina. Abrir o monitor continua sem iniciar calibração.

O pedido GET_CAL é `F2`, comando `07`, subcomando `P3=01`, conforme o [manual SH-2, §6.4.6](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf). Os testes da pilha real verificam sua codificação e a resposta F1. **Os registros antigos não mostram se a resposta do sensor se perdeu, foi truncada ou rejeitada por comando/sequência; a causa do timeout físico permanece sem confirmação.** Havia erros de I²C e saltos de sequência antes do START, mas esses contadores não estabelecem a causa sozinhos.

Para distinguir essas situações sem depender de USB, CAL_IMU STATUS agora preserva a primeira falha antes das ações de limpeza:

| Campos adicionais | Significado |
| --- | --- |
| `failed_op`, `failed_rc` | Operação e retorno que falharam; não são substituídos por sucesso na restauração. |
| `cmd`, `cmd_seq`, `cmd_tx_rc` | Comando, sequência e retorno de seu envio pela biblioteca; zero não comprova execução pelo sensor. |
| `cmd_rx`, `cmd_matched` | Respostas F1 examinadas durante a operação e quantas coincidiram com comando/sequência. |
| `cmd_last`, `cmd_last_seq`, `cmd_status` | Última resposta F1 examinada; só interpretar quando `cmd_rx>0`. |
| `cmd_control`, `cmd_unknown`, `cmd_truncated` | Pacotes de controle, relatórios desconhecidos e truncados observados durante a operação. Os dois últimos também contam descartes no canal de sensores. |

Se houver nova falha, o monitor e o JSONL conterão esses dados. A calibração continua exigindo leitura válida da configuração original; não foi substituída por uma máscara presumida nem houve repetição automática de SAVE.

O firmware precisa ser regravado para obter os novos campos. A correção de exibição do cliente também funciona com o firmware anterior; reconstrua a imagem após futuras edições:

```sh
docker compose -f docker/compose.yaml build calibrate-imu
docker compose -f docker/compose.yaml run --rm calibrate-imu
```
