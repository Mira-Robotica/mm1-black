# Coordenador de captura por pose — incrementos 7 e 8

O coordenador `scripts/experiment.py` importa o cliente `status.py` e reutiliza
os 43 waypoints do submódulo `kinova-geomagic-noetic-demo`. Há um contêiner com
o coordenador, driver Kortex e RViz opcional. Cliente da Trena e coordenador
compartilham o processo Python; o driver recebe comandos ROS. A Trena usa TCP.

## Várias capturas na mesma pose

Por padrão são **5 CAPTUREs por pose**, configuráveis de 1 a 20. Isso gera
215 pedidos em uma execução completa das 43 poses. A quantidade é fixa: mesmo
que a primeira captura funcione, as demais são executadas e preservadas. Não
há tentativas extras além da quantidade configurada para buscar um sucesso.

O lote é coordenado no PC: cada pedido é `CAPTURE <id> 1` (ou `CAPTURE_IMU <id> 1`
com laser desabilitado) do schema 3,
com ID diferente e nova conexão. Esta decisão substitui, para este experimento,
a proposta de um único comando `CAPTURE <id> n`. Não é necessário atualizar o
firmware para usar vários pedidos por pose. A retentativa interna da IMU requer
o firmware que já implementa `LAB_IMU_MAX_RETRIES`.

O robô move uma vez, espera o repouso contínuo de 1 s e permanece na pose
para todas as capturas. Cada pedido aguarda DONE e valida suas contagens antes
de gravar o registro com flush/fsync. Feedback recente e repouso são monitorados
durante cada transação. Somente depois de salvar todas as tentativas e o resumo
da pose o coordenador envia o próximo waypoint. J7 permanece em 270° durante a
varredura e volta a zero na pose final, conforme a sequência já validada.

## Executar

Comandos a partir da raiz de `mm1-black`, com a imagem
`kinova-geomagic-noetic:demo` já disponível. O código é montado do checkout;
não é necessário reconstruir a imagem para estas mudanças do coordenador.

Teste inteiramente simulado (leituras sintéticas, juntas comandadas, sem ROS):

```bash
docker compose -f docker/compose.experiment.yaml run --rm experiment
```

Também pode usar Python diretamente:

```bash
python3 docker/scripts/experiment.py --captures-per-pose 5
```

Prévia visual com Trena simulada, usando o acesso X11 já configurado:

```bash
docker compose -f docker/compose.experiment.yaml run --rm experiment \
  roslaunch mm1_experiment experiment.launch execution_mode:=rviz captures_per_pose:=5
```

Hardware (movimenta o braço e captura a Trena):

```bash
docker compose -f docker/compose.experiment.yaml run --rm experiment \
  roslaunch mm1_experiment experiment.launch \
  execution_mode:=hardware robot_ip:=192.168.1.10 \
  trena_host:=150.164.212.184 captures_per_pose:=5
```

Use os IPs atuais dos equipamentos. O comando anterior sem `captures_per_pose`
agora também usa 5. `start_rviz:=false` desativa a janela e mantém o movimento
real quando `execution_mode:=hardware`. O launch encerra após a pose final.
Não execute outro controlador ou cliente TCP da Trena simultaneamente.

### Capturar somente a IMU

Quando a distância não fizer parte do ensaio, use **`use_laser:=false`**:

```bash
docker compose -f docker/compose.experiment.yaml run --rm experiment \
  roslaunch mm1_experiment experiment.launch \
  execution_mode:=hardware robot_ip:=192.168.1.10 \
  trena_host:=150.164.212.184 captures_per_pose:=5 use_laser:=false
```

Use os IPs atuais. **Recompile e grave o firmware `mm1_p4_lab` atualizado antes
desse uso**: o novo comando `CAPTURE_IMU <id> 1` precisa aparecer em `META.commands`.
Se não aparecer, o coordenador encerra na verificação inicial, antes dos movimentos,
com instrução de atualizar o firmware. Não substitui silenciosamente esse comando
por `CAPTURE`. A imagem do experimento não precisa ser reconstruída, pois monta
o código do checkout.

O padrão é `use_laser:=true`, que mantém a captura de distância e IMU. A opção
também funciona na prévia RViz e na simulação; em Python, use `--use-laser false`.
O modo vale para toda a execução e é salvo na configuração, nas tentativas e nos
resumos por pose, junto do comando enviado.

Sem laser, o firmware não acende o feixe nem dispara medição: inicia diretamente
a barreira de novidade da IMU e busca RV, GRV e campo magnético calibrado. Mantém
prazo, retentativas e detecção de reset. Não é necessário obter uma superfície
refletora. `LASER_RESYNC_REQUIRED` anterior não impede a captura somente da IMU;
esse modo **não limpa nem recupera** o estado do laser para usos posteriores.

A distância fica vazia, `laser_valid=0`, `laser_error=LASER_DISABLED` e
`DONE.laser_requested=0`. Essa ausência intencional não conta como falha:
`result=OK`/`n_valid=1` exigem somente os três relatórios da IMU válidos. Erros reais
da IMU ou da rede continuam registrados, mantendo a continuidade do experimento.
Ângulos derivados continuam usando o eixo geométrico informado em `META.laser_axis`,
mesmo sem emitir o feixe; o alinhamento dos referenciais continua pendente.

| Parâmetro | Padrão / efeito |
| --- | --- |
| `execution_mode` | `rviz`; `hardware` movimenta o robô |
| `captures_per_pose` | 5, entre 1 e 20 |
| `use_laser` | `true`; `false` seleciona `CAPTURE_IMU`, sem distância |
| `trena_mode` | `tcp` no hardware, `simulated` na prévia |
| `trena_host`, `trena_port`, `timeout` | IPv4; 5000; 5 s por etapa de rede |
| `start_rviz`, `start_driver`, `vision` | `true` |
| `robot_name` | `my_gen3` |
| `hold_sec` | 1 s de repouso contínuo antes da primeira captura da pose |
| `first_tilt_deg` | 120°, intervalo `(90, 120]` |
| `position_tolerance_deg`, `velocity_tolerance_deg_s` | 0,1°; 0,5°/s |
| `visual_move_sec` | 2 s, apenas para a prévia |
| `output_dir` | `/data` no launch |

A CLI Python usa `--captures-per-pose`, `--log` e `--first-capture-id`.
A última opção reserva IDs únicos para toda a execução e recusa overflow.
Arquivos existentes não são sobrescritos. `LAB_HOST`, `LAB_PORT` e `LAB_TIMEOUT`
permitem configurar a rede pelo ambiente; `LAB_UID`/`LAB_GID` ajustam o usuário
que escreve em `docker/data`, montado como `/data`. HOME e runtime do Qt ficam
em `/tmp/mm1-home` e `/tmp/mm1-runtime`; o entrypoint cria o runtime com modo 700.
Os registros persistem no host mesmo após remover o contêiner.

## Continuidade e relatório

Resultados PARTIAL/ERROR, timeout, EOF ou erro de protocolo de uma captura são
registrados e o coordenador segue para os pedidos planejados seguintes. Cada
pedido tem ID novo: não há reenvio silencioso de um pedido cujo resultado ficou
incerto. As mensagens recebidas até uma interrupção ficam no registro, com
`exchange_complete=false`. Mesmo sem nenhuma captura válida, a pose é salva
como `no_valid_capture` e o robô segue a sequência.

Uma mudança de boot/epoch ou reset da IMU invalida a aceitação daquela tentativa
e fica registrada. Pedidos seguintes podem fornecer dados de outra sessão; os
quaternions permanecem associados à sua própria sessão, sem realinhamento ou
média entre sessões. Qualidade baixa não invalida automaticamente um relatório
válido, e uma singularidade angular não invalida seu quaternion.

Falhas de movimento, perda de repouso/feedback, interrupção ROS/Ctrl+C e erro de
gravar os dados são fatais: solicitam Stop e encerram a execução. A identificação
inicial do protocolo também precisa passar antes de começar a movimentação.
O monitor solicita Stop se perder repouso enquanto aguarda a rede; a chamada
TCP continua limitada pelos seus prazos. Não há retorno automático ao zero
após interrupção. A integração não faz planejamento de colisões.

O JSONL usa **`experiment_schema=2`**, com:

- `session`: configuração, quantidade de pedidos por pose, alvos, revisões Git
  e hashes dos scripts. Os dados simulados são identificados como tais.
- `preflight`: identificação e estado inicial da Trena.
- `pose`: uma linha **por tentativa**, ligada a `pose_id`, `pose_index`,
  `attempt_index` (1..n), `attempts_requested` e `capture_id` único. Inclui o
  resultado e dados parciais da Trena, feedback antes/depois, horários do PC,
  `accepted`, `exchange_complete`, erro e tipo de falha. Falhas fatais têm
  `fatal=true`. O schema anterior tinha apenas uma tentativa por pose.
- `pose_summary`: número de capturas gravadas/aceitas/falhas e contagens de
  validade por sensor. `outcome` é `ok`, `partial` ou `no_valid_capture`.
  `product_improvement_required=true` destaca poses com qualquer falha.
- `end`: `complete` ou **`complete_with_failures`** quando a sequência termina;
  `failed`/`interrupted` quando para antes. Traz `saved_poses` (resumos de poses
  gravados), `failed_captures`, `poses_with_failures` e
  `poses_without_valid_capture`, além de `product_improvement_required`.

`accepted` exige transação concluída, todos os resultados solicitados válidos
(laser/RV/GRV/MAG ou apenas RV/GRV/MAG com `use_laser=false`) e continuidade
verificada da sessão naquela tentativa. Uma captura parcial pode conter
orientação útil mesmo sem distância: os dados e as contagens individuais são
preservados para a análise. `no_valid_capture` significa nenhuma tentativa
inteiramente aceita, não necessariamente ausência de todos os dados.

Código de saída 0 significa sequência concluída, inclusive com falhas de
sensores. Confira `end.outcome` e as listas de poses, inclusive usando roslaunch.
A ausência do registro `end` significa encerramento incompleto. Um erro de disco
pode impedir até o registro da própria falha; nesse caso o stderr também é evidência.

Em hardware, o TCP é medido por `BaseCyclic_Feedback.base.tool_pose_*`, em metros
e graus no referencial da base; o quaternion usa Rz(theta_z) Ry(theta_y) Rx(theta_x).
A prévia fornece juntas comandadas e `tcp=null`. Horários são do PC, sem
sincronização fina com o ESP. RV, GRV, MAG, qualidade e sessão são preservados.
Os ângulos são recalculados com `META.laser_axis`; a comparação robô–IMU permanece
`FRAME_ALIGNMENT_NOT_CONFIGURED` até validar os referenciais e a montagem.

## CAPTUREs versus retentativas da IMU

Cada CAPTURE inicia outra aquisição dos sensores selecionados, tem seu próprio prazo e
produz outra amostra. Isso permite medir dispersão e taxa de falhas por pose.
As retentativas internas tratam erros de I/O durante **uma** captura: preservam
laser e relatórios já obtidos, e buscam os ausentes dentro do prazo original.

`LAB_IMU_MAX_RETRIES` em `include/lab_config.h` define 0–20 retentativas, padrão 3;
alterar esse valor requer recompilar/gravar o firmware. O DONE/JSONL conserva
`imu_retries`, `imu_retry_limit` e `imu_io_errors`. Aumentar a quantidade de
CAPTUREs não altera esse limite. Cinco CAPTUREs podem ter até três retentativas
internas cada, mas cada captura ainda respeita o prazo original da IMU.

## Verificação

### Parada reportada na captura 213 (ensaio somente IMU)

O log do ensaio enviado após a implementação de `CAPTURE_IMU` chegou à pose
43/43, tentativa 3/5, e terminou com `ACTION_ABORT`, `abort_details=55`
(`CONTROL_MANUAL_STOP`, conforme o enum Kortex local). Os Stop de encerramento
foram recusados com `SESSION_NOT_IN_CONTROL` (130). Isso é uma ocorrência do
controle do braço, separada dos resultados ERROR das capturas da IMU. O log não
identifica a origem da parada nem comprova se houve intervenção pelo Web App,
botões ou outro cliente; também não permite atribuir a perda de controle a
uma causa específica.

O coordenador agora compartilha uma única tentativa de Stop entre watchdog,
tratamento de exceções e shutdown, inclusive quando o serviço recusa a sessão.
O diagnóstico de código 55 inclui seu nome. A parada permanece fatal: não há
retomada automática nem tentativa de tomar controle de outra sessão.

As mensagens `uninitialized ServerGoalHandle` vêm dos action servers de
trajetória do driver Kortex: no tratamento de ACTION_ABORT, consultam
`m_goal.getGoalStatus()` antes de testar se há trajetória em execução. Nosso
experimento envia ações por serviço, sem criar um goal desses action servers.
Esse diagnóstico secundário do driver ainda pode aparecer em aborts; eliminar
Stop duplicado reduz as chamadas de encerramento, mas não corrige esse caminho
C++ do driver. A origem da parada/perda de controle precisa ser observada no
próximo ensaio físico. Testes do adaptador cobrem parada concorrente, recusa do
serviço e preservação do caráter fatal de CONTROL_MANUAL_STOP.

Verificação do modo sem laser em 31/10/2026: 77 testes Python e nove suítes
nativas com ASan/UBSan aprovados; build `mm1_p4_lab` aprovado. O launch ROS na
imagem Kinova completou 43 poses e 215 capturas sintéticas aceitas com
`use_laser:=false`, `end.outcome=complete` e distância ausente. Esse teste usou
rede isolada, driver real e janela RViz desativados, com tempos de prévia
reduzidos somente para o teste. Firmware não gravado e aceite físico pendente.

```bash
python3 -m unittest discover -s docker/tests -v
```

Cobertura: 1/5/20 pedidos, IDs, espera por DONE, falhas iniciais/intermediárias/
totais, continuidade, reset/sessão, rede, armazenamento, movimento e repouso.
Os testes usam simulação e sockets de loopback. O fluxo físico com vários pedidos
e somente IMU foi concluído nos dois ensaios registrados abaixo; os demais
critérios de aceite continuam pendentes. Em 31/10/2026, o ensaio anterior com um pedido
por pose chegou a 28 capturas válidas; recuperou erros na captura 6 (2 retentativas)
e 26 (1), e encerrou na 29 por 4 erros de I/O com 3 retentativas esgotadas.

## Encerramento e reconexão (31/10/2026)

Após salvar o encerramento completo, o coordenador dispensa Stop redundante
na saída e no callback de shutdown. O último waypoint já terminou e seu repouso
foi confirmado. Falhas/interrupções ainda solicitam Stop. Isso evita disparar
ACTION_ABORT nos action servers do Kortex sem goal ROS (o executor usa serviços
ExecuteAction), que consultavam um ServerGoalHandle não inicializado.
O banner de roslaunch “REQUIRED process ... has died / finished cleanly” indica
a saída normal do nó required e o encerramento dos demais processos. O resumo
impresso e end.outcome identificam o resultado do experimento.

Há uma pausa mínima de 200 ms entre conexões de captura para reduzir a disputa
com o encerramento da conexão anterior no servidor de cliente único. Isso é uma
mitigação para EOF antes de HELLO, cuja causa exata exige diagnóstico no ESP.
Não repete o pedido; uma falha remanescente continua registrada.

## Identificação dos arquivos e mensagens de progresso

O nome automático registra a criação em UTC, com microssegundos, por exemplo
`experiment-2026-10-31T20-18-14.365123Z.jsonl`. O sufixo Z indica UTC; a data
também fica em `session.metadata.created_utc`. O UUID continua como identificador
interno para associar registros. `--log` permite escolher um nome explícito;
a abertura exclusiva impede sobrescrever arquivos existentes.

Cada pose anuncia primeiro movimento/repouso. As tentativas 1..n são anunciadas
após chegar à pose e conferir o feedback. Um resumo encerra a pose antes de
anunciar o movimento seguinte, evitando que a tentativa 1 pareça pertencer
ao final da pose anterior. A associação dos IDs nos dados permanece a mesma.


### Ensaio integrado de 31/10/2026

Na execução `experiment-2026-10-31T20-51-38.168494Z.jsonl`, o manipulador
completou 43 poses e 215 capturas. Foram 119 capturas completas aceitas e 96
com falhas: uma somente da IMU, 93 somente do laser e duas de ambos. Não houve
falhas TCP registradas; 19 poses ficaram sem captura integralmente aceita.

Na captura 114, RV e campo magnético tiveram `IMU_IO_ERROR` após três
retentativas; GRV e laser foram válidos. Nas capturas 163 e 213, os três
relatórios de IMU falharam após três retentativas. Os três relatórios foram
válidos juntos em 212/215 tentativas. Houve 11 capturas integralmente aceitas
com retentativas da IMU. Os dados de orientação válidos das 19 poses permanecem
no JSONL, embora a aceitação integral também exija o laser.

Na captura 121, o laser retornou `LASER_TIMEOUT`. Nas 94 capturas seguintes,
o firmware retornou `LASER_RESYNC_REQUIRED`: após o timeout, bloqueia novas
medições porque uma resposta atrasada não pode ser associada com segurança
a um pedido posterior. Isso representa um timeout inicial e bloqueios
subsequentes, não 95 timeouts independentes. A causa do timeout não foi
determinada. O coordenador continuou como solicitado e o registro terminou
com `complete_with_failures`. A validação física da recuperação do laser e
dos demais critérios de aceite continua pendente.


### Dois experimentos concluídos somente com IMU — 31/10/2026

O usuário relatou duas execuções concluídas com `use_laser:=false`. Os JSONL
locais confirmam os resultados abaixo; o log de terminal enviado corresponde
à segunda execução. Em ambas, o braço percorreu todas as 43 poses, com cinco
tentativas em cada uma, e todas as poses tiveram pelo menos uma captura aceita.

| Arquivo JSONL | Tentativas / aceitas | Falhas de aquisição | Encerramento |
| --- | --- | --- | --- |
| `experiment-2026-10-31T22-52-45.674363Z.jsonl` | 215 / 202 | 13 de sensor | `complete_with_failures` |
| `experiment-2026-10-31T23-15-00.148218Z.jsonl` | 215 / 202 | 12 de sensor e 1 de transporte | `complete_with_failures` |

Na primeira execução, as 13 falhas de sensor foram `IMU_IO_ERROR` nos três
relatórios. Na segunda, a captura 15 falhou com EOF antes de completar o HELLO:
não há amostra para atribuir essa ocorrência à IMU. As capturas 57, 84, 92, 99,
114, 117, 148, 159, 163, 167 e 210 retornaram `IMU_IO_ERROR` nos três relatórios;
a captura 100 retornou `IMU_DECODE_ERROR`. As falhas foram registradas e a
sequência continuou, conforme a política do experimento. Não houve pose sem
captura válida nem interrupção fatal nessas duas execuções. Cada execução
preservou uma única sessão de orientação entre as amostras recebidas.

No log da segunda execução, às `1793488506.058733541` e
`1793488506.058773176` (tempo ROS), apareceram duas mensagens
`Attempt to get goal status on an uninitialized ServerGoalHandle or one that
has no ActionServer associated with it.` Elas ocorreram durante o primeiro
movimento, após a validação da duração e antes da primeira captura. Todas as
cinco capturas da primeira pose foram aceitas e o experimento terminou
normalmente. Não há evidência de prejuízo à coleta causado por essas mensagens
nesse ensaio. O aviso do driver continua pendente; esse log não identifica o
evento de notificação exato que acionou a consulta ao goal. O texto da mensagem
isoladamente não prova perda de controle nem uma parada do braço.

Esses resultados confirmam o fluxo físico de captura somente da IMU e a
continuidade após falhas individuais. Não encerram os aceites pausados dos
incrementos 4/5/6, a recuperação do laser ou a validação de precisão e
alinhamento de referenciais. Após a correção do encerramento, a suíte Python
completa passou com 79 testes, incluindo Stop concorrente/recusado e diagnóstico
de `CONTROL_MANUAL_STOP`.
