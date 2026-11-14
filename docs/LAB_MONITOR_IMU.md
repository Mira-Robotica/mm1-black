# Monitor contínuo da IMU por TCP

Implementação de `docs/PLANO_MONITOR_STREAMING_IMU.md`, integrada ao clone que estava
em `a93dd19`. Exclusiva do firmware **`mm1_p4_lab`**: não requer ROS, Kinova, display,
servidor web ou medições de laser. CAPTURE, CAPTURE_IMU e calibração mantêm seus
comandos e formatos. A validação inicial em hardware está registrada ao final deste guia.

## Executar

Compile e grave o firmware de laboratório usando o procedimento de
[LAB_SENSORES_P4.md](LAB_SENSORES_P4.md). A pinagem deste clone é definida em
`src/board/p4/imu_sparkfun/config.h`; não copie pinagens de outra revisão do plano.
O monitor requer `stream_imu=1 stream_schema=1` em META e recusa firmware anterior.
Não há atualização automática do firmware.

Na raiz do repositório, com a placa conectada ao Wi-Fi:

```sh
pio run -e mm1_p4_lab
mkdir -p docker/data
docker compose -f docker/compose.yaml build monitor-imu
LAB_HOST=192.168.1.100 docker compose -f docker/compose.yaml run --rm monitor-imu --rate-hz 2 --save false
LAB_HOST=192.168.1.100 docker compose -f docker/compose.yaml run --rm monitor-imu --rate-hz 2 --save true --notes "Mesa; trena fixa; aproximação e retirada do ímã"
```

Substitua o IP pelo atual da placa. A rede é host; só um cliente TCP é aceito.
`LAB_UID`/`LAB_GID` controlam as permissões no volume, como no serviço status.
A imagem é a mesma dos clientes status/calibrate-imu, sem dependências ROS.

Sem Docker, basta Python e sua biblioteca padrão:

```sh
python3 docker/scripts/monitor_imu.py --host 192.168.1.100 --rate-hz 2 --save true --output-dir ./docker/data --notes "Condições observadas"
```

| Opção | Padrão / significado |
| --- | --- |
| `--host` / `LAB_HOST` | IPv4 obrigatório |
| `--port` / `LAB_PORT` | 5000 |
| `--timeout` / `LAB_TIMEOUT` | 5 segundos de margem por operação, não duração da sessão |
| `--rate-hz` | 2; faixa 0,2 a 5, usando ponto decimal na CLI |
| `--save true\|false` | false; sem arquivos de sessão quando false |
| `--output-dir` | `/data`; volume `docker/data` no Compose |
| `--notes` | Condições informadas pelo operador; nenhuma condição é presumida |

Ctrl+C ou SIGTERM inicia STOP e aguarda a restauração. O painel restaura o cursor ao
sair. Fora de TTY, imprime blocos de texto sem escapes ANSI. Falhas pontuais de
amostra não imprimem mensagens: cada campo retém seu último valor válido, com idade
por relatório. Antes da primeira amostra, aparece `—`. Falhas de sessão/conexão/disco
encerram com uma mensagem e código 1; argumentos inválidos retornam 2; encerramento
normal ou por sinal com restauração confirmada retorna 0.

## O que os valores significam

- **Raw (0x16):** XYZ e timestamp nativos são decodificados; XYZ exibidos em ADC,
  sem conversão presumida para µT. Status baixo não os oculta.
- **Uncalibrated (0x0F):** XYZ em µT, já com correção soft iron do sensor. O cliente
  e o firmware do monitor não subtraem o viés de hard iron fornecido no relatório.
- **Calibrated (0x03):** XYZ em µT corrigidos pelo sensor.
- **RV (0x05):** quaternion corpo → mundo ENU, frente **+X**, azimute magnético
  horário a partir do norte. Identidade dá 90°, rotação +90° em Z dá 0°.
- **GRV (0x08):** mesma geometria com referência **arbitrária**, sujeita a deriva.
  Não é zerado ou alinhado ao RV.

O rótulo **Inclinação (pitch)** representa a inclinação do feixe gravada em
`p.pitch` na produção, e não o pitch Euler `imu_pitch`. Frente elevada gera valor
positivo; quaternion de pitch Euler +30° gera inclinação −30°. Roll usa a relação
da produção. Não há `g_azimuth_offset_deg`, declinação, tare, NVS ou outra correção
angular. A norma do quaternion deve estar a até 0,02 de 1; não há normalização
silenciosa. Projeção frontal horizontal ou par de componentes de roll abaixo de
1e−5 torna apenas o ângulo correspondente indisponível para atualização.

**Correção de sinal em 13/11/2026.** Com +Y à esquerda, o azimute horário
usa o termo lateral positivo. A versão inicial seguia o sinal negativo do plano,
que invertia o sentido do azimute. A versão corrigida anuncia
`mag_azimuth_method=atan2_left_forward_v2` em STREAM_META. Ausência desse campo
identifica o método antigo. O cliente atualizado registra a fórmula correspondente
ao firmware e continua lendo ambos os formatos. Recompile/grave o firmware e
reconstrua a imagem do cliente para usar a correção e seus metadados.
A [análise das quatro posições](ANALISE_IMU_QUATRO_POSICOES_2026-11-13.md) detalha
os dados que motivaram a verificação e os limites do mapeamento Raw observado.

O azimute Uncalibrated é uma **extensão geométrica**, não um helper de magnetômetro
já existente na produção. Usa somente a vertical do GRV:

```text
u = normalize(R_grv^T * (0,0,1))
f = normalize((1,0,0) - dot((1,0,0),u)*u)
l = cross(u,f)
azimuth = wrap360(degrees(atan2(dot(m,l), dot(m,f))))
```

Nivelado: +X→0°, +Y→90°, −X→180°, −Y→270°. O yaw arbitrário do GRV cancela.
Exige **magnetômetro novo**, GRV válido no mesmo epoch, até 200 ms de diferença entre
recepções locais e ambos com idade até 200 ms no instante de cálculo. Campo
horizontal abaixo de **0,01 µT** ou frente quase vertical retém o último ângulo.
GRV novo sozinho não atualiza o azimute magnético.

**Azimute Raw permanece indisponível.** Não foi estabelecida a transformação entre
os eixos físicos Raw e os fusionados, nem a comparabilidade das escalas ADC entre
eixos. `raw_mapping=unknown`, `raw_azimuth=unavailable` e
`raw_horizontal_min_adc=unknown` registram essa limitação. Não existe um limiar ADC
inventado ou transformação identidade implícita. A coluna existe, mas permanece
vazia. Os XYZ Raw são úteis para observar perturbações; esta entrega não valida o
azimute Raw. Uma futura transformação deve ser fixa, documentada e comprovada,
preservando os XYZ nativos; nenhuma correção dinâmica pode ser estimada pelo ímã.

## Sessão e restauração

HELLO continua na versão 3, com CAPTURE schema 3. META acrescenta capabilities.
O protocolo de streaming usa linhas ASCII terminadas em LF (cliente aceita CRLF):

```text
STREAM_IMU START rate_hz=2
# OK STREAM_IMU START session_id=1 state=CONFIGURING
... configurações confirmadas pelo sensor ...
# OK STREAM_IMU READY session_id=1
# STREAM_META schema=1 ...
session_id,seq,...
... snapshots CSV ...
STREAM_IMU PING session_id=1
# OK STREAM_IMU PING session_id=1
STREAM_IMU STOP session_id=1
... restauração confirmada pelo sensor ...
# OK STREAM_IMU STOP session_id=1 restored=1 code=NONE
```

START pertence à conexão. É recusado durante captura, calibração, reset pendente,
outra sessão ou restauração pendente/falha. STATUS continua permitido; comandos
conflitantes por **serial e TCP** retornam BUSY. START pela serial é recusado.
STOP da última sessão é idempotente na mesma conexão.

Antes de modificar qualquer feature, a sessão lê a máscara de calibração e as
configurações completas dos cinco relatórios pelo SH-2. Snapshot incompleto recusa
a sessão sem alteração. Preserva intervalos não padrão e os demais campos de
feature (flags, sensibilidade, batch e sensor-specific). Relatórios antes
inativos recebem inicialmente 20.000 µs; os já ativos mantêm o intervalo lido.
A publicação TCP tem seu próprio relógio: pedir 2 Hz **não muda RV/GRV para 2 Hz**.

Set Feature aceito pelo transporte não basta. O serviço pede Get Feature e aguarda
nova resposta, registrando o intervalo **confirmado**, inclusive se o sensor o
ajustar. Confirmação de intervalo não é comprovação da cadência de amostras em
bancada. Cada etapa tem prazo de 2 s. O cliente permite até 24 s mais `--timeout`
para preparação; continua enviando PING durante a espera.

STOP, EOF, perda de Wi-Fi, erro/timeout de envio, heartbeat expirado ou reset da
IMU iniciam restauração cooperativa executada pelo loop mesmo sem socket. São
restauradas e comparadas todas as features com o snapshot, incluindo as desabilitadas.
A máscara de calibração é lida novamente; em operação normal nunca é modificada
pelo monitor. Após reset inesperado, reinstala a máscara anterior e a confirma.
A sessão é interrompida, o epoch é invalidado e o streaming não reinicia sozinho.

A política DCD conhecida é autosave desabilitado por envio no bring-up já existente;
o SH-2 não define ACK para esse comando, portanto META o identifica como
`disabled_sent_unacknowledged`. O monitor não grava/apaga DCD, não dispara
calibração guiada, não aplica tare nem escreve NVS. Reset físico espontâneo ainda
aciona a recuperação já existente no adaptador; o monitor não solicita reset.
Restaurar configuração **não desfaz a evolução interna da fusão/calibração dinâmica**.

Falhas de preparação geram rollback. Restauração falha resulta em `restored=0`,
`code=RESTORE_FAILED` e bloqueia operações conflitantes. Não se anuncia sucesso por
timeout. Para repetir uma restauração após corrigir o problema de comunicação:
consulte `STATUS` (`stream_state`, `stream_session_id`) e envie
`STREAM_IMU STOP session_id=<id>`. Se a conexão original foi perdida e a restauração
falhou, uma nova conexão pode executar explicitamente esse STOP; ela não pode
iniciar nova aquisição antes da recuperação. Não use IMU_RESET para pular o bloqueio.

PING a cada 5 s mantém um prazo de 15 s; tráfego de saída não renova o heartbeat.
O limite antigo de 60 s sem comandos permanece compatível com esses PINGs.
SIGKILL/perda física de rede não podem enviar STOP: dependem dessa restauração
no firmware. Após STOP o cliente drena dados em trânsito antes do ACK, com prazo
máximo de 14 s mais a margem de operações. O serviço Docker configura `stop_grace_period: 30s`; aumente esse prazo se aumentar
`--timeout`. SIGKILL ainda depende do firmware.

O envio usa `MSG_DONTWAIT`: um buffer de 2048 bytes e um de controle do mesmo tamanho,
sem fila crescente. STOP/PING são lidos durante envio parcial. Nenhuma linha parcial
é truncada ou intercalada. Um snapshot ainda sem bytes enviados pode ser substituído
pelo mais recente, preservando os bits de novidade; sequência pode saltar. O prazo
de 2 s de envio não é reiniciado pelas substituições. O polling da IMU continua.

## Schema de streaming 1

Cabeçalho compartilhado por testes C++ e Python em
`docker/tests/fixtures/stream_header.csv`. A fixture `stream_meta.txt` é saída de uma
sessão **simulada**, não evidência do hardware.

```csv
session_id,seq,device_ms,updated_mask,valid_mask,value_updated_mask,raw_age_ms,uncal_age_ms,cal_age_ms,rv_age_ms,grv_age_ms,mag_raw_x_adc,mag_raw_y_adc,mag_raw_z_adc,mag_uncal_x_uT,mag_uncal_y_uT,mag_uncal_z_uT,mag_cal_x_uT,mag_cal_y_uT,mag_cal_z_uT,rv_azimuth_deg,rv_inclination_deg,rv_roll_deg,grv_azimuth_deg,grv_inclination_deg,grv_roll_deg,mag_raw_azimuth_deg,mag_uncal_azimuth_deg
```

- `session_id`, `seq`, `device_ms`: inteiros sem sinal; sequência crescente por
  sessão, relógio local uint32 em ms (com wrap). ID mais boot_id identifica a sessão.
- `updated_mask`: bits **0 Raw, 1 Uncalibrated, 2 Calibrated, 3 RV, 4 GRV**; relatório
  válido novo desde a publicação anterior. As cinco idades são vazias antes da
  primeira amostra, depois tempo desde a recepção local da última amostra válida.
- `valid_mask`: bits **0–16 na ordem das 17 colunas científicas**; existe valor
  válido retido. Não significa que ele seja novo/recente.
- `value_updated_mask`: mesmos bits, para os valores com atualização válida desde
  a publicação anterior. É subconjunto de `valid_mask`. Bits 9–11 correspondem a
  azimute/inclinação/roll RV; 12–14 aos GRV; 15 Raw e 16 Uncalibrated magnéticos.
  Bit 15 permanece zero nesta implementação.
- As 17 grandezas contêm o último valor válido, ou vazio antes da primeira leitura.
  Quaternion inválido não substitui seu cache. Singularidade invalida apenas a
  atualização daquele ângulo. Baixa qualidade não invalida Raw/Uncalibrated.

Um snapshot reúne relatórios **assíncronos**, não uma aquisição simultânea. Decoder,
sequências repetidas e valores inválidos não sobrescrevem os caches de streaming.
Estes são separados dos snapshots históricos usados pelos experimentos existentes.
O cliente descarta linha truncada, desalinhada, oversized, sequência repetida ou
sessão incorreta; não tenta reconstruir colunas. Um grupo magnético numericamente
inválido preserva seus três valores, deixando os outros grupos avançarem. Ângulos
são validados individualmente; o cliente não recalcula geometria.

STREAM_META é emitido uma vez, depois de READY e antes do cabeçalho. Contém firmware,
produto/build/versão IMU, epoch, taxa, convenções e configurações. Os arrays
`previous`, `requested`, `confirmed` têm ordem Raw/Uncal/Cal/RV/GRV, entradas separadas
por `;`, cada uma `interval_us/batch_us/flags/change/specific`. Flags seguem SH-2:
bit 0 change-relative, bit 1 change-enabled, bit 2 wake, bit 3 always-on. Intervalo
zero representa relatório desabilitado. `previous` é lido, não default presumido;
`confirmed` vem de resposta do sensor. Máscara vem de GET_CAL. Informações sem
confirmação são declaradas como tais.

## Arquivos de uma execução

Com `--save true`, cria diretório exclusivo `UTC_uuid` com:

1. `metadata.json`, aberto exclusivamente e escrito **uma vez**, antes das amostras.
   Inclui configuração do cliente, versão/commit quando disponível (null na imagem
   sem Git), notas, host/porta, HELLO/META/STATUS inicial e STREAM_META completo,
   unidades, método, tolerâncias, políticas e limites. Não é reescrito no término e
   não contém um estado final fictício. Não são coletadas credenciais/variáveis de
   ambiente além das opções de conexão declaradas.
2. `samples.csv`, cabeçalho `elapsed_s` seguido das 17 colunas científicas.
   Uma linha por snapshot aceito. **Somente atualizações válidas daquele snapshot
   são preenchidas**; os outros campos ficam vazios, mesmo que a tela mostre um
   valor retido. Não se fabricam medições repetindo dados congelados.

`elapsed_s` usa relógio monotônico do PC relativo ao começo da gravação; não
sincroniza a hora física do sensor. Flush ocorre no máximo uma vez por segundo
durante amostragem, além de cabeçalho e fechamento. Erro de disco interrompe a sessão
e tenta STOP. Um diretório parcialmente criado pode permanecer como evidência do
erro; não é reutilizado/sobrescrito. As notas são texto livre e são gravadas como
fornecidas: não inclua senhas nelas.

## Validação automatizada desta entrega

Comandos executados e resultados registrados após a implementação:

```sh
ASAN_OPTIONS=detect_leaks=0 sh tests/lab/run.sh
python3 -m unittest discover -s docker/tests -v
docker compose -f docker/compose.yaml config
docker compose -f docker/compose.yaml build monitor-imu
pio run -e mm1_p4_lab
docker compose -f docker/compose.yaml run --rm --entrypoint python monitor-imu -m unittest discover -s tests -p test_monitor_imu.py -v
# Regressão completa: os testes de experimentos usam fixtures do submódulo Kinova.
docker compose -f docker/compose.yaml run --rm -v "$PWD:/workspace:ro" --entrypoint python monitor-imu -m unittest discover -s /workspace/docker/tests -v
```

Resultado: **11 executáveis nativos aprovados**, **94 testes Python aprovados no
host e no Docker com o clone montado somente para leitura**, além dos **15 testes
do monitor na imagem isolada**. Compose válido, imagem construída e build
`mm1_p4_lab` aprovado. Nenhum desses resultados substitui o aceite físico abaixo.

A descoberta de toda a suíte na imagem isolada também foi tentada: 60 testes
executaram, mas dois módulos de experimentos não puderam ser importados porque
`spherical_sweep_sequence`/fixtures Kinova não fazem parte desse contexto Docker.
A montagem somente para leitura acima permite executar esses testes existentes
sem incorporar ROS/Kinova ao serviço. O teste específico do monitor usa apenas
os arquivos já presentes na imagem.

O comando nativo original foi tentado primeiro. LeakSanitizer falhou sob ptrace neste
ambiente; repetiu-se com **somente detecção de leaks desabilitada**, preservando
AddressSanitizer/UndefinedBehaviorSanitizer e `-Wall -Wextra -Werror`. Testes de
loopback Python requereram execução fora da restrição de sockets do sandbox;
PlatformIO precisou escrever locks no cache instalado. Esses são limites do
ambiente, não resultados de bancada.

A suíte cobre os testes anteriores de aquisição/calibração/SH-2/driver, mais:

- Decodificação real Raw/Uncal com pacotes intercalados, status zero, duplicatas,
  Raw timestamp, viés não subtraído, quaternion inválido, epoch e caches separados.
- Ângulos +X/ENU, sinais de inclinação, quadrantes magnéticos, compensação de
  inclinação, invariância de yaw do GRV e singularidades independentes.
- Snapshot completo antes de escrever, intervalos não padrão, confirmação,
  configuração parcial, rollback, STOP idempotente, heartbeat, reset, falha de
  restauração e recuperação explícita.
- Dispatcher TCP/serial real com Wi-Fi/socket simulados: exclusão, envio parcial,
  prioridade de controle, substituição de snapshot não iniciado, backpressure,
  timeout/erro de envio, desconexão/perda de Wi-Fi e comandos anteriores após parar.
- Cliente por sockets reais de loopback: fragmentação/concatenação, schema,
  capability, finitude, retenção, sequências, oversized, ausência de dados,
  metadata único, CSV com lacunas, nenhum arquivo sem save, erros de disco,
  SIGINT/SIGTERM e restauração do terminal.

## Validação física pendente — separada dos testes de software

O usuário gravou o firmware e foram realizados testes TCP com a IMU em 13/11/2026.
Os ensaios geométricos e com ímã continuam pendentes. Antes de considerar o
aceite físico concluído:

- Operar por mais de 60 s em 2 Hz; confirmar painel responsivo, ausência de fila
  crescente, heartbeat e consumo sem starvation. Repetir em 0,2 e 5 Hz.
- Verificar suporte efetivo de **Raw/Uncal** no produto/firmware da placa; guardar
  metadata com intervalos pedidos/confirmados e medir a cadência entregue.
- Trena fixa: aproximar/retirar ímã, observando Raw/Uncal acima do ruído. Não exigir
  que Calibrated elimine a perturbação nem presumir a reação do RV.
- Orientações niveladas e inclinadas conhecidas: verificar +X, quadrantes, sinal de
  inclinação e azimute Uncal. **Resolver e documentar eixos/escalas Raw antes de
  habilitar seu azimute**; não alegar validação dele enquanto isso estiver pendente.
- Comparar configurações antes/depois de STOP, SIGKILL, perda de Wi-Fi e reset
  espontâneo. Confirmar retorno de CAPTURE_IMU, captura completa e calibração,
  sem reset manual, SAVE DCD ou regravação para sair de uma sessão normal.
- Provocar falha transitória de relatório/conexão: verificar retenção independente,
  lacunas no CSV e recuperação real, além das simulações já automatizadas.


### Investigação de RESTORE_FAILED em 13/11/2026

A placa com IMU part 10004563, firmware 3.12.6/build 62, apresentou
`cause=REPORT_DISABLED` na preparação e `CONFIG_MISMATCH` para Raw (22) na
restauração. A confirmação aceitava a primeira resposta de feature, incluindo
estado anterior ainda em trânsito. A correção aguarda a configuração esperada
até o prazo fixo, com consultas Get Feature a cada 200 ms, sem repetir Set Feature.
Testes nativos reproduzem respostas antigas na configuração e na restauração.

Após o upload pelo usuário, a sessão chegou a READY em aproximadamente 0,51 s,
confirmou os cinco intervalos em 20.000 µs e entregou 60 snapshots em cerca de
30 s. STOP retornou `restored=1` e STATUS confirmou IDLE, sem mudança de epoch.
Os contadores I²C ainda aumentaram de 63 para 475 durante esse teste, com perdas
de sequência. A correção resolve a confirmação prematura observada, mas a qualidade
do transporte I²C continua exigindo investigação própria.

O cliente da imagem Docker também executou por 70 s com `--rate-hz 2 --save false`,
mostrando 138 painéis (incluindo o inicial). SIGTERM encerrou com código 0, sem
mensagem de erro. STATUS posterior confirmou `stream_state=IDLE` e intervalos
RV/GRV/Calibrated confirmados em 20.000 µs. Isso valida nesta placa o fluxo normal
através do cliente Docker e sua finalização por sinal.
