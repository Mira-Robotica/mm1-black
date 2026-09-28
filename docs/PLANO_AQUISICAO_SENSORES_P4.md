# Plano incremental: validação dos sensores da trena por aquisição Wi-Fi sob demanda

Status: incremento 1 implementado e compilado; validação em hardware pendente. Incrementos 2–5 permanecem planejados. Ver [instruções de Wi-Fi e ping](LAB_WIFI_P4.md). Base de código examinada: commit `684c16a` e arquivos locais. Revisão de escopo em 26/09/2026: aquisição em posições discretas do manipulador, por solicitação do Python, com timestamps no PC. Este plano substitui a proposta de streaming contínuo e sincronização temporal no ESP. O lote executa um número fixo de tentativas, incluindo falhas, e exporta quaternion junto dos ângulos e da qualidade.

## 1. Objetivo e sequência do ensaio

Criar um firmware mínimo de bancada para validar os sensores da trena no hardware P4, com Wi-Fi em modo station e servidor TCP. O Python posiciona o manipulador, espera a estabilização, solicita leituras da trena e aguarda a resposta completa antes de mover para a próxima pose. O ESP não controla o robô.

Sequência de uma posição do ensaio:

1. O Python atribui um `pose_id` e comanda uma posição/orientação predefinida.
2. Confirma pela interface do robô que o movimento terminou e que a pose **medida**, não apenas a comandada, está dentro das tolerâncias de posição, orientação e velocidade do ensaio.
3. Aguarda um tempo de acomodação configurável para vibração e estabilização da fusão da IMU. Esse tempo será definido em bancada; não presumir que o ACK de movimento seja confirmação de repouso.
4. Registra a pose real do robô e o horário do PC, envia `CAPTURE <request_id> [n]` e mantém o robô na mesma pose até receber o término.
5. Recebe as leituras individuais, confere resultado e quantidade, registra novamente a pose real e o horário do PC, salva tudo associado àquela posição e só então segue para a próxima.

O jitter do laser passa a afetar a **duração da permanência em cada pose**, sem exigir sincronização fina entre laser, IMU e robô enquanto a pose permanece estável. A validação será estática/quase estática: ela não caracteriza atraso ou desempenho da IMU durante movimento contínuo.

O firmware terá inicialização, rede, polling, aquisição por pedido e transmissão. Display, LVGL, touch, SD, BLE/SAP6, interface web, OTA, áudio, bateria, botões, geometria de levantamento e persistência de pontos ficam fora do executável. Também ficam fora NTP/SNTP, âncoras UTC, compensação de deriva, timestamps de aquisição do ESP e alinhamento temporal pelo SH-2. Timers locais comuns continuam sendo usados para timeouts e intervalos.

SSID, senha, porta, tempos de espera e número de repetições são parâmetros. No incremento 1, configurar as credenciais em `include/lab_config.h`, ignorado pelo Git, antes de gravar o teste de rede.

## 2. O que o código atual realmente usa

### Hardware e compilação

| Item | Evidência no repositório | Consequência para o teste |
| --- | --- | --- |
| Hardware antigo | `env:denky32`, ESP32 CYD, versões v0.x | Não é o alvo deste trabalho. |
| Hardware recente | `env:mm1_p4`, Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 | Usar a definição `boards/mm1_p4.json`. |
| CPU/revisão | A definição atual usa `esp32p4_es` e 360 MHz; o documento de portabilidade identifica ECO2 | Preservar esses valores até verificar a revisão física; não mudar para 400 MHz apenas por ser a frequência nominal de outra revisão. |
| Rádio | ESP32-C6, ESP-Hosted por SDIO | P4 não possui Wi-Fi integrado. O firmware do C6 precisa ser compatível com o host. |
| IMU | BNO086 segundo os comentários do driver, família BNO08x/SH-2 | Confirmar identificação do módulo por `sh2_getProdIds()` e registrar no ensaio. |
| Barramento da IMU | I²C por software, SDA 30/SCL 31, endereço preferencial `0x4B` e alternativa `0x4A` | Preservar o transporte existente inicialmente; INT e RST não estão conectados. |
| Laser | UART1, RX 21/TX 22, `9600`, `SERIAL_8N1`, RX com buffer de 1024 bytes | Não compartilhar com `Serial`, que fica para diagnóstico USB/UART. |

Referências locais: [PlatformIO](../platformio.ini), [placa](../boards/mm1_p4.json), [pinos](../src/board/p4/mm1_p4_pins.h), [portabilidade P4](ESP32_P4_PORT.md).

Há comentários históricos conflitantes sobre os pinos SDIO. O caminho de `sap6_ble_begin()` usa **`hostedSetPins(18, 19, 14, 15, 16, 17, 54)`**, na ordem CLK, CMD, D0, D1, D2, D3 e RESET. A configuração foi extraída para `p4_hosted_configure()` em `src/board/p4/p4_hosted.cpp`, compartilhada pela aplicação original e pelo teste de rede, antes de iniciar Wi-Fi. Não transportar a checagem de prontidão baseada em MAC BLE para o teste sem Bluetooth; validar diretamente a inicialização Hosted e a conexão station.

O alvo original `mm1_p4` mantém sua configuração. O novo `mm1_p4_lab` usa pioarduino **55.03.312** fixado, Arduino **3.3.12** e IDF **5.5.5**, confirmados na compilação do incremento 1. A variante `esp32p4_es` e a CPU a 360 MHz foram preservadas.

### IMU: funções, relatórios e representação

Fluxo atual em [main.cpp](../src/main.cpp) e [p4_imu.cpp](../src/board/p4/p4_imu.cpp):

1. `sensor_init()` chama `p4_imu_begin(IMU_ADDR)`.
2. `imu_try_open()` configura a HAL I²C, chama `sh2_open()`, `sh2_getProdIds()`, `sh2_setSensorCallback(sensor_handler, ...)` e habilita os relatórios por `sh2_setSensorConfig()`.
3. `loop()` chama `poll_imu()`, que chama `p4_imu_poll()`.
4. `p4_imu_poll()` chama `sh2_service()`. A entrega a `sensor_handler()` ocorre durante esse atendimento por polling; não depende de uma interrupção da IMU.
5. `sensor_handler()` usa `sh2_decodeSensorEvent()` e guarda os últimos valores. `p4_imu_get_quat()` e `p4_imu_get_accel()` retornam esse cache.

| Relatório atual | ID | Intervalo solicitado | Dados disponíveis |
| --- | --- | --- | --- |
| `SH2_ROTATION_VECTOR` | `0x05` | 20.000 µs, nominalmente 50 Hz | Quaternion `real/i/j/k`, usado internamente para calcular os ângulos, e estimativa de precisão angular em radianos. |
| `SH2_ACCELEROMETER` | `0x01` | 50.000 µs, nominalmente 20 Hz | Aceleração calibrada `x/y/z`, em m/s², incluindo gravidade. |

O código trabalha com `float` após decodificar os campos do protocolo. No Rotation Vector, o decoder converte componentes de formato fixo Q14 e precisão angular Q12. Não são dados brutos de ADC. A aplicação deriva Euler, azimute e inclinação do feixe em `imu_update_angles_from_quat()` **somente a partir do quaternion**.

**Para que serve a aceleração hoje?** `poll_imu()` calcula `imu_grav_mag = sqrt(ax² + ay² + az²)`. A tela de configuração usa essa norma para mostrar `Consistency: OK/check (g=...)`, considerando OK a faixa de 8,5 a 11,0 m/s²; quando offline, o rótulo aparece como `Level`. Isso é uma checagem grosseira da magnitude da aceleração, que inclui gravidade e movimento, não uma entrada do cálculo de orientação nem uma medida de precisão angular. Não há outra utilização de `imu_grav_mag` no código examinado.

**Rotation Vector é a orientação fundida de acelerômetro, giroscópio e magnetômetro**, referenciada à gravidade e ao norte magnético. A fusão acontece dentro do BNO: deixar de solicitar o relatório separado `SH2_ACCELEROMETER` ao host não desliga o acelerômetro necessário à fusão. No teste, habilitar somente `SH2_ROTATION_VECTOR`, inicialmente a 50 Hz, tanto na abertura quanto após reset. Game Rotation Vector e outros relatórios ficam fora do escopo. Fonte: [datasheet BNO08x local](datasheets/BNO080_085-Datasheet_v1.16.pdf), seções 2.1.1 e 2.2; [documentação do fabricante](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf).

### Orientação exportada: ângulos e quaternion

Reaproveitar as fórmulas de `imu_update_angles_from_quat()` e `imu_vec_body_to_world()` em uma função independente de UI, executada para **cada Rotation Vector novo selecionado para uma repetição**. Exportar também o quaternion do **mesmo relatório** usado nessa conversão, para auditoria do cálculo e comparação direta de rotações no Python. A tela SENSOR em `refresh_sensor_display()` apresenta:

| Coluna proposta | Valor existente | Convenção a preservar |
| --- | --- | --- |
| `azimuth_deg` | `imu_azimuth_deg` | Azimute do eixo do laser, normalizado em [0, 360), com o offset angular configurado. |
| `inclination_deg` | `imu_inclination_deg` | Inclinação do eixo do laser, de −90° a +90°, positiva acima da horizontal. |
| `roll_deg` | `imu_roll` | Roll de Euler calculado pela fórmula atual, aproximadamente de −180° a +180°. |

Acrescentar `qw,qx,qy,qz`, respectivamente `rotationVector.real/i/j/k`, na ordem explícita **w,x,y,z**, com componentes adimensionais. Preservar os valores decodificados, sem offset de azimute, rotação de montagem adicional, troca de sinal ou normalização silenciosa na exportação. São dados de orientação fundida, não dados brutos de ADC. Usar precisão decimal suficiente para recuperar o `float` (por exemplo, 9 algarismos significativos). Registrar nos metadados a ordem e a convenção corpo → mundo usada por `imu_vec_body_to_world()`.

O Python poderá reproduzir as fórmulas com esses valores e os mesmos parâmetros para conferir os ângulos do firmware. Para comparar rotações, verificar a norma original e normalizar apenas uma cópia de um quaternion finito e não nulo, preservando o registro original. Normas incompatíveis com uma rotação unitária devem ser sinalizadas, não corrigidas silenciosamente para esconder erro. Bibliotecas podem adotar ordem diferente e normalização automática; no SciPy, a ordem w,x,y,z exige `scalar_first=True`: [documentação de quaternions](https://docs.scipy.org/doc/scipy/reference/generated/scipy.spatial.transform.Rotation.from_quat.html).

Azimute e inclinação do feixe não devem ser substituídos diretamente pelos valores internos `imu_yaw` e `imu_pitch`. A aplicação normaliza o vetor `IMU_LASER_AXIS_BX/BY/BZ`, gira esse vetor pelo quaternion e obtém `(wx, wy, wz)` no referencial usado pelo código (+X leste, +Y norte, +Z para cima). Então calcula `azimuth = norm_deg360(atan2(wx, wy) × 180/π + offset)` e `inclination = atan2(wz, sqrt(wx² + wy²)) × 180/π`. Manter também a fórmula atual de roll, sem reinterpretá-lo como rotação em torno de um eixo de laser arbitrário.

O eixo de montagem e `azimuth_offset_deg` serão parâmetros explícitos do teste, registrados nos metadados e fixos durante o ensaio. Para reproduzir os números da aplicação, usar os mesmos valores efetivos, incluindo eventual ajuste que hoje está salvo na NVS; copiar esse valor para a configuração do teste, sem carregar toda a persistência/UI. O padrão no código é −21,7°, mas ele não comprova a declinação atual do local. Offset zero permite ensaio referido ao norte magnético, devendo ser identificado como tal. Não aplicar tare automaticamente.

Exportar os ângulos com casas decimais suficientes para análise, sem limitar à única casa exibida na tela. Nas comparações, tratar a passagem 359° → 0° como continuidade circular. Perto do feixe vertical, o azimute fica indefinido/sensível; nas singularidades de Euler, roll também exige cuidado. Essas limitações geométricas não são corrigidas por um status alto da IMU.

Correções mínimas necessárias para que o teste represente os dados recebidos:

- Preservar `rotationVector.accuracy` como `float`. Hoje há conversão para `int`, e a UI trata esse inteiro como qualidade 0–3. São grandezas diferentes: a qualidade está em `sh2_SensorValue_t.status`, enquanto `accuracy` é uma estimativa angular em radianos.
- Exportar `accuracy_rad` e `status` do mesmo Rotation Vector usado nos ângulos: status 0 = não confiável, 1 = precisão baixa, 2 = média, 3 = alta. A estimativa angular não é uma garantia de erro de cada ângulo. Manter validade, sequência, perdas e eventos de reset/calibração; um relatório decodificado pode resultar em `imu_valid=1` e `imu_status=0` no CSV. A checagem de norma `Consistency` deixa de fazer parte do teste ao retirar o relatório de aceleração.
- Expor novidade por callback/contador e preservar `sequence` e `status`. Os getters atuais podem retornar repetidamente o mesmo cache; uma repetição do ensaio deve consumir um relatório novo. Não exportar timestamps SH-2 nem reconstruí-los para sincronização.
- Continuar atendendo a IMU mesmo sem pedido, com Rotation Vector a 50 Hz e sem batching intencional, para manter a fusão e evitar acúmulo. Relatórios não selecionados para o pedido são descartados deliberadamente; não há requisito de transmitir todos os 50 relatórios/s.
- Ao detectar `SH2_RESET`, invalidar o cache, registrar a ocorrência e verificar o sucesso de `p4_imu_enable_reports()`. Hoje `p4_imu_poll()` consome o sinal de reset e não verifica esse retorno; não depender somente de `p4_imu_was_reset()` no chamador.
- Preservar inicialmente a configuração de calibração do sensor; registrar reinicializações, status e condições de calibração. Aplicar aos ângulos somente o eixo e o offset explicitamente registrados, conforme a convenção acima.

### Laser: extrair o polling existente

O driver está embutido em `src/main.cpp`, não em uma biblioteca independente. O fluxo reaproveitável é:

| Função | Papel |
| --- | --- |
| `lzr_uart_begin()` | Inicializa porta, pinos, baud e buffer. |
| `lzr_loop_tick(now)` | Recebe bytes, avança o polling e trata recuperação/validade. |
| `lzr_process_incoming()` / `lzr_feed_byte()` | Consomem UART e montam frames incrementalmente. |
| `lzr_try_decode13()` / `lzr_decode_frame13()` | Validam frames de 13 bytes e checksum; extraem distância BCD e convertem para metros. |
| `lzr_poll_tick()` / `lzr_poll_send_measure()` | Disparam a medição e aguardam resposta por máquina de estados. |
| `lzr_poll_fallback()` / `lzr_recover()` | Fazem tentativas alternativas e recuperação. |
| `lzr_shutdown_beam()` | Encerra a emissão e o estado de captura; extrair sem os estados de botão/UI. |

O padrão atual é `LZR_CONTINUOUS=0`: comando de ligar o laser, `CMD_SINGLE`, espera da resposta e fallback `CMD_QUICK`/`CMD_READ_RES`. Isso é compatível com a aquisição por **polling** pedida, mesmo que o sensor tenha processamento interno autônomo.

Valores atuais: intervalo base de 350 ms; intervalo na tela SENSOR de 1000 ms após a resposta; timeout de 3200 ms; validade de 4000 ms; recuperação espaçada em pelo menos 6000 ms; até 256 bytes consumidos por atendimento. Esses intervalos não garantem 1 Hz ou qualquer outra taxa exata, pois somam tempo de medição e atendimento.

Extrair o parser, comandos e máquina de estados para `src/lab/laser_poll.*`. Trocar condições `ui_is_lzr_live_page()`, estados de botão e ajustes por tela pelo estado do pedido. Disparar uma medição nova somente quando solicitada por `CAPTURE`; não deixar o laser medindo periodicamente em idle. Entre repetições de um lote, usar inicialmente 1000 ms de intervalo conservador, configurável e a validar com o módulo.

Não usar `lzr_measure_once_blocking()` ou `lzr_sync_for_capture()` no caminho normal. Mesmo a rotina periódica atual contém `delay(25)`, esperas de recuperação e `flush()` de UART. Converter as esperas maiores em prazos da máquina de estados, preservando ordem e tempos mínimos dos comandos. Medir o bloqueio residual: 9 bytes a 9600 baud/8N1 já exigem aproximadamente 9,4 ms no fio. O I²C por software também tem espera de clock stretching de até 50 ms por tentativa; medir e limitar o orçamento total de atendimento em falhas.

O parser tem uma aceitação alternativa mais permissiva em `lzr_try_decode13()`. Comparar frames reais, checksum, campos de erro e protocolo do módulo antes de tratá-los como distância válida. Não transportar silenciosamente um erro de sensor como zero. Em recuperação, `lzr_last_valid_ms` hoje é atualizado sem uma nova medição: não usá-lo como evidência de novidade. Só uma resposta válida de uma transação nova pode fornecer distância para o pedido. Fallbacks como `CMD_READ_RES` só podem ser aproveitados após confirmar que não retornam uma medição anterior; caso contrário, finalizar somente a tentativa desse sensor com erro explícito e continuar o lote.

## 3. Decisão: n tentativas por lote, incluindo resultados inválidos

**Adotar `CAPTURE <request_id> [n]`: avaliar os dois sensores em cada uma das n tentativas e devolver uma linha por tentativa, tenha ela sucesso ou falha.** `n` é o número de tentativas, não uma meta de resultados bons. Uma falha comum de sensor não encerra o lote e não gera uma tentativa extra para substituí-la. Omitir `n` equivale a `n=1`. Aceitar inicialmente de 1 a 20 repetições, com memória e prazo limitados.

Para o script de validação, começar com **`n=5` por pose**, como escolha inicial de engenharia a ajustar em um ensaio piloto. Usar `n=1` para depuração e varreduras rápidas. Cinco repetições permitem observar dispersão inicial, mas não estabelecem por si só a suficiência estatística do ensaio. O custo cresce principalmente com as cinco medições sucessivas do laser.

O firmware devolve **cada leitura individual**, incluindo qualidade e falhas; não calcula nem devolve apenas uma média. Isso permite avaliar no Python distância média/mediana, dispersão angular, valores discrepantes e variações de qualidade. Uma única leitura não permite estimar a dispersão naquela pose; a média de várias leituras pode reduzir ruído aleatório, mas não elimina viés, erro de montagem ou perturbação magnética. Não presumir independência entre leituras sucessivas da fusão nem melhoria automática de precisão por um fator fixo. A distinção entre repetibilidade e contribuição da referência também deve ser mantida na análise: [NIST, componentes de incerteza](https://www.itl.nist.gov/div898/handbook/mpc/section5/mpc56.htm).

O lote é somente a repetição limitada da operação unitária. Implementar e verificar primeiro `n=1`; depois adicionar o contador de repetições. Não adicionar nesta versão contagens independentes de laser/IMU ou captura contínua em segundo plano para o cliente.

### Como obter uma repetição

1. Atribuir `sample_index` de 1 a `n` e iniciar uma transação nova do laser, sem reutilizar distância em cache. Se estiver indisponível ou em recuperação, fazer a verificação/recuperação limitada prevista para essa tentativa; se não puder iniciar uma medição, registrar `LASER_NOT_READY` ou `LASER_RECOVERING`.
2. Enquanto aguarda resposta, continuar atendendo IMU, rede e prazos. Validar frame, checksum e erro do laser. Registrar sucesso, timeout ou erro dessa tentativa, sem apagar o resultado quando uma tentativa posterior tiver sucesso.
3. Independentemente do resultado do laser, tentar obter um Rotation Vector novo em um callback posterior ao início da etapa de IMU, com contador maior que o registrado na entrada. Não copiar o último valor de um getter. Descartar relatórios pendentes anteriores à etapa e conferir ausência de acúmulo. Sem novidade garantida, registrar erro de IMU nessa linha e recuperar para as próximas tentativas, dentro dos prazos.
4. Preservar `qw/qx/qy/qz`, calcular azimute, inclinação e roll a partir desse mesmo relatório e manter seu `accuracy_rad`, `status` e `sequence`. Guardar validades e erros separados por sensor; a falha de um não invalida o dado do outro.
5. Finalizar a linha e avançar para `sample_index + 1` **mesmo que um ou ambos os sensores falhem**, até completar `n`. Não repetir um índice nem acrescentar tentativas para compensar resultados ruins.

As leituras representam a **mesma pose mantida**, não o mesmo instante físico. A taxa interna de 50 Hz permanece útil à fusão, mas não determina a taxa de pares exportados. A recuperação de backlog, reset ou timeout é limitada e não usa timestamps precisos. Uma resposta tardia do laser deve ser descartada antes de iniciar outra transação; enquanto não houver certeza de novidade, marcar o laser como indisponível nas tentativas seguintes e continuar avaliando a IMU.

Distinguir `TIMEOUT`/erro de uma medição efetivamente iniciada de `NOT_READY`/`RECOVERING`, quando não foi possível iniciar uma nova medição. Ambas produzem linhas inválidas para o sensor afetado, mas **n linhas de diagnóstico não comprovam n medições físicas** se o dispositivo permaneceu indisponível. Não inventar valores nem repetir cache para preencher essas linhas.

`imu_valid=1` significa orientação nova, decodificada, com quaternion finito, não nulo e norma dentro da tolerância documentada na implementação. `imu_status` informa separadamente qualidade 0–3: não esperar status alto nem esconder baixa qualidade. Se um quaternion decodificado falhar na validação, preservar seus componentes finitos para diagnóstico com `imu_valid=0` e `IMU_BAD_QUAT`; componentes não finitos ficam vazios. Sem relatório, quaternion, sequência e qualidade ficam vazios, nunca preenchidos com quaternion identidade.

`angles_valid` e `angle_error` indicam separadamente a validade geométrica/conversão dos ângulos. Em azimute indefinido com feixe vertical ou singularidade de Euler, preservar quaternion e qualidade válidos, marcar `angles_valid=0`, informar `ANGLE_SINGULARITY` e deixar vazios somente os ângulos indefinidos. A singularidade dos ângulos não deve invalidar a orientação em quaternion. Sem quaternion válido, deixar os ângulos vazios e usar `angles_valid=0` e `angle_error=IMU_INVALID`.

Interromper antes de `n` somente se o pedido não puder continuar como operação: perda de conexão, reinicialização do ESP, falha interna que impeça o atendimento ou estouro do prazo global. **Timeout ou reset isolado de sensor não é motivo para abortar o lote.** Interrupções deixam explícitos os índices realmente avaliados; não fabricar linhas para índices nunca iniciados. Os prazos devem comportar o pior caso limitado de todas as `n` tentativas, inclusive falhas.

O robô deve permanecer parado **durante todo o lote**, inclusive intervalos e tentativas internas permitidas pelo protocolo do laser. Se a estabilidade for perdida, o Python marca o lote como inadequado para comparação estática e decide explicitamente se repete a pose.

## 4. Firmware mínimo e limites de espera

| Arquivo futuro | Responsabilidade |
| --- | --- |
| `src/lab/main.cpp` | `setup()` e `loop()` de laboratório; fonte original `src/main.cpp` preservada e excluída do alvo lab. |
| `src/lab/network.*` e `src/board/p4/p4_hosted.*` | Incremento 1: Wi-Fi station, pinos SDIO, reconexão e diagnóstico serial/TCP. |
| `src/lab/laser_poll.*` | Parser e máquina de estados para uma transação do laser sob demanda. |
| `src/board/p4/p4_imu.*` e `bno08x/*` | Transporte existente, Rotation Vector, novidade, qualidade e reset. |
| `src/lab/orientation.*` | Conversão para os ângulos atuais, independente de UI. |
| `src/lab/capture_service.*` | TCP, pedido ativo, contador de repetições, buffer limitado e respostas. |
| `include/lab_config.example.h` e configuração local ignorada pelo Git | Wi-Fi, porta, limites e convenções angulares. |
| `tools/capture_sensors.py` | Cliente reutilizável pelo script do manipulador, gravação e análise no PC. |

Inicialização: Serial/GPIOs mínimos → ESP-Hosted → Wi-Fi station → sensores → servidor TCP. O servidor aceita `CAPTURE` também com um ou ambos os sensores indisponíveis, desde que o serviço consiga atender o pedido e seus prazos. A indisponibilidade é registrada por tentativa; assim é possível caracterizar um lote com todas as leituras inválidas. Não há dependência de internet, servidor de horário ou data válida no ESP. Não chamar `p4_board_init()`, que carrega display/touch e SD; manter o backlight desabilitado.

Loop: atender TCP → atender IMU e laser → avançar pedido/prazos → transmitir um trecho limitado da resposta → espera curta fixa, inicialmente 5 ms. Não haverá comando de ajuste de delay pelo cliente. A espera do Python pela resposta não significa bloquear o firmware em uma função longa: preservar atendimento da IMU, da rede e do watchdog.

Usar estados do serviço `IDLE`, `BUSY`, `RECOVERING` e `FAULT`, separados da prontidão de cada sensor. Só um pedido fica ativo; `BUSY` inclui todas as tentativas e o envio. Timeout/reset de sensor mantém o serviço em `BUSY` e dispara recuperação limitada apenas desse sensor; os próximos índices continuam. Desligar o laser ao terminar, falhar sua transação ou perder a conexão. Não disparar medição nova enquanto uma resposta antiga puder ser confundida com ela: registrar indisponibilidade do laser e continuar a IMU. Reservar `RECOVERING`/`FAULT` do serviço para limpeza após interrupção ou falha que impeça atender um pedido com limites; falha isolada de sensor não bloqueia novos lotes de diagnóstico.

Propostas iniciais, ajustáveis após ensaio com o módulo:

| Parâmetro | Valor inicial e função |
| --- | --- |
| Timeout do laser | 3200 ms, partindo do valor existente; abranger a tentativa atual, sem retentativas ilimitadas. |
| Espera por IMU nova | 1000 ms; falhar se não chegar relatório utilizável. |
| Prazo de uma repetição | 6000 ms, incluindo recuperação limitada, envio, leitura e processamento dos dois sensores; reservar o prazo da IMU mesmo se o laser falhar. |
| Intervalo entre repetições | 1000 ms, conservador e independente da espera curta do loop. |
| Prazo anunciado do pedido | `n × (6000 + 1000) + 2000 ms`, limite conservador incluindo margem de resposta. |
| Timeout do cliente | Prazo anunciado + margem de rede configurável, inicialmente 5 s. |

Para `n=5`, o limite inicial do pedido é 37 s; é um teto de espera, não a duração nominal. O firmware só conclui normalmente após avaliar todos os índices, mesmo com falhas. Recuperação que exceda o orçamento disponível deixa o sensor indisponível naquela tentativa, sem estender silenciosamente o prazo nem impedir a avaliação do outro sensor. O prazo global é proteção contra travamento, não uma condição normalmente atingida por falhas previstas dos sensores. Esses prazos exigem medir e limitar os bloqueios do I²C/UART durante a implementação.

Usar um timer monotônico local somente para esses prazos (por exemplo, `esp_timer_get_time()` convertido em ms); não convertê-lo em UTC nem incluí-lo como timestamp de amostra. Manter a HAL SH-2 existente para operação da biblioteca, sem estender seu mecanismo temporal para o protocolo do teste.

Criar `mm1_p4_lab` com lista positiva de fontes e dependências. Excluir da compilação UI, BLE, SD e demais módulos, não somente suas chamadas. Preservar placa, transporte e revisão P4; fixar pioarduino após uma compilação confirmada. A entrada isolada `src/lab/main.cpp` preserva a aplicação original; os filtros de `mm1_p4` e `denky32` excluem `lab/`. Não introduzir `src/lab/timebase.*`.

## 5. Protocolo TCP de solicitação e resposta

Servidor no P4, cliente no PC, porta configurável (padrão 5000), um cliente por vez. Protocolo ASCII por linhas terminadas em `\n`, aceitando `\r\n`. Esta revisão mantém a proposta ainda não implementada de **protocolo/schema 2**, com o cabeçalho atualizado abaixo; incompatível com o streaming proposto anteriormente.

| Comando | Comportamento |
| --- | --- |
| `STATUS` | Informa estado, sensores, pedido ativo/último resultado e parâmetros de timeout; disponível também durante coleta. |
| `CAPTURE <request_id> [n]` | Em `IDLE`, aceita um pedido de 1 a 20 tentativas; padrão 1, inclusive com sensor indisponível. Responde ACK, depois dados e marcador de término. Em outro estado, responde erro sem enfileirar trabalho. |

O Python usa `request_id` inteiro positivo de 32 bits, estritamente crescente dentro da conexão. O ESP rejeita IDs já aceitos ou anteriores (`DUPLICATE_ID`); não refaz medições automaticamente. A combinação `boot_id`, `connection_id` e `request_id` identifica o pedido no log. Reinício do ESP muda `boot_id`; nova conexão muda `connection_id`. A associação a `pose_id` fica no Python, sem o ESP precisar conhecer o robô.

Ao conectar, emitir `# HELLO`, metadados `# META` (incluindo limites de espera) e um cabeçalho CSV uma única vez. Um pedido aceito recebe `# OK CAPTURE` com ID, `n` e `timeout_ms`. Guardar no máximo 20 registros do lote e devolvê-los ao concluir todas as tentativas ou sofrer uma interrupção global, seguidos por **`# DONE`** com o mesmo ID. O Python espera `DONE`, não apenas o ACK ou a primeira linha.

Exemplo ilustrativo com três tentativas: a primeira falha no laser, mas a IMU responde; as outras duas têm sucesso. Para simplificar os valores, o exemplo usa eixo do laser +X e offset de azimute zero. Os quaternions identidade e seu negativo representam a mesma orientação:

```text
# HELLO MM1LAB 2 boot_id=b1 connection_id=1 state=IDLE
# META schema=2 imu_report=rotation_vector imu_interval_us=20000 n_max=20 quaternion_order=wxyz laser_axis=1/0/0 azimuth_offset_deg=0
request_id,sample_index,distance_m,laser_valid,laser_error,qw,qx,qy,qz,azimuth_deg,inclination_deg,roll_deg,angles_valid,angle_error,accuracy_rad,imu_status,imu_seq,imu_valid,imu_error
CAPTURE 15 3
# OK CAPTURE request_id=15 n=3 timeout_ms=23000
15,1,,0,LASER_TIMEOUT,1,0,0,0,90,0,0,1,NONE,0.025,3,42,1,NONE
15,2,1.234,1,NONE,1,0,0,0,90,0,0,1,NONE,0.025,3,118,1,NONE
15,3,1.235,1,NONE,-1,0,0,0,90,0,0,1,NONE,0.026,3,194,1,NONE
# DONE request_id=15 completion=COMPLETE result=PARTIAL n_requested=3 n_rows=3 n_valid=2 n_laser_valid=2 n_imu_valid=3 code=NONE state=IDLE
```

`sample_index` começa em 1 e não se repete. Cada linha corresponde a uma tentativa, inclusive se os dois sensores falharam. As sequências SH-2 podem ter saltos porque a IMU continua sendo atendida entre tentativas e dão a volta em 8 bits; isso, isoladamente, não comprova perda no transporte.

Separar **conclusão do lote** de **validade dos dados** em `DONE`:

- `completion=COMPLETE`: todas as `n` tentativas foram avaliadas e suas `n` linhas devolvidas, inclusive inválidas.
- `completion=INTERRUPTED`: o serviço precisou interromper; devolver as linhas disponíveis e informar o motivo global em `code`. Finalizar a linha em andamento se isso ainda for possível, preservando os sensores que já responderam e marcando como inválidos apenas os resultados ausentes; não inventar tentativas restantes.
- `result=OK`: todos os pares previstos são válidos e o lote foi concluído. Qualidade angular, singularidades de Euler e estabilidade do robô têm critérios separados.
- `result=PARTIAL`: existe pelo menos um resultado válido de sensor, mas nem todos os pares previstos são válidos ou o lote foi interrompido.
- `result=ERROR`: nenhum resultado válido de sensor foi obtido. **Pode ocorrer com `completion=COMPLETE` e `n_rows=n`**, quando todas as tentativas falham.
- `n_rows` conta linhas devolvidas; `n_valid` conta pares com `laser_valid=1` e `imu_valid=1`; `n_laser_valid` e `n_imu_valid` contam sucessos de cada sensor. `angles_valid=0` não descarta um quaternion válido. O cliente confere as contagens com as linhas.

Cada sensor tem seu campo de erro, para preservar inclusive falha simultânea: `laser_error` e `imu_error`. Sucesso usa `NONE`; timeout, frame inválido, reset e indisponibilidade usam códigos específicos, como `LASER_TIMEOUT`, `LASER_FRAME`, `LASER_RECOVERING`, `IMU_TIMEOUT`, `IMU_RESET`, `IMU_NOT_READY` e `IMU_BAD_QUAT`. Não confundir esses erros de tentativa com `code` em `DONE`, reservado à interrupção global (por exemplo, `REQUEST_TIMEOUT` ou `INTERNAL_ERROR`); em lote concluído, `code=NONE` mesmo que todos os sensores falhem.

Erros anteriores à aceitação usam `# ERR`, por exemplo `BAD_COMMAND`, `BAD_ARGUMENT`, `LINE_TOO_LONG`, `BUSY`, `DUPLICATE_ID` e `SERVICE_NOT_READY`; não geram `DONE` porque nenhum pedido foi iniciado. Não rejeitar o pedido só porque um sensor está indisponível. Enquanto houver conexão funcional e serviço capaz de responder, cada pedido aceito tem exatamente um término.

Regras de transporte e falha:

- Limitar comando a 128 bytes, tratar fragmentação e vários comandos na mesma leitura; descartar linha longa até o próximo delimitador. Nunca esperar bloqueado por uma linha inteira.
- Preservar offsets de escrita parcial, sem misturar uma resposta dentro de outra linha. Manter buffers limitados e prazo de envio; cliente sem consumir não pode prender o firmware indefinidamente.
- Não gerar medições do laser sem pedido. Fora do pedido, continuar o polling da IMU e descartar seus relatórios de aplicação; não zerar calibração a cada pose.
- Desconexão, perda de Wi-Fi ou cliente que excede o prazo de envio interrompe o pedido, desliga o feixe e preserva diagnóstico para `STATUS`. Não é possível garantir `DONE` em conexão perdida; o Python marca o lote como interrompido/incompleto.
- Sem resposta no prazo, o Python fecha a conexão, mantém o robô parado e reconecta para consultar prontidão. Só repete com novo ID, depois de recuperação. Não há reenvio automático do mesmo pedido nem replay persistente.
- Depois de timeout do laser, não aceitar uma resposta tardia como amostra do pedido seguinte. A limpeza precisa respeitar o protocolo real do módulo; esvaziar apenas o buffer UART não garante que uma medição anterior terminou.

Não há comandos `START`, `PAUSE`, `STOP` ou `DELAY`: uma solicitação tem início e fim definidos; fechar a conexão interrompe a operação em caso de abandono.

## 6. Registro no PC e comparação com o manipulador

### Dados e horários

O CSV transmitido contém distância em metros, azimute/inclinação/roll em graus, `accuracy_rad` em ponto flutuante, `imu_status` 0–3, sequência, validades de sensor/ângulos e erros. Campos ausentes ficam vazios; ponto decimal e vírgula como separador. Acrescentar `qw/qx/qy/qz` do mesmo relatório dos ângulos e erros separados por sensor. Não exportar aceleração nem timestamps do ESP.

O Python salva as leituras individuais e acrescenta `run_id`, `pose_id`, IDs de conexão/pedido e contexto da pose. Em uma tabela de pedidos ou arquivo auxiliar, guardar:

- Pose comandada, pose real imediatamente antes/depois do lote, unidades, referencial e confirmação de repouso.
- `pc_request_utc_ns` antes do envio e `pc_done_utc_ns` ao receber o término; em falha, registrar horário e motivo da interrupção.
- Duração usando `time.monotonic_ns()`; datas usando `time.time_ns()`. O primeiro serve para intervalos sem saltos do relógio civil, o segundo para rotular o ensaio no relógio do PC. Referência: [documentação Python de time](https://docs.python.org/3/library/time.html).
- `n` solicitado, recebido, válido por sensor e por par, conclusão do lote, erros por tentativa, acomodação, critérios de estabilidade/qualidade e motivo de qualquer exclusão. Salvar também lotes inteiramente inválidos; não descartá-los por não permitirem calcular média.
- Firmware/commit, placa, SDK, identificação dos sensores, parâmetros, eixo de montagem, offset de azimute, ordem/convenção dos quaternions, tolerância de norma, critérios de singularidade e calibração conhecida. Nunca registrar senha Wi-Fi.

Os horários do PC delimitam a transação observada pelo cliente. Não são o instante exato da medição física nem tornam simultâneas as leituras de um lote. A associação à pose vem dos IDs e da manutenção da posição durante a aquisição. Não é necessário sincronizar o ESP com o PC ou com o relógio do robô.

Oferecer uma função reutilizável `capture(request_id, n=1)` que aguarda a resposta com timeout, valida IDs/índices/cabeçalho/contagens e retorna leituras mais resultado. A integração com a API específica do manipulador fica no script do PC. Salvar o lote e confirmar persistência antes de iniciar a próxima pose. Uma execução com falha de rede preserva os dados já recebidos como incompletos.

### Comparar a mesma grandeza no mesmo referencial

Antes de comparar números, definir a transformação fixa entre efetuador e IMU e a relação entre a base do robô e o referencial da orientação da trena. A orientação do robô deve ser transformada para a orientação esperada da IMU; então usar as mesmas convenções de eixo do laser, azimute, inclinação e roll da seção 2.

Não subtrair diretamente “yaw do robô” de “azimute da trena”: podem ter eixos, ordem de Euler, sinais e referências diferentes. Preferir guardar também o quaternion retornado pelo robô, com sua ordem e convenções originais registradas. Converter no Python para a mesma ordem e o mesmo referencial do quaternion exportado pela trena. Para distâncias, registrar também a geometria do alvo e a posição do emissor, caso o robô seja usado para construir a referência de distância.

Fazer duas verificações independentes:

- **Cálculo da trena:** recalcular os ângulos com o quaternion exportado, o mesmo eixo/offset e as fórmulas do firmware. Comparar apenas ângulos geometricamente definidos, com tolerância numérica documentada.
- **Orientação contra o robô:** alinhar os referenciais e a montagem, validar e normalizar cópias dos quaternions, e calcular o ângulo da rotação relativa. Para quaternions unitários no mesmo referencial, usar `erro_deg = 2 × acos(clamp(abs(dot(q_trena, q_ref)), 0, 1)) × 180/π`. O valor absoluto trata a equivalência entre `q` e `-q`; não subtrair componentes nem converter para Euler para calcular esse erro. Uma alternativa é compor a rotação relativa e obter sua magnitude: [SciPy Rotation.magnitude](https://docs.scipy.org/doc/scipy/reference/generated/scipy.spatial.transform.Rotation.magnitude.html).

Essa comparação por quaternion evita as singularidades da representação de Euler; não corrige erros do sensor ou desalinhamento de referenciais. O offset adicionado ao azimute exibido não está aplicado ao quaternion exportado: não misturar esses dois caminhos de comparação.

A pose retornada pelo robô é uma referência de comparação com sua própria incerteza, não uma verdade absoluta sem erro. O Rotation Vector usa magnetômetro: avaliar a influência da estrutura metálica, motores e cabos do manipulador, inclusive em repouso. Um offset fixo não necessariamente corrige uma perturbação que muda com a pose.

### Estatísticas por pose

Guardar primeiro todas as tentativas; calcular resumos no PC sem sobrescrevê-las. Reportar fração de falhas por sensor e por pose usando os índices avaliados, distinguindo falha de medição de indisponibilidade/recuperação. Um lote completamente inválido é um resultado do ensaio; pode indicar ambiente inadequado, comunicação, configuração ou defeito, sem identificar sozinho a causa. Para distância, reportar média/mediana e dispersão; sem leitura válida, média e dispersão ficam indisponíveis; com apenas uma leitura válida, a dispersão amostral fica indisponível, não zero. Separar erro médio em relação à referência de dispersão entre repetições.

Para azimute e roll, usar diferenças e médias circulares: 359° e 1° têm média próxima de 0°, não 180°. A média pode ser implementada como `atan2(sum(sin θ), sum(cos θ))`, convertendo graus/radianos e normalizando a faixa. Resultante quase nula indica média sem direção bem definida. Referência: [definição de média circular no SciPy](https://docs.scipy.org/doc/scipy/reference/generated/scipy.stats.circmean.html); SciPy não é dependência obrigatória do cliente.

Tratar inclinação e singularidades conforme a geometria da seção 2; médias separadas de ângulos são resumos locais por pose, não uma média geral de rotações 3D. Para a avaliação principal de orientação, analisar os erros de rotação relativos calculados com quaternions; não fazer média componente a componente sem um método de média de rotações que trate `q ≡ -q`. Registrar a qualidade de cada leitura e o número de dados usados no resumo; não reduzir os status 0–3 a uma suposta qualidade média contínua. Repetir algumas poses após movimentação e retorno para distinguir dispersão em repouso de repetibilidade de posicionamento e efeitos de histórico.

## 7. Incrementos e critérios de aceite

O incremento 1 foi implementado e compilado; seus ensaios em hardware ainda são pendentes. As etapas 2–5 continuam como plano de implementação.

### Incremento 1: aplicação mínima e rede

**Implementado:** ambiente padrão `mm1_p4_lab`, entrada isolada, pinos Hosted compartilhados, Wi-Fi station/DHCP, reconexão e TCP/serial `STATUS`. `CAPTURE` informa `NOT_IMPLEMENTED`; não há sensores inicializados nesta etapa. Configuração e teste: [LAB_WIFI_P4.md](LAB_WIFI_P4.md).

**Aceite:** `pio run -e mm1_p4_lab` compila; boot identifica versões/placa; conecta ao laboratório e oferece diagnóstico sem internet ou horário sincronizado. Display/áudio ficam desligados e o ELF não inclui UI, pilha BLE da aplicação, biblioteca SD, portal ou serviço OTA. Rotinas internas de SDMMC necessárias ao SDIO do C6 e consulta da partição de boot podem permanecer no SDK. Não compilar os dois platforms na mesma invocação, conforme limitação documentada do projeto.

### Incremento 2: aquisição unitária

Extrair laser e conversão angular, habilitar apenas Rotation Vector e implementar uma repetição com validade, qualidade, novidade e prazos.

**Aceite:** a distância vem de uma transação nova e os ângulos de um relatório novo; getters repetidos não contam como novas amostras. Fórmulas coincidem com a aplicação para mesmo quaternion/eixo/offset. Quaternion e ângulos pertencem ao mesmo relatório; o CSV preserva w,x,y,z e precisão suficiente para recomputar os ângulos. Qualidade baixa permanece visível, `accuracy_rad` mantém a parte fracionária. Singularidade angular preserva o quaternion válido e sinaliza `angles_valid=0`. Testar checksum, frame fragmentado, falta de sensor, reset, cache antigo e resposta tardia; não há espera ilimitada.

### Incremento 3: protocolo e lote limitado

Implementar `STATUS`, `CAPTURE`, identificação, ACK/dados/DONE, estados, limites de `n` e envio limitado.

**Aceite:** `n=1`, `n=5` e `n=20` entregam contagens/índices corretos; valores fora de 1–20 são recusados. Injetar falha no primeiro e no meio do lote e falhas em todas as tentativas: em todos esses casos os índices vão até `n`, com erros separados e contagens corretas. Verificar que falha do laser não impede tentar a IMU e vice-versa, sem tentativas extras para obter sucessos. Distinguir `COMPLETE/ERROR` com `n` linhas de `INTERRUPTED`. Testar pedido com sensores indisponíveis, recuperação limitada, `BUSY`, ID repetido, linha longa, TCP fragmentado, prazo global, cliente lento e reconexão. Pedido aceito termina uma vez se a conexão permitir; desconexão desliga laser e impede contaminação do próximo pedido por respostas antigas. `STATUS` continua responsivo durante a espera do laser.

### Incremento 4: cliente Python e integração com poses

Implementar a função de captura, gravação de dados/metadados e sequência mover → confirmar repouso → acomodar → capturar → conferir/salvar. Primeiro testar com uma interface simulada do robô; adaptar depois à API real.

**Aceite:** o script não avança na primeira resposta/ACK, espera `DONE` e confere contagens. Usa a pose real, associa os IDs corretamente, grava lotes totalmente inválidos e distingue conclusão, validade e qualidade. Recalcula os ângulos a partir do quaternion e verifica erro de rotação zero para `q` e `-q`, rotação conhecida não nula e singularidade de Euler com quaternion válido. Testar movimento não concluído, perda de estabilidade, falha no lote, interrupção de rede e erro de gravação; não executar reenvio ou avanço de pose silencioso nesses casos.

### Incremento 5: validação no manipulador

Executar piloto com poses conhecidas, inicialmente cinco repetições por pose, ajustar acomodação/`n`/prazos e validar alinhamento de referenciais. Repetir orientações após movimento e retorno, observar efeitos magnéticos e ensaiar distâncias/alvos conhecidos.

**Aceite:** arquivo reproduzível com leituras individuais, qualidade, horários do PC, pose real, resultado e critérios de exclusão. Calcular viés e dispersão separadamente; validar média circular em 359°/1° e singularidades. Confirmar que a duração variável do laser não altera a pose durante o lote. Não exigir ensaios de NTP, deriva de cristal ou sincronização fina do ESP para aprovar esta versão.

## 8. Pontos a confirmar na implementação

- Modelo/protocolo do laser, erros, tempos mínimos, desligamento e garantia de novidade em fallback/recuperação.
- Revisão da placa/P4, SDK efetivamente resolvido e firmware ESP-Hosted compatível no C6.
- Firmware/calibração da IMU, condições magnéticas e comportamento de fila/reset sob polling.
- API do manipulador, significado de conclusão de movimento, pose real disponível, convenções e transformação de montagem.
- Tolerâncias de estabilidade, acomodação, quantidade de poses/repetições e limites de erro desejados. Esses são parâmetros do ensaio, não motivos para adicionar UI ou sincronização temporal ao ESP.

## 9. Referências locais de implementação

- [Aplicação atual](../src/main.cpp): sensores, polling, laser, `imu_update_angles_from_quat` e `refresh_sensor_display`.
- [Driver P4 da IMU](../src/board/p4/p4_imu.cpp) e [interface](../src/board/p4/p4_imu.h): HAL, callback, relatórios e caches.
- [Tipos SH-2](../src/board/p4/bno08x/sh2_SensorValue.h), [decoder](../src/board/p4/bno08x/sh2_SensorValue.c) e [API](../src/board/p4/bno08x/sh2.h).
- [Rádio](../src/sap6_ble.cpp): `hostedSetPins` e inicialização Hosted; [inicialização de placa a excluir](../src/board/p4/p4_board.cpp).
- [Datasheet BNO08x](datasheets/BNO080_085-Datasheet_v1.16.pdf), [calibração](datasheets/BNO08X-Sesnor-Calibration-Procedure.pdf) e [esquema da placa](datasheets/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf).

Incremento 1 implementado e compilado. Não houve gravação nem verificação de ping em hardware; os próximos incrementos ainda não foram implementados.
