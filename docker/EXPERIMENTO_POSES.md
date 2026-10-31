# Coordenador de captura por pose — primeira implementação do incremento 8

O coordenador em `scripts/experiment.py` importa o cliente `status.py` e usa
a sequência do submódulo `kinova-geomagic-noetic-demo`. Em hardware há **um
contêiner**, com o coordenador e o driver Kortex em processos ROS separados;
RViz é opcional. O cliente da Trena é um módulo dentro do coordenador, sem
processo/servidor adicional. A comunicação com o driver usa ROS; com a Trena,
TCP. O Compose reutiliza a imagem `kinova-geomagic-noetic:demo` existente e
monta o código atual dos dois repositórios, sem precisar reconstruí-la.

Esta versão usa exclusivamente **`CAPTURE <id> 1` / schema 3**. O incremento 7
continua congelado: não há lotes de firmware nem reenvios. São 43 capturas,
uma em cada parada, incluindo zero inicial, preparação de J7 e retorno final.
O protocolo independente de `STATUS` e o monitor de calibração continuam disponíveis.

## Execução

Os comandos partem da raiz de `mm1-black`. A imagem do submódulo deve existir;
se necessário, construa-a conforme o README dele (incluindo as dependências
de fabricante). Ajuste `LAB_UID`/`LAB_GID` se seu usuário não for 1000:1000.
Os dados ficam em `docker/data`, montado como `/data` com permissão de escrita.
O Compose define `HOME=/tmp/mm1-home` e `XDG_RUNTIME_DIR=/tmp/mm1-runtime`.
O entrypoint cria `.rviz` e o runtime como o usuário do contêiner, com modo 700
no runtime, evitando herdar diretórios pertencentes ao root da imagem. Essas
preferências gráficas são temporárias; os registros em `/data` permanecem no host.

**Teste do fluxo inteiramente simulado**, sem ROS, rede ou hardware:

```bash
docker compose -f docker/compose.experiment.yaml run --rm experiment
```

Também funciona sem Docker, usando Python 3 e o submódulo inicializado:

```bash
python3 docker/scripts/experiment.py
```

O simulador usa juntas comandadas e leituras sintéticas constantes, identificadas
como `simulated`. Não modela a cinemática cartesiana nem a orientação da IMU.
É um teste de sequência/registro; seus dados não servem para avaliar os sensores.

**Prévia no RViz com Trena simulada**, usando o mesmo acesso X11 já configurado
para o Docker do manipulador:

```bash
docker compose -f docker/compose.experiment.yaml run --rm experiment \
  roslaunch mm1_experiment experiment.launch execution_mode:=rviz
```

Esse modo publica somente juntas de prévia, sem criar o driver ou chamar serviços
do robô. Transições visuais duram 2 s, com espera de 1 s, como no script original.
O registro identifica as juntas como `rviz_commanded` e deixa `tcp=null`.
Ao terminar as 43 capturas, o launch encerra, inclusive o RViz.

**Experimento real — este comando movimenta o braço e captura a Trena:**

```bash
docker compose -f docker/compose.experiment.yaml run --rm experiment \
  roslaunch mm1_experiment experiment.launch \
  execution_mode:=hardware robot_ip:=192.168.1.10 trena_host:=192.168.0.10
```

Substitua os dois IPs pelos equipamentos. `execution_mode:=hardware` seleciona
automaticamente `trena_mode:=tcp`. O primeiro movimento vai da posição atual ao
zero inicial da sequência. Use a preparação mecânica e o espaço livre já
verificados nos testes preliminares, considerando os acessórios instalados.
Não execute simultaneamente outro controlador, a varredura autônoma ou outro
cliente TCP da Trena. Não há planejamento de colisões nesta integração.

Parâmetros do launch:

| Parâmetro | Padrão / efeito |
| --- | --- |
| `execution_mode` | `rviz`; `hardware` seleciona movimento real |
| `trena_mode` | `simulated` na prévia, `tcp` no hardware |
| `start_rviz` | `true`; `false` desativa apenas a janela |
| `start_driver` | `true`; `false` reutiliza driver já ativo em `robot_name` |
| `vision` | `true`, conforme o braço usado |
| `robot_name` | `my_gen3` |
| `trena_host`, `trena_port`, `timeout` | IPv4, 5000, 5 s; aceitam LAB_HOST/PORT/TIMEOUT |
| `hold_sec` | 1 s de repouso contínuo antes de capturar |
| `first_tilt_deg` | 120°; aceita valores em `(90, 120]` |
| `position_tolerance_deg`, `velocity_tolerance_deg_s` | 0,1° e 0,5°/s |
| `visual_move_sec` | 2 s; somente prévia |
| `output_dir` | `/data`, um arquivo novo por execução |

Para testar apenas o cliente com a placa, sem movimentar o braço, é possível
combinar `--execution-mode simulated --trena-mode tcp --host IP` na CLI Python
(43 requisições unitárias). A pose continuará explicitamente simulada.
A CLI aceita `--log CAMINHO` e `--first-capture-id`; IDs subsequentes são
incrementados por pose. Um arquivo já existente é recusado, sem sobrescrita.

## Ordem e falhas

1. Abre o arquivo novo, grava parâmetros/revisões e consulta `STATUS`/`META`.
   Firmware incompatível ou destino sem escrita impede iniciar a sequência.
2. Usa os mesmos waypoints, validação de duração e `ACTION_END` do script
   original. J7 permanece em 270° da preparação até o retorno final.
3. Exige repouso por `hold_sec` com feedback recente antes de cada captura.
4. Registra a pose medida e faz a transação TCP completa, incluindo `DONE` e
   contagens. Durante a transação, cada feedback é verificado; um monitor
   adicional detecta ausência de feedback por mais de 0,5 s. Desvio transitório
   de posição/velocidade fica registrado como falha mesmo que o braço retorne.
5. Registra a pose medida final e grava o resultado com `flush` + `fsync`.
   Só então pode enviar o próximo waypoint.

Timeout, falha de movimento, perda de repouso, erro de protocolo, rede ou gravação
interrompem o ensaio e solicitam `Stop`. A perda de repouso solicita `Stop` pelo
monitor mesmo enquanto a chamada TCP aguarda; a chamada ainda termina pelo
seu prazo limitado, preservando a resposta disponível. Não há reenvio, retomada
nem retorno automático ao zero após falha. Um `Stop` recusado pelo driver é
reportado; a aplicação não garante parada se a comunicação com o robô falhar.

`DONE result=PARTIAL/ERROR` é uma transação concluída, mas também interrompe este
coordenador **depois de salvar** os dados, inclusive quando todos forem inválidos.
Qualidade baixa é preservada sem ser transformada em falha de protocolo; uma
singularidade angular não invalida um quaternion válido. Mudança de `boot_id`/
`imu_epoch` ou `imu_reset` interrompe o ensaio para não misturar continuidade do GRV.
Ctrl+C preserva a captura parcial quando possível e solicita `Stop`.

## Registro unificado

O JSONL tem `experiment_schema=1`, um `experiment_id` comum e registros:

- `session`: configuração, lista de alvos, revisão Git do repositório e do
  submódulo, estado de alterações do submódulo e SHA-256 dos scripts executados.
  A revisão pode ser `null` se o checkout/`git` não estiver disponível; os hashes
  ainda identificam os arquivos de código lidos.
- `preflight`: respostas de identificação e estado antes dos movimentos.
- `pose`: `pose_id`, índice desde zero, `capture_id`, alvo e feedback antes/depois,
  horários UTC e monotônicos do PC, resposta completa da Trena, dados derivados,
  aceitação, fase e eventual erro. Não são timestamps de aquisição do ESP.
- `end`: `complete`, `failed` ou `interrupted`, quantidade de poses aceitas e
  gravadas e duração. Ausência de `end` significa encerramento incompleto.

Em hardware, `robot_before/after` contém sete posições/velocidades e o TCP
**medido** de `BaseCyclic_Feedback.base.tool_pose_*`, em metros/graus no referencial
da base. O quaternion é derivado pela convenção `Rz(theta_z) Ry(theta_y) Rx(theta_x)`,
também usada pelo simulador Kortex do submódulo. Não são usados os campos
`commanded_tool_pose_*`. O TCP é o ponto de ferramenta configurado no controlador,
não necessariamente a origem da IMU ou do laser.

`trena` mantém as linhas recebidas, META, RV, GRV, campo magnético, distância,
validade/erro/qualidade, sessão de orientação e DONE. `exchange_complete=true`
indica DONE conferido; `accepted=true` indica que os critérios desta execução
passaram. Nenhum desses campos certifica qualidade metrológica. Erro ao gravar
pode impedir o registro de falha; nesse caso o stderr e o arquivo incompleto
são as evidências disponíveis. Confira `end.outcome`, inclusive ao usar roslaunch.

Os ângulos são recalculados de quaternions normalizados usando `META.laser_axis`
e a convenção do firmware em `src/lab/orientation.cpp`. Componentes indefinidas
na singularidade ficam `null`. META sem eixo deixa a derivação explicitamente
indisponível. Valores originais permanecem separados, sem sobrescrita.
O registro inclui a variação angular do TCP entre os dois feedbacks quando
disponível, mas **não calcula erro robô–IMU antes do alinhamento**: o campo fica
`null` com `FRAME_ALIGNMENT_NOT_CONFIGURED`. A montagem IMU↔TCP, base↔referencial
magnético e o yaw arbitrário de cada sessão GRV precisam ser tratados na análise
e no piloto do incremento 9. Esta implementação não aplica offsets implícitos.

## Verificação

```bash
python3 -m unittest discover -s docker/tests -v
python3 -m unittest discover -s kinova-geomagic-noetic-demo/tests -v
docker compose -f docker/compose.experiment.yaml config --quiet
```

Os testes verificam ordem/IDs, espera por DONE, captura inválida, perda de
estabilidade/feedback, rede, disco, interrupção, sessão da IMU e cálculo de
orientação (`q` e `-q`, rotação conhecida e singularidade). A suíte de protocolo
usa sockets de loopback; não conecta aos equipamentos.

Em 30/10/2026, passaram os 66 testes do cliente/coordenador/adaptador e os 18
testes da varredura também no Python da imagem Kinova. O launch foi executado
nessa imagem com rede isolada, `execution_mode:=rviz`, `start_rviz:=false` e
tempos reduzidos para o teste: as 43 poses e o encerramento completo do JSONL
foram conferidos. A expansão do launch de hardware foi verificada sem iniciar
o driver. A janela gráfica e a integração física não foram exercitadas aqui.

O aceite físico integrado permanece pendente. Incrementos 4–6 continuam com
validação pausada e o incremento 7 continua congelado. Não há firmware novo,
lotes, média de repetições nem calibração de referenciais nesta entrega.
