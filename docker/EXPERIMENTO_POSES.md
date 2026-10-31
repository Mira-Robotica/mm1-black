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

O lote é coordenado no PC: cada pedido é `CAPTURE <id> 1` do schema 3 existente,
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

| Parâmetro | Padrão / efeito |
| --- | --- |
| `execution_mode` | `rviz`; `hardware` movimenta o robô |
| `captures_per_pose` | 5, entre 1 e 20 |
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

`accepted` exige transação concluída, quatro sensores válidos e continuidade
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

Cada CAPTURE inicia outra aquisição de laser e IMU, tem seu próprio prazo e
produz outra amostra. Isso permite medir dispersão e taxa de falhas por pose.
As retentativas internas tratam erros de I/O durante **uma** captura: preservam
laser e relatórios já obtidos, e buscam os ausentes dentro do prazo original.

`LAB_IMU_MAX_RETRIES` em `include/lab_config.h` define 0–20 retentativas, padrão 3;
alterar esse valor requer recompilar/gravar o firmware. O DONE/JSONL conserva
`imu_retries`, `imu_retry_limit` e `imu_io_errors`. Aumentar a quantidade de
CAPTUREs não altera esse limite. Cinco CAPTUREs podem ter até três retentativas
internas cada, mas cada captura ainda respeita o prazo original da IMU.

## Verificação

```bash
python3 -m unittest discover -s docker/tests -v
```

Cobertura: 1/5/20 pedidos, IDs, espera por DONE, falhas iniciais/intermediárias/
totais, continuidade, reset/sessão, rede, armazenamento, movimento e repouso.
Os testes usam simulação e sockets de loopback. O aceite físico do fluxo com
vários pedidos permanece pendente. Em 31/10/2026, o ensaio anterior com um pedido
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
