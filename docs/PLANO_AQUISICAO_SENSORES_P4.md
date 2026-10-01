# Plano incremental: validação dos sensores da trena por aquisição Wi-Fi sob demanda

Status e revisão do plano em 30/09/2026: incrementos 1 e 2 implementados, com gravação, Wi-Fi, ping e `STATUS` via Docker confirmados pelo usuário na placa real. Incremento 3 implementado e verificado em software: aquisição unitária, compilação P4, testes C++ e 24 testes Python aprovados; captura física de laser e IMU confirmada pelo usuário, com término informado como `COMPLETE/OK`, após corrigir exclusivamente o lab para SDA31/SCL30. Qualidade baixa foi preservada (`imu_status=0`, `accuracy_rad=3.14160156`); a análise de calibração/precisão estava adiada e agora passa a ser a próxima etapa, no incremento 4. Demais ensaios de bancada continuam pendentes. Incrementos 4–7 permanecem planejados. Ver [resultado da rede na placa](LAB_WIFI_P4.md#resultado-na-placa-real), [guia da aquisição unitária](LAB_SENSORES_P4.md) e [cliente Docker](../docker/README.md). A análise inicial partiu do commit `684c16a` e arquivos locais. Revisão de escopo em 26/09/2026: aquisição em posições discretas do manipulador, por solicitação do Python, com timestamps no PC. Este plano substitui a proposta de streaming contínuo e sincronização temporal no ESP. O lote executará um número fixo de tentativas, incluindo falhas, e exportará quaternion junto dos ângulos e da qualidade; o incremento 3 limita-se a uma tentativa por solicitação. Por decisão do usuário, o azimute é magnético, sem offset ou declinação; a calibração nativa da IMU foi antecipada do antigo incremento 7 para o novo incremento 4, antes dos lotes e do manipulador. Os antigos incrementos 4, 5 e 6 tornam-se 5, 6 e 7. Calibração/trim do laser ficam fora desta sequência, para planejamento futuro separado; offsets de heading continuam fora do escopo.

## 1. Objetivo e sequência do ensaio

Criar um firmware mínimo de bancada para validar os sensores da trena no hardware P4, com Wi-Fi em modo station e servidor TCP. O Python posiciona o manipulador, espera a estabilização, solicita leituras da trena e aguarda a resposta completa antes de mover para a próxima pose. O ESP não controla o robô.

Sequência de uma posição do ensaio:

1. O Python atribui um `pose_id` e comanda uma posição/orientação predefinida.
2. Confirma pela interface do robô que o movimento terminou e que a pose **medida**, não apenas a comandada, está dentro das tolerâncias de posição, orientação e velocidade do ensaio.
3. Aguarda um tempo de acomodação configurável para vibração e estabilização da fusão da IMU. Esse tempo será definido em bancada; não presumir que o ACK de movimento seja confirmação de repouso.
4. Registra a pose real do robô e o horário do PC, envia `CAPTURE <request_id> [n]` e mantém o robô na mesma pose até receber o término.
5. Recebe as leituras individuais, confere resultado e quantidade, registra novamente a pose real e o horário do PC, salva tudo associado àquela posição e só então segue para a próxima.

O jitter do laser passa a afetar a **duração da permanência em cada pose**, sem exigir sincronização fina entre laser, IMU e robô enquanto a pose permanece estável. A validação será estática/quase estática: ela não caracteriza atraso ou desempenho da IMU durante movimento contínuo.

O firmware terá inicialização, rede, polling, aquisição por pedido e transmissão. Display, LVGL, touch, SD, BLE/SAP6, interface web, OTA, áudio, bateria, geometria de levantamento e persistência de pontos ficam fora do executável. Também ficam fora NTP/SNTP, âncoras UTC, compensação de deriva, timestamps de aquisição do ESP e alinhamento temporal pelo SH-2. Timers locais comuns continuam sendo usados para timeouts e intervalos. No incremento 4, acrescentar a calibração guiada da IMU pelo servidor TCP, com confirmações do operador e feedback visual no terminal do PC. A operação remota será completa, sem conexão USB com a trena; apesar disso, a serial oferecerá os mesmos comandos como alternativa.

SSID, senha, porta, tempos de espera e número de repetições são parâmetros. No incremento 1, configurar as credenciais em `include/lab_config.h`, ignorado pelo Git, antes de gravar o teste de rede.

## 2. O que o código atual realmente usa

### Hardware e compilação

| Item | Evidência no repositório | Consequência para o teste |
| --- | --- | --- |
| Hardware antigo | `env:denky32`, ESP32 CYD, versões v0.x | Não é o alvo deste trabalho. |
| Hardware recente | `env:mm1_p4`, Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 | Usar a definição `boards/mm1_p4.json`. |
| CPU/revisão | A definição atual usa `esp32p4_es` e 360 MHz; o documento de portabilidade identifica ECO2 | Preservar esses valores até verificar a revisão física; não mudar para 400 MHz apenas por ser a frequência nominal de outra revisão. |
| Rádio | ESP32-C6, ESP-Hosted por SDIO | P4 não possui Wi-Fi integrado. Usar o firmware de fábrica do C6 fornecido pela Waveshare; conexão Wi-Fi e ping já confirmados nesta placa. |
| IMU | BNO086 segundo os comentários do driver, família BNO08x/SH-2 | Confirmar identificação do módulo por `sh2_getProdIds()` e registrar no ensaio. |
| Barramento da IMU | I²C por software, **SDA 31/SCL 30 no laboratório**, endereço preferencial `0x4B` e alternativa `0x4A` | Ligação física confirmada pelo usuário; seleção exclusiva de `MM1_LAB`. Transporte existente preservado; INT e RST não estão conectados. |
| Laser | UART1, RX 21/TX 22, `9600`, `SERIAL_8N1`, RX com buffer de 1024 bytes | Não compartilhar com `Serial`, que fica para diagnóstico USB/UART. |

Referências locais: [PlatformIO](../platformio.ini), [placa](../boards/mm1_p4.json), [pinos](../src/board/p4/mm1_p4_pins.h), [portabilidade P4](ESP32_P4_PORT.md).

A pinagem da IMU nesta bancada difere da ordem padrão documentada no projeto original: SCL está fisicamente no GPIO30 e SDA no GPIO31. O alvo `mm1_p4_lab` seleciona explicitamente esse par em `p4_imu.cpp`, sob `MM1_LAB`; o mapa de pinos, o comportamento de seleção dos alvos originais e os documentos gerais permanecem preservados. O primeiro teste real confirmou laser e resposta `PARTIAL`, mas a IMU não foi detectada com a ordem anterior. Após gravar a correção, o usuário confirmou `imu=READY` e captura válida dos dois sensores. A avaliação de calibração/precisão, inicialmente adiada, será tratada no novo incremento 4; registro em [LAB_SENSORES_P4.md](LAB_SENSORES_P4.md#verificações-realizadas-e-ensaio-pendente).

Há comentários históricos conflitantes sobre os pinos SDIO. O caminho de `sap6_ble_begin()` usa **`hostedSetPins(18, 19, 14, 15, 16, 17, 54)`**, na ordem CLK, CMD, D0, D1, D2, D3 e RESET. O teste de rede chama essa mesma função diretamente em `src/lab/network.cpp`, antes de iniciar Wi-Fi, verificando seu retorno. Não transportar a checagem de prontidão baseada em MAC BLE para o teste sem Bluetooth; validar diretamente a inicialização Hosted e a conexão station.

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

**Rotation Vector é a orientação fundida de acelerômetro, giroscópio e magnetômetro**, referenciada à gravidade e ao norte magnético. A fusão acontece dentro do BNO: deixar de solicitar o relatório separado `SH2_ACCELEROMETER` ao host não desliga o acelerômetro necessário à fusão. No modo normal de aquisição, habilitar somente `SH2_ROTATION_VECTOR`, inicialmente a 50 Hz, tanto na abertura quanto após reset. A sessão de calibração do incremento 4 será a exceção temporária: habilitar também Game Rotation Vector e Magnetic Field calibrado conforme o procedimento do fabricante e restaurar o modo normal ao sair. Esses relatórios auxiliares não alteram a origem dos quaternions nem as colunas do CSV de captura. Fonte: [datasheet BNO08x local](datasheets/BNO080_085-Datasheet_v1.16.pdf), seções 2.1.1 e 2.2; [documentação do fabricante](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf).

### Orientação exportada: ângulos e quaternion

Reaproveitar as fórmulas de `imu_update_angles_from_quat()` e `imu_vec_body_to_world()` em uma função independente de UI, **retirando a soma do offset de azimute no firmware de laboratório**. Executar para cada Rotation Vector novo selecionado para uma repetição. Exportar também o quaternion do mesmo relatório usado nessa conversão, para auditoria do cálculo e comparação direta de rotações no Python. A relação com a tela SENSOR em `refresh_sensor_display()` será:

| Coluna proposta | Valor existente | Convenção a preservar |
| --- | --- | --- |
| `azimuth_deg` | Fórmula de `imu_azimuth_deg`, sem somar `g_azimuth_offset_deg` | Azimute do eixo do laser em relação ao norte magnético, normalizado em [0, 360), sem declinação ou zeramento de heading. |
| `inclination_deg` | `imu_inclination_deg` | Inclinação do eixo do laser, de −90° a +90°, positiva acima da horizontal. |
| `roll_deg` | `imu_roll` | Roll de Euler calculado pela fórmula atual, aproximadamente de −180° a +180°. |

Acrescentar `qw,qx,qy,qz`, respectivamente `rotationVector.real/i/j/k`, na ordem explícita **w,x,y,z**, com componentes adimensionais. Preservar os valores decodificados, sem offset de azimute, rotação de montagem adicional, troca de sinal ou normalização silenciosa na exportação. São dados de orientação fundida, não dados brutos de ADC. Usar precisão decimal suficiente para recuperar o `float` (por exemplo, 9 algarismos significativos). Registrar nos metadados a ordem e a convenção corpo → mundo usada por `imu_vec_body_to_world()`.

O Python poderá reproduzir as fórmulas com esses valores e os mesmos parâmetros para conferir os ângulos do firmware. Para comparar rotações, verificar a norma original e normalizar apenas uma cópia de um quaternion finito e não nulo, preservando o registro original. Normas incompatíveis com uma rotação unitária devem ser sinalizadas, não corrigidas silenciosamente para esconder erro. Bibliotecas podem adotar ordem diferente e normalização automática; no SciPy, a ordem w,x,y,z exige `scalar_first=True`: [documentação de quaternions](https://docs.scipy.org/doc/scipy/reference/generated/scipy.spatial.transform.Rotation.from_quat.html).

Azimute e inclinação do feixe não devem ser substituídos diretamente pelos valores internos `imu_yaw` e `imu_pitch`. Normalizar o vetor `IMU_LASER_AXIS_BX/BY/BZ`, girá-lo pelo quaternion e obter `(wx, wy, wz)` no referencial magnético usado pelo código (+X leste magnético, +Y norte magnético, +Z para cima). No laboratório, calcular **`azimuth = norm_deg360(atan2(wx, wy) × 180/π)`**, sem o termo `+ offset` da aplicação original, e `inclination = atan2(wz, sqrt(wx² + wy²)) × 180/π`. Manter também a fórmula atual de roll, sem reinterpretá-lo como rotação em torno de um eixo de laser arbitrário.

O eixo de montagem será um parâmetro explícito, registrado nos metadados e fixo durante o ensaio. Registrar **`azimuth_reference=magnetic` e `azimuth_offset_deg=0`** como descrição do dado exportado, não como um offset ajustável. Não carregar o valor de heading salvo na NVS nem o padrão −21,7° da aplicação original. Não aplicar declinação, `Head=0`, tare ou outra rotação de referência ao quaternion exportado. A transformação do referencial magnético para a base do manipulador será medida e aplicada no Python, mantendo os dados adquiridos no referencial original. Por isso, a igualdade com o azimute mostrado pela aplicação original só é esperada quando o offset dela for zero; inclinação e roll conservam as fórmulas existentes.

Exportar os ângulos com casas decimais suficientes para análise, sem limitar à única casa exibida na tela. Nas comparações, tratar a passagem 359° → 0° como continuidade circular. Perto do feixe vertical, o azimute fica indefinido/sensível; nas singularidades de Euler, roll também exige cuidado. Essas limitações geométricas não são corrigidas por um status alto da IMU.

Correções mínimas necessárias para que o teste represente os dados recebidos:

- Preservar `rotationVector.accuracy` como `float`. Hoje há conversão para `int`, e a UI trata esse inteiro como qualidade 0–3. São grandezas diferentes: a qualidade está em `sh2_SensorValue_t.status`, enquanto `accuracy` é uma estimativa angular em radianos.
- Exportar `accuracy_rad` e `status` do mesmo Rotation Vector usado nos ângulos: status 0 = não confiável, 1 = precisão baixa, 2 = média, 3 = alta. A estimativa angular não é uma garantia de erro de cada ângulo. Manter validade, sequência, perdas e eventos de reset/calibração; um relatório decodificado pode resultar em `imu_valid=1` e `imu_status=0` no CSV. A checagem de norma `Consistency` deixa de fazer parte do teste ao retirar o relatório de aceleração.
- Expor novidade por callback/contador e preservar `sequence` e `status`. Os getters atuais podem retornar repetidamente o mesmo cache; uma repetição do ensaio deve consumir um relatório novo. Não exportar timestamps SH-2 nem reconstruí-los para sincronização.
- Continuar atendendo a IMU mesmo sem pedido, com Rotation Vector a 50 Hz e sem batching intencional, para manter a fusão e evitar acúmulo. Relatórios não selecionados para o pedido são descartados deliberadamente; não há requisito de transmitir todos os 50 relatórios/s.
- Ao detectar `SH2_RESET`, invalidar o cache, registrar a ocorrência e verificar o sucesso de `p4_imu_enable_reports()`. Hoje `p4_imu_poll()` consome o sinal de reset e não verifica esse retorno; não depender somente de `p4_imu_was_reset()` no chamador.
- Preservar inicialmente a configuração de calibração interna do sensor; registrar reinicializações, status e condições de calibração. A conversão angular usa o eixo de montagem registrado, sem offset de azimute. Essa decisão não desabilita a calibração interna do BNO; apenas remove a correção de heading da aplicação. O controle explícito da calibração nativa fica para o novo incremento 4, com operação remota por TCP (serial como alternativa), confirmação de cada etapa e feedback no cliente Python, sem recalibrar ou zerar automaticamente a cada pose.

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

### Trim do laser: significado e escopo da aquisição

No firmware original, `laser_raw_m()` fornece a distância retornada pelo módulo em metros, antes da correção da aplicação; “crua” aqui não significa leitura de ADC. `laser_used_m()` chama `mm1_distance_at_ref_m(laser_raw_m(), 0, g_mm1_range_offset_mm)`. Em [mm1_geometry.h](../include/mm1_geometry.h), a correção efetiva é:

```cpp
if (!isfinite(laser_m))
    return laser_m;
const float d = laser_m + trim_mm * 0.001f;
return (d > 0.f) ? d : 0.f;
```

Para entradas finitas, `D_usada_m = max(0, D_laser_m + trim_mm / 1000)`. O trim é uma correção aditiva constante: positivo aumenta a distância e negativo diminui. Por exemplo, 2,000 m com +15 mm resulta em 2,015 m; com −15 mm, em 1,985 m. A UI limita o ajuste a ±300 mm, começa em zero e salva o valor na NVS. Ele pode compensar um desvio constante ou uma mudança do ponto de referência ao longo do feixe; não altera a medição interna do módulo, não corrige ganho/escala, ruído nem orientação. O argumento `proj_top` é ignorado nessa função: ela não acrescenta automaticamente o comprimento da carcaça.

Nos incrementos 3–7, **`distance_m` será a distância original de uma transação nova do laser**, sem trim da aplicação, sem carregar esse ajuste da NVS e sem importar a saturação em zero como tratamento de falha. Registrar `distance_source=laser_report` e `laser_trim_mm=0`. Falhas continuam sendo campos vazios com validade/erro explícitos. A configuração remota de trim e a calibração do laser não pertencem ao novo incremento 4 nem às demais etapas aqui numeradas. Se forem retomadas futuramente, elaborar um plano separado que preserve `distance_m` original e identifique qualquer distância corrigida em outro campo, sem mudar silenciosamente o significado da coluna existente.

A palavra “trim” aparece em contextos distintos no código:

- **Menu `Trim` / `Laser trim` / `g_mm1_range_offset_mm`:** o ajuste de distância em milímetros descrito acima.
- **`Heading trim`, na página IMU:** nome dado ao próprio `g_azimuth_offset_deg`, em graus. A aplicação soma esse valor ao azimute; pode representar declinação ou um ajuste empírico. `Head=0` altera esse mesmo offset para zerar a direção atual, sem ser uma calibração nativa dos sensores do BNO. Nenhum deles será aplicado ao azimute do laboratório.
- **`trim_mm` nos auxiliares de geometria:** o mesmo tipo de ajuste linear. `mm1_laser_delta_mm()` apenas o retorna; `mm1_imu_arm_mm()` calcula `abs(mm1_imu_x_base_mm(proj_top) + trim_mm)`. Não há chamadas desses dois auxiliares no código examinado; eles não acrescentam outra correção ao caminho atual de `laser_used_m()`.
- **`String.trim()` em comandos de texto:** remove espaços em branco nas extremidades da string; não tem relação com calibração.

A calibração nativa da IMU via SH-2 é uma operação distinta desses trims. Da mesma forma, o comando `Zero C` do laser, condicionado ao protocolo alternativo `LZR_PROTO_ILIASAM`, envia um comando ao módulo; não é a soma de `trim_mm` e não será incorporado implicitamente ao teste.

## 3. Decisão: n tentativas por lote, incluindo resultados inválidos

**Adotar `CAPTURE <request_id> [n]`: avaliar os dois sensores em cada uma das n tentativas e devolver uma linha por tentativa, tenha ela sucesso ou falha.** `n` é o número de tentativas, não uma meta de resultados bons. Uma falha comum de sensor não encerra o lote e não gera uma tentativa extra para substituí-la. Omitir `n` equivale a `n=1`. O incremento 3 aceita somente 1; ampliar para 1–20 no incremento 5, com memória e prazo limitados.

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

| Arquivo | Responsabilidade |
| --- | --- |
| `src/lab/main.cpp` | `setup()` e `loop()` de laboratório; fonte original `src/main.cpp` preservada e excluída do alvo lab. |
| `src/lab/network.*` | Incremento 1: Wi-Fi station, configuração direta dos pinos SDIO, reconexão e diagnóstico serial/TCP. |
| `src/lab/laser_poll.*` | Parser e máquina de estados para uma transação do laser sob demanda. |
| `src/board/p4/p4_imu.*` e `bno08x/*` | Transporte existente, Rotation Vector, novidade, qualidade e reset. |
| `src/lab/orientation.*` | Conversão para os ângulos atuais, independente de UI. |
| `src/lab/capture_service.*` | Aquisição unitária, validade e novidade; contador de repetições no incremento 5. |
| `src/lab/capture_format.cpp` | Parser do comando e serialização CSV/DONE; transporte permanece em `network.cpp`. |
| `src/lab/imu_calibration.*` (planejado) | Incremento 4: etapas da calibração, confirmações do operador, qualidade, DCD e retorno ao modo de aquisição. |
| `docker/scripts/calibrate_imu.py` (planejado) | Incremento 4: guia interativo por TCP, confirmação de cada etapa, monitor textual de qualidade e registro da sessão no PC. |
| `include/lab_config.example.h` e configuração local ignorada pelo Git | Wi-Fi, porta, limites e convenções angulares. |
| `docker/` | Incremento 2: ambiente Python autocontido, configuração, dependências e teste TCP de `STATUS`. |
| `docker/scripts/capture_sensors.py` | Evolução posterior: cliente reutilizável pelo script do manipulador, gravação e análise no PC. |

Inicialização: Serial/GPIOs mínimos → ESP-Hosted → Wi-Fi station → sensores → servidor TCP. O servidor aceita `CAPTURE` também com um ou ambos os sensores indisponíveis, desde que o serviço consiga atender o pedido e seus prazos. Durante calibração, a exclusão mútua tem prioridade: `CAPTURE` responde `BUSY`. A indisponibilidade de sensor fora da calibração é registrada por tentativa; assim é possível caracterizar um lote com todas as leituras inválidas. Não há dependência de internet, servidor de horário ou data válida no ESP. Não chamar `p4_board_init()`, que carrega display/touch e SD; manter o backlight desabilitado.

Loop: atender TCP/serial → atender IMU e laser → avançar sessão de calibração ou pedido/prazos → transmitir um trecho limitado da resposta → espera curta fixa, inicialmente 5 ms. Não haverá comando de ajuste de delay pelo cliente. A espera do Python pela resposta não significa bloquear o firmware em uma função longa: preservar atendimento da IMU, da rede e do watchdog.

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

Servidor no P4, cliente no PC, porta configurável (padrão 5000), um cliente por vez. Protocolo ASCII por linhas terminadas em `\n`, aceitando `\r\n`. O incremento 3 implementa um subconjunto do **protocolo/schema 2**, limitado a `n=1`, com `stage=unit_capture` e cabeçalho CSV por captura (`csv_header=per_capture`). O contrato de lotes abaixo é o destino do incremento 5; é incompatível com o streaming proposto anteriormente. Ao ampliar esse contrato, atualizar também a identificação de etapa/metadados e o cliente Docker, sem alterar silenciosamente o formato que ele espera.

| Comando | Comportamento |
| --- | --- |
| `STATUS` | Informa estado, sensores, pedido ativo/último resultado e parâmetros de timeout; disponível também durante coleta. |
| `CAPTURE <request_id> [n]` | Em `IDLE`, aceita um pedido de 1 a 20 tentativas; padrão 1, inclusive com sensor indisponível. Responde ACK, depois dados e marcador de término. Em outro estado, responde erro sem enfileirar trabalho. |

Essa tabela descreve a aquisição até a integração com o manipulador. O incremento 4 acrescentará a família proposta `CAL_IMU START/STATUS/CONFIRM/SAVE/CANCEL`, com identificação da sessão e da etapa a confirmar, respostas próprias e sem misturar telemetria de calibração ao CSV de captura. Publicar essa capacidade e a nova etapa em `HELLO/META` e atualizar o cliente Docker, mantendo a consulta de STATUS e a captura unitária. Comandos de trim/calibração do laser e de offset de azimute ficam fora do escopo; a orientação adquirida continuará magnética, sem declinação ou zeramento de heading.

O Python usa `request_id` inteiro positivo de 32 bits, estritamente crescente dentro da conexão. O ESP rejeita IDs já aceitos ou anteriores (`DUPLICATE_ID`); não refaz medições automaticamente. A combinação `boot_id`, `connection_id` e `request_id` identifica o pedido no log. Reinício do ESP muda `boot_id`; nova conexão muda `connection_id`. A associação a `pose_id` fica no Python, sem o ESP precisar conhecer o robô.

Ao conectar, emitir `# HELLO`, metadados `# META` (incluindo limites de espera) e um cabeçalho CSV uma única vez. Um pedido aceito recebe `# OK CAPTURE` com ID, `n` e `timeout_ms`. Guardar no máximo 20 registros do lote e devolvê-los ao concluir todas as tentativas ou sofrer uma interrupção global, seguidos por **`# DONE`** com o mesmo ID. O Python espera `DONE`, não apenas o ACK ou a primeira linha.

Exemplo ilustrativo com três tentativas: a primeira falha no laser, mas a IMU responde; as outras duas têm sucesso. O exemplo usa eixo do laser +X; o azimute sem offset e a distância sem trim seguem a convenção obrigatória dos incrementos 3–7. Os quaternions identidade e seu negativo representam a mesma orientação:

```text
# HELLO MM1LAB 2 boot_id=b1 connection_id=1 state=IDLE
# META schema=2 imu_report=rotation_vector imu_interval_us=20000 n_max=20 quaternion_order=wxyz laser_axis=1/0/0 azimuth_reference=magnetic azimuth_offset_deg=0 distance_source=laser_report laser_trim_mm=0
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

Não há comandos `START`, `PAUSE`, `STOP` ou `DELAY` para streaming de aquisição: cada `CAPTURE` tem início e fim definidos; fechar a conexão interrompe a operação em caso de abandono. A família separada `CAL_IMU` controla somente a sessão de calibração do incremento 4.

## 6. Registro no PC e comparação com o manipulador

### Dados e horários

O CSV transmitido contém a distância original do módulo em metros (`distance_m`, sem trim), azimute magnético sem offset/inclinação/roll em graus, `accuracy_rad` em ponto flutuante, `imu_status` 0–3, sequência, validades de sensor/ângulos e erros. Campos ausentes ficam vazios; ponto decimal e vírgula como separador. Acrescentar `qw/qx/qy/qz` do mesmo relatório dos ângulos e erros separados por sensor. Não exportar aceleração nem timestamps do ESP.

O Python salva as leituras individuais e acrescenta `run_id`, `pose_id`, IDs de conexão/pedido e contexto da pose. Em uma tabela de pedidos ou arquivo auxiliar, guardar:

- Pose comandada, pose real imediatamente antes/depois do lote, unidades, referencial e confirmação de repouso.
- `pc_request_utc_ns` antes do envio e `pc_done_utc_ns` ao receber o término; em falha, registrar horário e motivo da interrupção.
- Duração usando `time.monotonic_ns()`; datas usando `time.time_ns()`. O primeiro serve para intervalos sem saltos do relógio civil, o segundo para rotular o ensaio no relógio do PC. Referência: [documentação Python de time](https://docs.python.org/3/library/time.html).
- `n` solicitado, recebido, válido por sensor e por par, conclusão do lote, erros por tentativa, acomodação, critérios de estabilidade/qualidade e motivo de qualquer exclusão. Salvar também lotes inteiramente inválidos; não descartá-los por não permitirem calcular média.
- Firmware/commit, placa, SDK, identificação dos sensores, parâmetros, eixo de montagem, referência magnética, offset de azimute fixo em zero, distância original sem trim, ordem/convenção dos quaternions, tolerância de norma, critérios de singularidade e calibração conhecida. Registrar também a transformação medida entre o referencial magnético e a base do robô, separadamente dos dados originais. Nunca registrar senha Wi-Fi.

Os horários do PC delimitam a transação observada pelo cliente. Não são o instante exato da medição física nem tornam simultâneas as leituras de um lote. A associação à pose vem dos IDs e da manutenção da posição durante a aquisição. Não é necessário sincronizar o ESP com o PC ou com o relógio do robô.

Oferecer uma função reutilizável `capture(request_id, n=1)` que aguarda a resposta com timeout, valida IDs/índices/cabeçalho/contagens e retorna leituras mais resultado. A integração com a API específica do manipulador fica no script do PC. Salvar o lote e confirmar persistência antes de iniciar a próxima pose. Uma execução com falha de rede preserva os dados já recebidos como incompletos.

### Comparar a mesma grandeza no mesmo referencial

Antes de comparar números, definir a transformação fixa entre efetuador e IMU e medir a transformação entre o referencial magnético da trena e a base do robô. Não passar pelo norte geográfico nem aplicar declinação magnética: o experimento determinará diretamente essa relação de referenciais. Manter essa transformação no Python e nos metadados, sem modificar as leituras originais. A orientação do robô deve ser transformada para a orientação esperada da IMU; então usar as mesmas convenções de eixo do laser, azimute, inclinação e roll da seção 2.

Não subtrair diretamente “yaw do robô” de “azimute da trena”: podem ter eixos, ordem de Euler, sinais e referências diferentes. Preferir guardar também o quaternion retornado pelo robô, com sua ordem e convenções originais registradas. Converter no Python para a mesma ordem e o mesmo referencial do quaternion exportado pela trena. Para distâncias, registrar também a geometria do alvo e a posição do emissor, caso o robô seja usado para construir a referência de distância.

Fazer duas verificações independentes:

- **Cálculo da trena:** recalcular os ângulos com o quaternion exportado, o mesmo eixo do laser e as fórmulas do firmware, sem offset. Comparar apenas ângulos geometricamente definidos, com tolerância numérica documentada.
- **Orientação contra o robô:** alinhar os referenciais e a montagem, validar e normalizar cópias dos quaternions, e calcular o ângulo da rotação relativa. Para quaternions unitários no mesmo referencial, usar `erro_deg = 2 × acos(clamp(abs(dot(q_trena, q_ref)), 0, 1)) × 180/π`. O valor absoluto trata a equivalência entre `q` e `-q`; não subtrair componentes nem converter para Euler para calcular esse erro. Uma alternativa é compor a rotação relativa e obter sua magnitude: [SciPy Rotation.magnitude](https://docs.scipy.org/doc/scipy/reference/generated/scipy.spatial.transform.Rotation.magnitude.html).

Essa comparação por quaternion evita as singularidades da representação de Euler; não corrige erros do sensor ou desalinhamento de referenciais. No laboratório, tanto o quaternion quanto o azimute derivado permanecem referidos ao quadro magnético, sem correção de declinação. Aplicar a transformação para o referencial do robô somente em cópias destinadas à análise.

A pose retornada pelo robô é uma referência de comparação com sua própria incerteza, não uma verdade absoluta sem erro. O Rotation Vector usa magnetômetro: avaliar a influência da estrutura metálica, motores e cabos do manipulador, inclusive em repouso. Um offset fixo não necessariamente corrige uma perturbação que muda com a pose.

### Estatísticas por pose

Guardar primeiro todas as tentativas; calcular resumos no PC sem sobrescrevê-las. Reportar fração de falhas por sensor e por pose usando os índices avaliados, distinguindo falha de medição de indisponibilidade/recuperação. Um lote completamente inválido é um resultado do ensaio; pode indicar ambiente inadequado, comunicação, configuração ou defeito, sem identificar sozinho a causa. Para distância, reportar média/mediana e dispersão; sem leitura válida, média e dispersão ficam indisponíveis; com apenas uma leitura válida, a dispersão amostral fica indisponível, não zero. Separar erro médio em relação à referência de dispersão entre repetições.

Para azimute e roll, usar diferenças e médias circulares: 359° e 1° têm média próxima de 0°, não 180°. A média pode ser implementada como `atan2(sum(sin θ), sum(cos θ))`, convertendo graus/radianos e normalizando a faixa. Resultante quase nula indica média sem direção bem definida. Referência: [definição de média circular no SciPy](https://docs.scipy.org/doc/scipy/reference/generated/scipy.stats.circmean.html); SciPy não é dependência obrigatória do cliente.

Tratar inclinação e singularidades conforme a geometria da seção 2; médias separadas de ângulos são resumos locais por pose, não uma média geral de rotações 3D. Para a avaliação principal de orientação, analisar os erros de rotação relativos calculados com quaternions; não fazer média componente a componente sem um método de média de rotações que trate `q ≡ -q`. Registrar a qualidade de cada leitura e o número de dados usados no resumo; não reduzir os status 0–3 a uma suposta qualidade média contínua. Repetir algumas poses após movimentação e retorno para distinguir dispersão em repouso de repetibilidade de posicionamento e efeitos de histórico.

## 7. Incrementos e critérios de aceite

O incremento 1 já permitiu gravação, conexão e ping na placa. O incremento 2 disponibiliza o ambiente no PC, validado com servidores simulados e com `STATUS` real. O incremento 3 acrescenta aquisição unitária, com compilação/testes de software aprovados e captura dos dois sensores confirmada na placa. A calibração nativa da IMU passa a ser a próxima etapa; reconexão e demais critérios de bancada ainda precisam de verificação. As etapas 4–7 continuam apenas planejadas. A nova ordem é 4: calibração da IMU, 5: protocolo/lotes, 6: cliente e integração com poses, 7: validação no manipulador.

| Nova iteração | Escopo | Relação com o plano anterior |
| --- | --- | --- |
| 1 | Rede mínima | Mantida |
| 2 | Docker e STATUS | Mantida |
| 3 | Aquisição unitária | Mantida |
| 4 | Calibração nativa da IMU e feedback no PC | Parte de IMU do antigo 7, antecipada; sem laser/heading |
| 5 | Protocolo e lote limitado | Antiga 4 |
| 6 | Cliente Python e integração com poses | Antiga 5 |
| 7 | Validação no manipulador | Antiga 6 |

### Incremento 1: aplicação mínima e rede

**Implementado:** ambiente padrão `mm1_p4_lab`, entrada isolada, configuração direta dos pinos Hosted, Wi-Fi station/DHCP, reconexão e TCP/serial `STATUS`. `CAPTURE` informa `NOT_IMPLEMENTED`; não há sensores inicializados nesta etapa. Configuração e teste: [LAB_WIFI_P4.md](LAB_WIFI_P4.md).

**Aceite:** `pio run -e mm1_p4_lab` compila; boot identifica versões/placa; conecta ao laboratório e oferece diagnóstico sem internet ou horário sincronizado. Display/áudio ficam desligados e o ELF não inclui UI, pilha BLE da aplicação, biblioteca SD, portal ou serviço OTA. Rotinas internas de SDMMC necessárias ao SDIO do C6 e consulta da partição de boot podem permanecer no SDK. Não compilar os dois platforms na mesma invocação, conforme limitação documentada do projeto.

### Incremento 2: contêiner Docker e teste Python de STATUS

**Implementado:** ambiente de execução Python autocontido em **`./docker`**, com imagem `python:3.12.14-slim-bookworm`, Compose em rede host, cliente `scripts/status.py`, configuração por argumentos/ambiente, registro JSONL opcional e testes. O primeiro uso é executar o teste de `STATUS` de [LAB_WIFI_P4.md](LAB_WIFI_P4.md), sem depender de Python no host nem de `CAPTURE`. Comandos: [docker/README.md](../docker/README.md).

Estrutura implementada:

```text
docker/
  Dockerfile
  compose.yaml
  .dockerignore
  .gitignore
  .env.example
  requirements.txt
  README.md
  scripts/
    status.py
  tests/
    mock_server.py
    test_status.py
  data/                 # saídas persistentes dos testes; conteúdo ignorado pelo Git
```

- Usar `./docker` como contexto de build, sem `COPY` de arquivos externos a esse diretório. A versão Python está fixada no Dockerfile e `requirements.txt` registra que esta etapa usa somente a biblioteca padrão (`socket`, `argparse` e tratamento de erros); não requer instalar pacotes para esses módulos. Incluir e fixar novas bibliotecas conforme as etapas seguintes precisarem delas.
- Disponibilizar um serviço/comando de execução única no Compose. IP da trena, porta (padrão 5000) e timeout (inicialmente 5 s para conexão/leitura) serão argumentos ou variáveis de ambiente, documentados em `.env.example`. A senha Wi-Fi pertence ao firmware e não é necessária ao cliente. O README deve mostrar a partir da raiz como construir e executar, por exemplo `docker compose -f docker/compose.yaml run --rm status --host <IP> --port 5000`.
- Configurar o serviço com **`network_mode: host`** no Compose desde o primeiro teste, tendo **Docker Engine no Linux** como ambiente de referência. O contêiner compartilha a rede do PC, sem uma bridge ou NAT adicional do Docker no caminho; não declarar `ports` nem uma rede própria para esse serviço. O cliente continua abrindo uma conexão TCP para o IP e a porta da trena. Rotas, firewall e acesso à rede Wi-Fi continuam sendo responsabilidade do host; esse modo não resolve isolamento de clientes no AP. Não requer USB nem `privileged`. Referência: [rede host do Docker](https://docs.docker.com/engine/network/drivers/host/).
- Manter o modo host na futura integração com o manipulador. Para uma API com apenas conexões TCP de saída, bridge também atenderia, mas descoberta por broadcast/multicast, portas dinâmicas ou conexões iniciadas pelo robô podem exigir configuração adicional nesse modo. Compartilhar a rede do PC simplifica esses caminhos; confirmar os requisitos quando a API for definida. Caso o cliente passe a abrir portas de escuta, elas precisam estar livres no host e acessíveis pelas regras locais de firewall. Docker Desktop não é o ambiente de referência desta etapa; seu modo host exige habilitação e tem limitações diferentes do Engine no Linux.
- Implementar `status.py` para ler e validar `HELLO` e `META`, enviar `STATUS\n` e aguardar uma linha `# OK STATUS stage=network_only`. Tratar TCP como fluxo: receber linhas completas mesmo quando fragmentadas, limitar tamanho e espera, detectar EOF e mensagens inesperadas. Exibir a resposta completa, incluindo IP, RSSI e estado Hosted. Sair com código zero somente após uma resposta válida; timeout, conexão recusada, desconexão ou erro de protocolo geram diagnóstico e código diferente de zero.
- Não aguardar CSV nem `DONE` nesta etapa: o firmware atual ainda responde `NOT_IMPLEMENTED` a `CAPTURE`. Registrar, se desejado, o horário do PC e a resposta em `docker/data`, montado como volume para preservar saídas após remover o contêiner. Não adicionar NTP ou timestamps no ESP.
- Manter esse mesmo ambiente nas próximas etapas, acrescentando captura, gravação e análise antes da integração com o robô. Reservar `docker/external/manipulador/` como localização proposta para o futuro submódulo, preservando o contexto autocontido; não criar nem adicionar um submódulo agora. Quando o outro repositório for escolhido, fixar sua revisão pelo Git, documentar a inicialização dos submódulos e incorporar suas dependências/API. O teste `STATUS` deve continuar funcionando sem esse submódulo.

**Aceite:** em um checkout novo, com Docker Engine e Compose disponíveis no Linux, construir e executar usando somente os arquivos de `./docker` e o IP informado. Confirmar que o serviço usa `network_mode: host`, sem mapeamento de portas. Receber os dois cumprimentos e uma resposta `STATUS` válida da placa; conferir falha com diagnóstico e saída não zero para IP/porta incorretos, timeout e encerramento prematuro. Verificar respostas fragmentadas e malformadas com servidor simulado. Documentar o comando e o resultado real do teste, sem confundir ping com validação do protocolo TCP. Nenhuma aquisição de sensores ou movimentação do manipulador é necessária para concluir esta etapa.

**Validação realizada:** imagem construída com sucesso; configuração Compose verificada para rede host, contexto e volume; 14 testes automatizados aprovados dentro da imagem, cobrindo protocolo, fragmentação, timeout, EOF, erros de configuração/conexão e registro. A execução do comando real do cliente contra outro contêiner simulador em loopback recebeu `HELLO`, `META` e `STATUS` e salvou o JSONL no host. Posteriormente, o usuário executou `docker compose -f docker/compose.yaml run --rm status` contra a placa real e recebeu as três mensagens esperadas: `wifi=CONNECTED`, `ip=192.168.0.10`, `rssi_dbm=-39`, `tcp=LISTENING`, `port=5000` e `hosted=1`. O teste de comunicação Docker → servidor embarcado foi aprovado; saída completa em [LAB_WIFI_P4.md](LAB_WIFI_P4.md#resultado-na-placa-real). Sensores ainda indisponíveis são o comportamento previsto neste incremento. Nenhuma alteração no firmware foi necessária nesta etapa.

### Incremento 3: aquisição unitária

**Implementado; captura unitária de laser e IMU confirmada na placa após correção da pinagem. Calibração/precisão e demais ensaios de aceite permanecem pendentes.** Laser, conversão angular, aquisição e formatação foram separados em `src/lab/`. O lab habilita apenas Rotation Vector a 50 Hz e executa uma repetição com validade, qualidade, novidade e prazos. Exporta azimute magnético sem offset e distância original sem trim, sem carregar ajustes de heading/distância da NVS. Não há comandos de calibração. O caminho da aplicação original continua disponível nos outros alvos.

Para permitir ensaio remoto já nesta etapa, foi antecipado o subconjunto `CAPTURE <id> [1]` da etapa de lotes (antigo incremento 4, agora 5), com ACK/CSV/DONE, `BUSY`, IDs crescentes, cancelamento por desconexão e buffers limitados. `STATUS` agora inclui diagnóstico dos sensores. O cliente Docker ganhou `--capture ID` e registro JSONL com a resposta, sem alterar o modo de rede host. Os lotes de até 20 e a integração com poses continuam nas etapas seguintes. Instruções completas em [LAB_SENSORES_P4.md](LAB_SENSORES_P4.md).

**Novidade e recuperação implementadas:** laser usa um único SINGLE após drenagem e mira, sem QUICK/READ_RES. Após timeout, resposta inválida ou cancelamento com medição pendente, bloqueia novas transações com `LASER_RESYNC_REQUIRED` e continua avaliando a IMU. Nesta etapa a recuperação exige desligar/ligar o conjunto incluindo o laser; a sequência automática segura depende de validação do módulo. A etapa IMU aguarda leitura SHTP vazia e callback posterior, sem aceitar cache, duplicata ou NACK como novidade. O driver preserva precisão fracionária/status/sequência, invalida cache em reset e limita cada operação HAL a 100 ms no lab. Quaternion inválido conserva componentes finitos e qualidade; singularidade conserva quaternion e ângulos definidos.

**Verificação de software concluída em 29/09/2026:** build `mm1_p4_lab` e imagem Docker aprovados; 24 testes Python de STATUS/captura por TCP passaram; testes C++ de aquisição e driver passaram com verificadores de memória/comportamento indefinido. O driver compartilhado foi testado com GPIO/SH-2 simulados nas configurações lab e original. Os testes cobrem fórmulas, checksum/BCD/frame fragmentado, ausência de sensor, reset, cache, resposta tardia, prazos, qualidade fracionária, singularidade, CSV, ACK/DONE e persistência. Essa verificação inicial foi feita em software. Posteriormente, o usuário gravou o firmware e confirmou a captura do laser por TCP/serial com resultado `PARTIAL` e `IMU_NOT_READY`. Após confirmar SDA31/SCL30 na montagem, foi aplicada a correção exclusiva do lab; nova compilação e testes C++ da inicialização nos modos lab/original passaram. No teste seguinte com `--capture 1`, o usuário confirmou `laser=READY`, `imu=READY`, distância `1.40500009 m`, quaternion, ângulos e um par válido, com `COMPLETE/OK` informado no término. O relatório manteve `imu_status=0` e `accuracy_rad=3.14160156`: sucesso de aquisição não certifica boa calibração ou precisão. Naquele teste, a análise de calibração foi adiada por decisão do usuário. A revisão de 30/09/2026 antecipa esse trabalho para o novo incremento 4, ainda sem implementação. Os contadores acumulados de reset, I²C, decodificação e sequência foram registrados para investigação posterior, sem atribuir seus eventos à amostra. A saída e a observação sobre o espaçamento na transcrição de `DONE` estão no [guia de bancada](LAB_SENSORES_P4.md#captura-de-laser-e-imu-após-a-correção-da-pinagem).

**Aceite:** a distância vem de uma transação nova e os ângulos de um relatório novo; getters repetidos não contam como novas amostras. Fórmulas coincidem com a aplicação para mesmo quaternion/eixo quando o offset de azimute dela é zero. Verificar que ajustes não nulos previamente salvos na NVS não afetam o azimute nem a distância exportados pelo laboratório. Quaternion e ângulos pertencem ao mesmo relatório; o CSV preserva w,x,y,z e precisão suficiente para recomputar os ângulos. Qualidade baixa permanece visível, `accuracy_rad` mantém a parte fracionária. Singularidade angular preserva o quaternion válido e sinaliza `angles_valid=0`. Testar checksum, frame fragmentado, falta de sensor, reset, cache antigo e resposta tardia; não há espera ilimitada.

### Incremento 4: calibração nativa da IMU com feedback no PC

**Planejado, sem implementação nesta revisão.** Antecipar a parte de IMU do antigo incremento 7. Seguir o [BNO08X Sensor Calibration Procedure](datasheets/BNO08X-Sesnor-Calibration-Procedure.pdf), documento CEVA 1000-4044, revisão 1.6, fevereiro/2023, seções 2.1, 2.2 e 3 (páginas impressas 2–5). A seção de calibração do usuário reutiliza os movimentos e configurações da calibração de fábrica. Calibrar o BNO na trena montada, conservando a ligação SDA31/SCL30 exclusiva do lab.

Esta etapa habilita e acompanha a **calibração dinâmica interna** do sensor e salva seu DCD; não aplica uma correção angular no P4. Não incluir trim/calibração do laser, `Zero C`, offsets de heading, declinação, tare ou `Head=0`. O laser permanece desligado durante o procedimento. A aquisição continua exportando Rotation Vector magnético e distância sem trim depois da sessão.

#### Operação remota e feedback sem tela embarcada

**Decisão:** realizar todo o procedimento pelo servidor TCP via Wi-Fi, com um guia interativo no terminal do PC, dentro de `./docker`. A trena poderá permanecer sem conexão USB durante toda a calibração. A serial será uma alternativa com os mesmos comandos, validações e estados; não será necessária para iniciar, acompanhar, confirmar etapas ou salvar. Não adicionar LVGL, display, touch, áudio, servidor web ou dependências de interface gráfica.

O monitor mantém uma conexão TCP e consulta `CAL_IMU STATUS`, inicialmente a 5 Hz, enquanto apresenta a instrução atual e aguarda a confirmação do operador. A espera por entrada no terminal não interrompe as consultas de qualidade nem o atendimento da IMU. O cliente Docker deve permitir entrada interativa pelo terminal. Não iniciar calibração apenas por abrir o monitor.

Contrato proposto para esta iteração, ainda a implementar:

| Comando/ação | Comportamento planejado |
| --- | --- |
| `CAL_IMU START` | Em IDLE e com IMU pronta, preparar a sessão e devolver `session_id` e a primeira etapa. A conexão que iniciou a sessão controla suas confirmações; TCP e serial não podem disputar o controle. |
| `CAL_IMU STATUS` | Informar sessão, etapa/subetapa atual, instrução, confirmações realizadas, qualidade recente e ações permitidas, sem avançar o procedimento. |
| `CAL_IMU CONFIRM <session_id> <step_id>` | Registrar que o operador realizou a etapa/subetapa indicada. O cliente envia ao confirmar no terminal (por exemplo, Enter após a instrução), espera o ACK e só então apresenta a próxima instrução devolvida pelo firmware. |
| `CAL_IMU SAVE <session_id>` | Confirmação explícita da etapa final: gravar uma vez, apenas depois dos movimentos confirmados e do critério magnético do procedimento. Mostrar confirmação do sensor, erro ou resultado desconhecido. |
| `CAL_IMU CANCEL <session_id>` | Encerrar sem iniciar gravação DCD e restaurar a configuração de aquisição. Durante SAVING, informar que a gravação já foi iniciada e aguardar seu resultado/prazo. Não interpretar cancelamento como restauração dos coeficientes antigos em RAM. |

O firmware mantém a ordem das etapas e valida os identificadores; o cliente apresenta esse estado ao operador. Recusar confirmações de outra sessão ou de uma etapa futura. Uma confirmação duplicada não avança novamente: responder com o estado já registrado. Cada subetapa, inclusive uma nova rodada de movimentos, recebe identificador próprio. Não permitir pular movimentos com `SAVE`, nem avançar por tempo decorrido ou por aumento da qualidade. A confirmação do operador atesta a execução declarada; o firmware não tenta reconhecer automaticamente os movimentos.

O monitor mostra texto e valores, com cores apenas opcionais: etapa e movimento solicitado, status do magnetômetro (0 não confiável, 1 baixo, 2 médio, 3 alto), status do Game Rotation Vector, status/`accuracy_rad` do Rotation Vector magnético, idade/novidade dos relatórios e resultado da gravação. Não representar 0–3 como porcentagem de conclusão nem prometer aumento monotônico. Não deduzir qualidades individuais de acelerômetro/giroscópio a partir do status de uma fusão. Os ângulos ou uma representação simples do Game Rotation Vector podem auxiliar a conferir os movimentos; não substituirão o quaternion magnético nas capturas.

Exemplo apenas de apresentação no terminal, não de formato de protocolo:

```text
Etapa 5/6 - Magnetometro - eixo roll
Gire cerca de 180 graus e volte ao inicio, em aproximadamente 2 s.
Magnetometro: 2/3 (medio) | Game RV: 3/3 | Rotation Vector: 1/3
Precisao angular RV: 0.35 rad | Relatorios: recentes
[Enter] Confirmar movimento realizado e seguir para pitch
DCD: ainda nao gravado
```

Valores são fictícios. A confirmação do movimento pelo operador, a qualidade/recepção recente e a confirmação de gravação pelo sensor são eventos diferentes. Manter poucas linhas atualizadas e eventos de mudança; permitir saída textual comum quando o terminal não suporta atualização. Salvar instruções/etapas, confirmações e seus ACKs, qualidades, configurações e resultado em JSONL no volume `docker/data`, com relógio do PC. O código cabe no ambiente Python existente e não depende do futuro submódulo do robô.

#### Preparação técnica antes das etapas do operador

- Em IDLE e com IMU pronta, registrar configuração atual via `sh2_getCalConfig()`, relatórios ativos e qualidade inicial. Entrar em estado de calibração exclusivo; recusar `CAPTURE` enquanto a sessão estiver ativa e recusar início de calibração durante captura. TCP e serial passam pelas mesmas verificações. Após mudar os relatórios, aguardar novidades: qualidade anterior fica apenas como referência, nunca como evidência de progresso ou prontidão da sessão nova.
- Usar Configure ME Calibration (`sh2_setCalConfig()`) para habilitar acelerômetro e magnetômetro. Para o perfil manual desta etapa, em que a trena será movida na mão, habilitar também a flag de giroscópio, conforme a observação do PDF. Registrar a máscara efetiva. Não transportar automaticamente esse perfil para uma futura rotina executada pelo manipulador nem confundir esta sequência com `sh2_startCal()`/`sh2_finishCal()`, que representam outra API de calibração simples.
- Habilitar temporariamente `SH2_GAME_ROTATION_VECTOR` (0x08) e `SH2_MAGNETIC_FIELD_CALIBRATED` (0x03). **Magnetic Field deve ser solicitado a 50 Hz**, conforme o PDF. Manter Rotation Vector como indicador complementar da fusão magnética, identificado separadamente. Proposta inicial de carga: Game RV a 20 Hz e Rotation Vector a 10 Hz durante a calibração; essas duas taxas são escolhas de engenharia, não exigências do PDF, e precisam de validação no I²C por software. Não reduzir os 50 Hz do magnetômetro para acomodar o monitor. O terminal consulta a 5 Hz, independentemente dessas taxas. Não é necessário reintroduzir o relatório separado de aceleração nem acrescentar colunas ao CSV de aquisição.

#### Etapas do operador na ordem do procedimento do fabricante

Preservar as seis etapas da seção 2.2 do PDF, usadas também na calibração pelo usuário da seção 3. As confirmações no terminal organizam a execução; não substituem movimentos, tempos aproximados ou observação da qualidade.

| Etapa do PDF | Instrução e condição para avançar |
| --- | --- |
| **1. Ambiente** | Posicionar a trena montada em ambiente relativamente livre de interferências magnéticas, afastada de fontes como estruturas magnéticas, gabinetes de PC e monitores. O operador confirma que preparou o local. |
| **2. Observar Magnetic Field** | Apresentar o status 0–3 do relatório Magnetic Field e sua idade. O operador confirma que está acompanhando esse indicador; exigir relatório recente, mas não status alto nesta etapa. |
| **3. Acelerômetro** | Orientar a trena em **4–6 posições distintas**, mantendo cada uma por aproximadamente **1 s**. Sugerir as seis faces de um cubo, sem impor ordem ou alinhamento exato. Antes dos movimentos, permitir escolher 4, 5 ou 6 posições (padrão 6), conforme a alternativa do PDF quando alguma face for difícil. Guiar uma posição por vez e solicitar confirmação após mantê-la; avançar à etapa 4 somente após confirmar todas as posições escolhidas. |
| **4. Giroscópio** | Colocar a trena sobre uma superfície imóvel por aproximadamente **2–3 s**. Solicitar confirmação de que o repouso foi realizado antes de apresentar os movimentos do magnetômetro. |
| **5. Magnetômetro** | Guiar separadamente **roll, pitch e yaw**: girar cerca de **180° e voltar à posição inicial**, em aproximadamente **2 s por eixo**. Pedir confirmação após cada eixo e manter o status Magnetic Field visível. Após realizar os três eixos, continuar as rotações enquanto esse status não atingir **2 ou 3**; novas rodadas também exigem confirmação dos movimentos. Status alto antes de completar os eixos não dispensa as confirmações. Ao concluir os movimentos com status magnético recente 2 ou 3, oferecer a etapa 6, aguardando a decisão do operador. |
| **6. Save DCD Now** | Exibir que os movimentos foram confirmados e solicitar a ação explícita **Salvar DCD**, que envia `CAL_IMU SAVE <session_id>`. Conferir novamente qualidade magnética recente 2 ou 3 e enviar `sh2_saveDcdNow()`. Só apresentar “DCD gravado” após confirmação do sensor; envio do comando ou qualidade alta não comprovam persistência. |

Um cronômetro pode auxiliar nos tempos aproximados, mas nunca confirma uma etapa pelo operador. Não acrescentar uma janela obrigatória de qualidade alta por vários segundos ao critério do PDF: o critério magnético é status 2 ou 3, após os movimentos. Se a qualidade cair antes de salvar, voltar à orientação de continuar as rotações da etapa 5; se o relatório ficar ausente/desatualizado, sinalizar isso e impedir gravação até recuperar feedback recente e cumprir novamente o critério. A disponibilidade de feedback e o controle de etapas são verificações da aplicação, não novos movimentos de calibração.

Ao encerrar a sessão, desabilitar os relatórios auxiliares, restaurar a máscara de calibração dinâmica registrada na entrada e Rotation Vector a 50 Hz, invalidando caches/barreiras de aquisição antes do próximo `CAPTURE`.

O status magnético é o critério indicado no procedimento para acompanhar essa fase. O `accuracy_rad` do Rotation Vector e seus status complementam o feedback da fusão, mas não medem diretamente erro de orientação contra uma referência externa. A prontidão para salvar não equivale ao aceite metrológico de todos os eixos. Esse aceite continua exigindo comparação e repetibilidade no ensaio posterior.

#### Persistência, estados e limites

DCD é salvo na flash do BNO, separado dos ajustes da aplicação na NVS do ESP. Não apagar DCD existente ao iniciar nem recalibrar a cada pose. Para garantir que uma sessão cancelada não seja gravada automaticamente, estabelecer no lab, a partir deste incremento, uma política explícita de `sh2_setDcdAutoSave(false)` após inicialização/reset, verificando o suporte e registrando o resultado; os salvamentos passam a ocorrer por ação do operador. Não modificar essa política no firmware original. Falha ao configurar esse controle impede prometer “sem gravação”: informar erro e não iniciar a sessão nessa condição. Cancelar encerra o processo sem SAVE; os ajustes dinâmicos já produzidos em RAM podem continuar ativos. Não prometer rollback dos coeficientes sem um procedimento específico de recarga validado.

Separar estados de sessão como `IDLE`, `CALIBRATING`, `READY_TO_SAVE`, `SAVING` e resultado final (`SAVED`, `CANCELLED`, `TIMEOUT`, `ERROR`). Informar o último resultado depois de voltar a IDLE. `STATUS` geral continua responsivo e informa que a captura está ocupada pela calibração. As consultas de qualidade são respostas solicitadas, sem despejar relatórios de 50 Hz no TCP nem misturar linhas espontâneas no CSV. Usar buffers fixos/limitados, indicação de relatório ausente/desatualizado e prazos; proposta inicial de limite de sessão: 5 min, configurável e visível ao operador. Esse limite é da aplicação, não do procedimento do fabricante; expirar encerra com TIMEOUT, nunca confirma etapas nem salva. Ao perder a conexão que acompanha a sessão, cancelar sem iniciar SAVE e restaurar o modo normal; não salvar automaticamente ao desconectar. Se SAVE já estiver em andamento, concluir a espera limitada e registrar sucesso, falha ou resultado desconhecido; desconexão/cancelamento não desfaz uma gravação enviada. Reset da IMU interrompe a sessão, invalida a qualidade anterior e reaplica a configuração normal, sem retomar nem repetir uma gravação silenciosamente.

**Pendência técnica identificada no código local:** `sh2_getCalConfig()`, `sh2_setCalConfig()` e `sh2_saveDcdNow()` usam operações síncronas cuja tabela local não define timeout, e `opProcess()` aceita espera ilimitada quando esse campo é zero. Antes de expô-las, tornar a execução limitada e compatível com o atendimento da rede, exclusivamente no caminho lab, sem chamadas concorrentes/reentrantes à biblioteca SH-2. O limite de 100 ms da HAL, sozinho, não limita a espera por uma resposta de comando. Definir prazos por operação (proposta inicial: 2 s), ACK/estado final da sessão e tratamento de retorno/erro; conferir suporte real do BNO086 e não usar espera ilimitada no loop. Se uma gravação perder sua confirmação, registrar resultado desconhecido em vez de afirmar que salvou ou reenviar automaticamente.

**Aceite:** realizar a sequência completa por TCP/Wi-Fi sem USB conectado à trena; verificar também os mesmos comandos pela serial como alternativa. Conferir as seis etapas na ordem do PDF, as opções de 4–6 orientações, a confirmação individual das posições e dos três eixos, a repetição das rotações com qualidade baixa e a ação explícita de salvar. Observar feedback recente enquanto o terminal aguarda confirmação e confirmar Magnetic Field a 50 Hz. Testar confirmação ausente, duplicada, fora de ordem ou de outra sessão, tentativa de SAVE antecipado e qualidade alta antes de terminar os movimentos; nenhum desses casos pode pular etapas ou gravar automaticamente. Verificar que consultas e timers não iniciam nem confirmam etapas, que TCP/serial não disputam uma sessão e que captura e calibração não se sobrepõem. Conferir leitura/configuração ME, qualidade baixa visível, qualidade que oscila, ausência de relatório, resposta de SAVE e conservação do DCD após desligar/ligar o conjunto. Registrar retorno dos comandos e dados antes/depois; um retorno SH-2 bem-sucedido é necessário, mas não substitui o ensaio físico de persistência. Testar falta de sensor, timeout SH-2, falha ao salvar, cancelamento, perda de TCP, reset e restauração de Rotation Vector a 50 Hz. Investigar os contadores de erro/decodificação/sequência observados na bancada, sem assumir que são causados pela calibração. Confirmar ausência de display/LVGL/áudio, nenhuma mudança no firmware original, nenhuma calibração do laser e nenhum offset de heading. Só depois seguir para lotes e comparação com o manipulador.

### Incremento 5: protocolo e lote limitado

Ampliar `STATUS`/`CAPTURE` e ACK/dados/DONE do incremento 3 para lotes de 1–20 tentativas, resultado/estado do pedido, interrupções globais, diagnóstico após desconexão e recuperação limitada validada no laser real. Consolidar cabeçalho/metadados e atualizar o cliente para o contrato de lote. Preservar o atendimento da IMU mesmo quando o laser não puder iniciar outra medição com garantia de novidade.

**Aceite:** `n=1`, `n=5` e `n=20` entregam contagens/índices corretos; valores fora de 1–20 são recusados. Injetar falha no primeiro e no meio do lote e falhas em todas as tentativas: em todos esses casos os índices vão até `n`, com erros separados e contagens corretas. Verificar que falha do laser não impede tentar a IMU e vice-versa, sem tentativas extras para obter sucessos. Distinguir `COMPLETE/ERROR` com `n` linhas de `INTERRUPTED`. Testar pedido com sensores indisponíveis, recuperação limitada, `BUSY`, ID repetido, linha longa, TCP fragmentado, prazo global, cliente lento e reconexão. Pedido aceito termina uma vez se a conexão permitir; desconexão desliga laser e impede contaminação do próximo pedido por respostas antigas. `STATUS` continua responsivo durante a espera do laser.

### Incremento 6: evolução do cliente Python e integração com poses

Evoluir o cliente e o contêiner criados no incremento 2, mantendo scripts, configuração e dependências sob `./docker`. Implementar a função de captura, gravação de dados/metadados em volume persistente e sequência mover → confirmar repouso → acomodar → capturar → conferir/salvar. Primeiro testar com uma interface simulada do robô; adaptar depois à API real do repositório adicionado como submódulo em `docker/external/manipulador/`. Registrar a revisão desse submódulo nos metadados do ensaio e preservar o comando independente de `STATUS`.

**Aceite:** o script não avança na primeira resposta/ACK, espera `DONE` e confere contagens. Usa a pose real, associa os IDs corretamente, grava lotes totalmente inválidos e distingue conclusão, validade e qualidade. Recalcula os ângulos a partir do quaternion e verifica erro de rotação zero para `q` e `-q`, rotação conhecida não nula e singularidade de Euler com quaternion válido. Testar movimento não concluído, perda de estabilidade, falha no lote, interrupção de rede e erro de gravação; não executar reenvio ou avanço de pose silencioso nesses casos.

### Incremento 7: validação no manipulador

Executar piloto com poses conhecidas, inicialmente cinco repetições por pose, ajustar acomodação/`n`/prazos e validar alinhamento de referenciais. Repetir orientações após movimento e retorno, observar efeitos magnéticos e ensaiar distâncias/alvos conhecidos.

**Aceite:** arquivo reproduzível com leituras individuais, qualidade, horários do PC, pose real, resultado e critérios de exclusão. Calcular viés e dispersão separadamente; validar média circular em 359°/1° e singularidades. Confirmar que a duração variável do laser não altera a pose durante o lote. Não exigir ensaios de NTP, deriva de cristal ou sincronização fina do ESP para aprovar esta versão.

## 8. Pontos a confirmar na implementação

- Modelo/protocolo do laser, erros, tempos mínimos, desligamento e garantia de novidade em fallback/recuperação.
- Registrar a revisão da placa/P4 e o SDK efetivamente resolvido, mantendo o firmware de fábrica Waveshare no C6.
- Firmware/calibração da IMU, condições magnéticas e comportamento de fila/reset sob polling.
- API do manipulador, significado de conclusão de movimento, pose real disponível, convenções e transformação de montagem.
- Tolerâncias de estabilidade, acomodação, quantidade de poses/repetições e limites de erro desejados. Esses são parâmetros do ensaio, não motivos para adicionar UI ou sincronização temporal ao ESP.

## 9. Referências locais de implementação

- [Aplicação atual](../src/main.cpp): sensores, polling, laser, `imu_update_angles_from_quat` e `refresh_sensor_display`.
- [Driver P4 da IMU](../src/board/p4/p4_imu.cpp) e [interface](../src/board/p4/p4_imu.h): HAL, callback, relatórios e caches.
- [Geometria e trim do laser](../include/mm1_geometry.h): correção linear da distância usada pela aplicação original.
- [Tipos SH-2](../src/board/p4/bno08x/sh2_SensorValue.h), [decoder](../src/board/p4/bno08x/sh2_SensorValue.c) e [API](../src/board/p4/bno08x/sh2.h).
- [Rádio](../src/sap6_ble.cpp): `hostedSetPins` e inicialização Hosted; [inicialização de placa a excluir](../src/board/p4/p4_board.cpp).
- [Datasheet BNO08x](datasheets/BNO080_085-Datasheet_v1.16.pdf), [calibração](datasheets/BNO08X-Sesnor-Calibration-Procedure.pdf) e [esquema da placa](datasheets/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf).

Incrementos 1 e 2 confirmados na placa para Wi-Fi/ping e `STATUS` via Docker. Incremento 3 implementado e verificado em software, com guia de teste em [LAB_SENSORES_P4.md](LAB_SENSORES_P4.md); a captura de laser e IMU foi confirmada na placa com SDA31/SCL30. A calibração nativa da IMU será o incremento 4; lotes, integração com poses e validação no manipulador serão os incrementos 5, 6 e 7, respectivamente. Todas essas etapas e os demais ensaios de bancada continuam pendentes. Calibração do laser e offsets de heading não fazem parte da nova etapa.
