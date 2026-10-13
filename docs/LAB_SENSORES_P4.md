# Incremento 3: aquisição unitária pelo Wi-Fi

Guia da captura, preservado após o incremento 4. Para calibração nativa da IMU, veja [LAB_CALIBRACAO_IMU_P4.md](LAB_CALIBRACAO_IMU_P4.md).

O alvo `mm1_p4_lab` oferece `STATUS` e `CAPTURE <request_id> [1]` por TCP e pela serial. Cada pedido avalia uma transação do laser e, em seguida, um relatório novo da IMU, inclusive quando o laser falha. O dispositivo deve permanecer parado até o término. Lotes com `n>1` pertencem ao incremento 6.

Desde o incremento 5, o lab usa SparkFun 1.0.6 e I²C1 de hardware. A montagem atual (13/10/2026) usa **SDA25/SCL34 e NRST49**. O usuário identificou mau contato no I²C e relatou inversão acidental da alimentação; retirou os GPIO30/31/32 instáveis por suspeita de dano e percebeu maior estabilidade após remapear os pinos, com o reset conectado. Ver [integração e relato de bancada](LAB_IMU_SPARKFUN_P4.md); os resultados de captura abaixo são históricos. O relato anterior de calibração do magnetômetro e qualidade RV **3/3** permanece registrado, com estabilidade quantitativa e persistência DCD ainda a verificar.

Atualização do ensaio de 13/10/2026: após SAVE e restauração confirmados, o novo log registra captura `COMPLETE/OK` com RV **3/3**. Ver [captura após SAVE](#captura-após-save-confirmado--13102026).

## Preparar e executar

Mantenha as credenciais em `include/lab_config.h`. Arquivos locais criados no incremento 1 continuam funcionando: os parâmetros novos têm valores padrão em `src/lab/settings.h`. A configuração de fábrica do C6 e a chamada direta `hostedSetPins` foram preservadas.

Pinagem da montagem de laboratório, confirmada pelo usuário:

| Sensor | Conexão no P4 |
| --- | --- |
| Laser | UART1, 9600 8N1; TX do módulo → GPIO21, RX do módulo → GPIO22 |
| BNO086 | **SDA GPIO25, SCL GPIO34**, I²C1 a 100 kHz; NRST GPIO49; endereço 0x4B, com tentativa de 0x4A |

Os GPIO7/8 continuam reservados aos periféricos da placa. Não há varredura ou inversão automática dos pinos da IMU no alvo lab. Display, touch, áudio, SD, BLE e a UI original permanecem fora desse alvo.

A pinagem atual é específica da bancada e está em [`src/board/p4/imu_sparkfun/config.h`](../src/board/p4/imu_sparkfun/config.h). O mapa `mm1_p4_pins.h` e os alvos originais permanecem preservados; eles não incorporam o remapeamento e não correspondem à ligação atual. Após gravar o lab, conferir no log `I2C1 SDA=25 SCL=34 RST=49` e em `STATUS` os mesmos pinos com `imu_transport=hardware_i2c`. Os registros antigos de bit-bang/SDA31/SCL30 abaixo documentam os ensaios anteriores e não são instrução de ligação atual.

Comandos a partir da raiz do repositório:

```sh
pio run -e mm1_p4_lab
pio run -e mm1_p4_lab -t upload
pio device monitor -b 115200
```

Confira o IP atual na serial e reconstrua o cliente Docker:

```sh
docker compose -f docker/compose.yaml build status
docker compose -f docker/compose.yaml run --rm status --host 192.168.0.10
docker compose -f docker/compose.yaml run --rm status --host 192.168.0.10 --capture 1 --log /data/capture.jsonl
```

Substitua o IP pelo mostrado na placa. A rede continua em modo **host**. Se o destino já estiver configurado, pode omitir `--host`. UID/GID e opções de `.env` seguem o [README do Docker](../docker/README.md). O arquivo fica em `docker/data/capture.jsonl` e recebe registros adicionais sem apagar os anteriores.

Sem `--capture`, o cliente apenas consulta `STATUS`. Com `--capture 1`, consulta `STATUS`, envia `CAPTURE 1 1`, espera ACK, cabeçalho CSV, uma linha e `DONE`; então confere IDs, campos e contagens. Saída de processo 0 significa troca de protocolo válida, mesmo se o resultado dos sensores for `PARTIAL` ou `ERROR`. Verifique os campos de validade, erros e qualidade antes de analisar a medição.

O registro JSONL inclui linhas originais, campos interpretados, ID e horários UTC de início/fim da transação no PC, além da duração monotônica. Não é um timestamp físico de cada sensor. Ainda não é o coletor de poses e CSV persistente do incremento 5; o CSV recebido fica preservado dentro do JSONL. Não há sincronização NTP.

## Respostas e dados

O cumprimento agora anuncia `stage=unit_capture`; `META` anuncia `capture=single`, `commands=STATUS,CAPTURE`, `schema=2`, `n_max=1`, `timeout_ms=9000` e as convenções dos dados. O cumprimento mantém duas linhas; o cabeçalho CSV é enviado **por captura**, após o ACK (`csv_header=per_capture`). Reconstrua o cliente: a versão anterior só reconhecia `stage=network_only`. O cliente atualizado reconhece ambas as etapas, mas recusa captura no firmware antigo.

Exemplo ilustrativo, sem reproduzir os cumprimentos:

```text
CAPTURE 1 1
# OK CAPTURE request_id=1 n=1 timeout_ms=9000
request_id,sample_index,distance_m,laser_valid,laser_error,qw,qx,qy,qz,azimuth_deg,inclination_deg,roll_deg,angles_valid,angle_error,accuracy_rad,imu_status,imu_seq,imu_valid,imu_error
1,1,1.234,1,NONE,1,0,0,0,90,0,0,1,NONE,0.025,3,42,1,NONE
# DONE request_id=1 completion=COMPLETE result=OK n_requested=1 n_rows=1 n_valid=1 n_laser_valid=1 n_imu_valid=1 code=NONE state=IDLE
```

- `distance_m`: distância original em metros, sem trim, correção geométrica ou leitura de ajustes da NVS.
- `qw,qx,qy,qz`: componentes originais do mesmo Rotation Vector que produz os ângulos, sem normalização ou mudança de sinal. Valores são serializados com nove algarismos significativos.
- Azimute magnético em [0,360), inclinação e roll em graus: fórmulas da aplicação, com offset de azimute fixo em zero. O eixo do feixe é +X da IMU por padrão; `IMU_LASER_AXIS_BX/BY/BZ` permitem configurar a montagem e são informados em `META`.
- `accuracy_rad`: estimativa de precisão angular do relatório, em ponto flutuante; `imu_status`: qualidade 0–3; `imu_seq`: sequência SH-2 de 8 bits. Qualidade zero permanece visível e não impede a exportação de um relatório estruturalmente válido.
- `imu_valid`: relatório novo com quaternion finito, norma dentro de 0,02 de 1 e precisão finita não negativa. Um quaternion inválido conserva componentes finitos, sequência e qualidade para diagnóstico, com `IMU_BAD_QUAT`; componentes não finitos ficam vazios. Sem relatório, esses campos ficam todos vazios.
- `angles_valid`: os três ângulos estão definidos. Singularidade (`ANGLE_SINGULARITY`, limiar 1e-5 para projeção horizontal/par de roll) preserva quaternion, qualidade e ângulos ainda definidos, deixando vazios somente os indefinidos. Não invalida a IMU. Eixo configurado inválido produz `ANGLE_AXIS_INVALID`.

Não são solicitados relatórios de aceleração no lab. No incremento 4, `CAL_IMU` habilita a calibração nativa e grava DCD mediante ação explícita do operador; fora dessa sessão, somente RV a 50 Hz é solicitado. Não há tare ou zeramento de heading. Durante calibração, CAPTURE responde BUSY. O firmware original continua habilitando os relatórios que já utilizava.

`DONE` conta validades de laser/IMU, independentemente da singularidade dos ângulos. `COMPLETE/ERROR` com uma linha significa que ambos foram avaliados e falharam. Um sensor válido e outro inválido produz `PARTIAL`. Não há tentativas extras para substituir falhas.

IDs devem ser positivos de 32 bits e crescer dentro da conexão; repetição ou retrocesso retorna `DUPLICATE_ID`. O cliente de teste abre uma conexão por execução, por isso pode usar `--capture 1` novamente. `n` diferente de 1 retorna `N_RANGE`; novo pedido durante uma captura retorna `BUSY`. A serial aceita os mesmos comandos, com IDs crescentes até reiniciar o ESP. Há apenas uma aquisição ativa compartilhada entre TCP e serial.

## Novidade, prazos e recuperação

O laser passa por desligamento/drenagem (até 500 ms, exigindo 50 ms sem bytes), mira ligada por 120 ms e um único comando `SINGLE`. O parser aceita a resposta de distância de 13 bytes do caminho documentado no driver existente, função 0x20, comprimento 0x0004, checksum e quatro bytes BCD válidos; zero não é uma distância válida. Não há fallback QUICK/READ_RES nem reaproveitamento de distância anterior. Bytes sem significado documentado não recebem códigos de erro inventados. O layout e os eventuais frames específicos de erro do módulo ainda precisam de confirmação na bancada.

O prazo padrão da resposta do laser é 3200 ms, com teto de 4500 ms para toda a etapa. A IMU continua sendo atendida nesse intervalo. Após o laser, descarta-se o backlog da IMU até uma leitura I²C bem-sucedida com comprimento SHTP zero. Só um callback de Rotation Vector posterior a essa barreira pode atender o pedido. NACK/erro I²C não serve como evidência de fila vazia. Relatórios duplicados não incrementam o contador de novidade no lab. A etapa IMU inteira tem prazo de 1000 ms: ausência de fila vazia gera `IMU_BACKLOG`; ausência de relatório posterior gera `IMU_TIMEOUT`.

Reset da IMU invalida o cache e tenta reabilitar Rotation Vector. Um reset durante a etapa IMU encerra essa tentativa com erro, permitindo avaliar a próxima solicitação. Falha na reabilitação deixa `imu=NOT_READY` até reinicializar o dispositivo. Falhas de barramento ou decodificação são separadas (`IMU_IO_ERROR`, `IMU_DECODE_ERROR`).

O lab usa timeout Wire de 50 ms por transação no I²C de hardware. Leituras recompõem pacotes em blocos de 32 bytes e podem exigir várias transações; o orçamento agregado de 100 ms do bit-bang foi removido. O timeout Wire não equivale a um limite configurado de clock stretching. Uma escrita com erro retorna falha ao SHTP, evitando repetição ilimitada; escritas maiores que 32 bytes são recusadas, sem truncamento. Os comandos utilizados nesta etapa cabem nesse limite. A UART é atendida em blocos limitados, sem `delay`/`flush` no polling. Os tempos efetivos do transporte precisam ser medidos na placa.

O ACK anuncia um teto conservador de 9000 ms; o cliente soma a margem `--timeout` (padrão 5 s) à espera de cada linha da captura. O firmware limita envio pendente a 2 s e conexões sem comandos a 60 s. Fechar a conexão cancela a aquisição TCP e envia desligamento do laser. `STATUS` continua sendo atendido durante a aquisição, sujeito aos bloqueios limitados do transporte da IMU.

**Após timeout, resposta inválida ou cancelamento com uma medição pendente, o laser fica em `RESYNC_REQUIRED`.** As próximas solicitações devolvem `LASER_RESYNC_REQUIRED` e continuam tentando a IMU. A UART é drenada, mas isso não prova que a medição anterior terminou: não há identificador de transação no protocolo conhecido. Nesta etapa a recuperação exige desligar e ligar a alimentação do conjunto, incluindo o módulo laser; reiniciar apenas o ESP não garante que o módulo abandonou o pedido antigo. Não há recuperação automática por simples espera. Validar uma sequência de recuperação do módulo é pendência de bancada para o incremento 5.

Em `STATUS`, `sensors=ENABLED` significa que a camada de aquisição foi compilada. `laser=READY` significa que o transporte está disponível para uma tentativa, não que um sensor desconectado já tenha sido detectado. Confira o resultado da captura. Os contadores de relatórios, resets, erros I²C/decoder e saltos de sequência ajudam no diagnóstico; o contador de saltos não equivale a um número comprovado de amostras perdidas.

## Verificações realizadas e ensaio pendente

Em 29/09/2026, o alvo lab compilou com pioarduino 55.03.312/Arduino 3.3.12/IDF 5.5.5. A imagem Docker foi reconstruída e seus **24 testes** passaram, incluindo captura válida, qualidade baixa, falhas parciais/totais, singularidade, diagnóstico de quaternion inválido, fragmentação, EOF, timeouts, ACK/CSV/DONE inconsistentes e gravação JSONL.

Os [testes C++](../tests/lab/README.md) passaram com AddressSanitizer/UndefinedBehaviorSanitizer: conversão angular, parser, máquina de aquisição, ausência de cache, resposta tardia, prazo, erros e formatação. O driver real da IMU também foi exercitado com GPIO/SH-2 simulados nas configurações lab e original: seleção de relatórios, qualidade fracionária, sequência/duplicatas, reset, leitura vazia versus NACK, pacote de 384 bytes e falha de escrita. Esses testes não validam sinais elétricos nem o firmware dos sensores.

**Primeiro teste físico do incremento 3:** o usuário confirmou captura por Docker/TCP com distância de `1.50300002 m`, `laser_valid=1` e `DONE completion=COMPLETE result=PARTIAL`. Na serial, outra captura retornou `1.48100007 m`. Em ambas a IMU ficou `NOT_READY`, sem relatórios. O boot mostrava `SDA=GPIO30 SCL=GPIO31 idle=1/1`, mas o usuário confirmou a ligação física inversa: **SDA no GPIO31 e SCL no GPIO30**. A seleção foi corrigida somente no lab. Esse teste confirmou o laser e a resposta parcial; o teste seguinte confirmou também a IMU.

### Captura de laser e IMU após a correção da pinagem

O usuário executou, com o destino configurado em `docker/.env`:

```sh
docker compose -f docker/compose.yaml run --rm status --capture 1
```

Saída informada pelo usuário, preservando os valores e a transcrição do protocolo:

```text
# HELLO MM1LAB 2 stage=unit_capture boot_id=7a2e1960de1d5401 connection_id=4
# META capture=single commands=STATUS,CAPTURE schema=2 n_max=1 timeout_ms=9000 imu_report=rotation_vector imu_interval_us=20000 quaternion_order=wxyz laser_axis=1/0/0 azimuth_reference=magnetic azimuth_offset_deg=0 distance_source=laser_report laser_trim_mm=0 quaternion_norm_tolerance=0.02 singularity_epsilon=1e-5 csv_header=per_capture
# OK STATUS stage=unit_capture wifi=CONNECTED ip=192.168.0.10 rssi_dbm=-42 tcp=LISTENING port=5000 hosted=1 sensors=ENABLED attempt=1 disconnect_reason=0 uptime_ms=426117 state=IDLE laser=READY imu=READY imu_reports=19295 imu_resets=1 imu_io_errors=1 imu_decode_errors=2 imu_sequence_gaps=1296 n_max=1
# OK CAPTURE request_id=1 n=1 timeout_ms=9000
request_id,sample_index,distance_m,laser_valid,laser_error,qw,qx,qy,qz,azimuth_deg,inclination_deg,roll_deg,angles_valid,angle_error,accuracy_rad,imu_status,imu_seq,imu_valid,imu_error
1,1,1.40500009,1,NONE,0.999755859,-0.0190429688,-0.0110473633,0.000427246094,89.9269409,1.26479602,-2.18323183,1,NONE,3.14160156,0,16,1,NONE
# DONE request_id=1 completion=COMPLETE result=OK n_requested=1 n_rows=1 n_valid=1 n_laser_valid=1 n_imu_valid=1 code=NONEstate=IDLE
```

Nota de transcrição: a última linha foi colada com `code=NONEstate=IDLE`, sem espaço. O firmware e o cliente atuais usam dois campos separados, `code=NONE state=IDLE`; a forma concatenada seria recusada pelo cliente. O trecho acima mantém o relato original e não comprova uma alteração no formato transmitido.

O resultado confirma a aquisição unitária na placa com os dois sensores: `laser=READY`, `imu=READY`, distância de `1.40500009 m`, quaternion e três ângulos exportados, `laser_valid=1`, `imu_valid=1`, `angles_valid=1` e término informado como `COMPLETE/OK`, com um par válido. O azimute continua magnético sem offset e a distância sem trim.

**Qualidade/calibração pendente por decisão do usuário:** a amostra preservou `accuracy_rad=3.14160156` e `imu_status=0`. O status indica orientação não confiável; `imu_valid=1` e `result=OK` confirmam os critérios de aquisição/estrutura do relatório, não boa calibração nem precisão angular comprovada. A suspeita de calibração inadequada foi registrada, mas sua causa não foi diagnosticada neste teste. Esse registro é histórico. A calibração nativa foi antecipada e implementada no incremento 4, com sucesso parcial de bancada relatado após o incremento 5: magnetômetro calibrado e Rotation Vector em 3/3. O aceite completo e a persistência DCD continuam pendentes; veja o guia dedicado.

O `STATUS` anterior ao pedido registrou 19.295 relatórios, 1 reset, 1 erro I²C, 2 erros de decodificação e 1.296 descontinuidades de sequência. São contadores acumulados, não erros atribuídos à amostra apresentada. Foram preservados para investigação posterior; o teste não determina suas causas nem permite converter as descontinuidades diretamente em número de amostras perdidas.

### Captura após SAVE confirmado — 13/10/2026

O último registro de `docker/data/status.jsonl` (linha 11 nesta análise) documenta `status --capture 1` após a [calibração salva e restaurada](LAB_CALIBRACAO_IMU_P4.md#ensaio-com-save-confirmado--13102026). O cliente iniciou às **06:09:12.699 UTC** e terminou às **06:09:13.521 UTC**, com `ok=true` e duração registrada de **821,666 ms** para a execução do cliente, incluindo consulta e captura.

```text
request_id,sample_index,distance_m,laser_valid,laser_error,qw,qx,qy,qz,azimuth_deg,inclination_deg,roll_deg,angles_valid,angle_error,accuracy_rad,imu_status,imu_seq,imu_valid,imu_error
1,1,0.844000041,1,NONE,0.341247559,0.0376586914,-0.0637207031,0.937072754,309.708344,6.54941845,-5.41308594,1,NONE,0.0927734375,3,55,1,NONE
# DONE request_id=1 completion=COMPLETE result=OK n_requested=1 n_rows=1 n_valid=1 n_laser_valid=1 n_imu_valid=1 code=NONE state=IDLE
```

A amostra tem **qualidade RV 3/3**, `laser_valid=1`, `imu_valid=1` e `angles_valid=1`, sem erro indicado na linha. A distância é **0.844000041 m**; o azimute magnético é **309.708344°**, inclinação **6.54941845°** e roll **−5.41308594°**. `accuracy_rad=0.0927734375` é a estimativa informada pela IMU, não um erro medido contra referência externa. Os metadados mantêm offset de azimute e trim de laser iguais a zero.

O STATUS anterior à captura registrou `laser=READY imu=READY calibration=IDLE calibration_blocked=0`, 9.350 relatórios, 1 reset, 139 erros de I/O, 0 erros de decodificação e 1 gap de sequência. Esses contadores já tinham os mesmos valores de erro/reset no fim da calibração e no STATUS intermediário às 06:07:52. Não há STATUS posterior à captura nesse registro para calcular a variação durante a própria aquisição.

A inicialização reportada foi `imu_init=READY imu_init_rc=0`, duração 211 ms e ACK de 0x4B na primeira sondagem, 150 ms após liberar NRST. O transporte é I²C1, SDA25/SCL34/NRST49, 100 kHz, SparkFun `fd0149c+mm1.4`; o sensor reportou firmware `3.12.6`, build `62`. A captura e a calibração usam o mesmo `boot_id=e3268535897ff631`, confirmando funcionamento após SAVE no mesmo boot. Persistência após desligar/ligar, repetibilidade e precisão contra referência continuam como ensaios separados.

### Ensaios ainda pendentes

O teste real de `STATUS` dos incrementos 1/2 está preservado em [LAB_WIFI_P4.md](LAB_WIFI_P4.md#resultado-na-placa-real). Para concluir o aceite físico do incremento 3:

1. A gravação, `STATUS` e captura dos dois sensores já foram confirmados. Ampliar o ensaio para diferentes orientações e repetições, mantendo o dispositivo parado durante cada pedido. Avaliar calibração/precisão posteriormente, conforme decisão do usuário.
2. Conferir a distância e o formato real da resposta do laser, inclusive erros do módulo. Verificar que o feixe apaga ao terminar e ao cancelar/desconectar.
3. Recalcular os ângulos com os quaternions recebidos e o eixo informado; observar qualidade fracionária, sequência e singularidades. Conferir que offsets/trim não nulos previamente salvos na UI não afetam os dados lab.
4. Exercitar sensor ausente, alvo sem retorno, reset da IMU, resposta tardia e perda de Wi-Fi. Confirmar que o outro sensor continua sendo avaliado e que uma captura antiga não preenche a seguinte. Após `RESYNC_REQUIRED`, desligar e ligar também o laser.
5. Medir duração e responsividade de `STATUS`, especialmente sob falha I²C. Esses resultados orientarão a recuperação e os lotes do incremento 5.
