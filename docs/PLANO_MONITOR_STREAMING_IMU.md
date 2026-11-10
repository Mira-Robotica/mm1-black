# Plano de implementação: monitor de relatórios da IMU via TCP e Docker

Documento de planejamento, criado em 10/11/2026. **A feature descrita aqui ainda deve ser implementada.** Este arquivo é autocontido e pode ser copiado para outro clone de `mm1-black` e usado como especificação/prompt de implementação. Todos os caminhos abaixo são relativos à raiz desse repositório, independentemente do diretório em que ele esteja clonado.

Base inspecionada: commit `06c0e068f262006b8133c6d1c15b9be652913092`. No clone de destino, conferir as instruções locais e os arquivos atuais antes de implementar; adaptar a integração se a revisão divergir, preservando os requisitos deste documento.

## 1. Objetivo e escopo

Adicionar um serviço Docker `monitor-imu` para visualizar continuamente, no terminal do PC, dados enviados pela trena em uma conexão TCP persistente. O cliente pede o início de uma sessão; o firmware configura os relatórios e passa a transmitir automaticamente, sem um comando CAPTURE por amostra. Ao terminar, o cliente pede a parada e o dispositivo restaura a configuração anterior.

Implementar no firmware de laboratório `mm1_p4_lab`, que já oferece o servidor TCP, reutilizando as relações matemáticas da aplicação de produção. Não é necessário executar a interface gráfica de produção, ROS, Kinova, laser ou servidor web. Não modificar o comportamento dos experimentos e comandos existentes.

Exibir:

- Campo magnético Raw: X, Y, Z em contagens ADC.
- Campo magnético Uncalibrated: X, Y, Z em µT.
- Campo magnético Calibrated: X, Y, Z em µT.
- Azimute, inclinação do feixe e roll calculados separadamente a partir de RV.
- Azimute, inclinação do feixe e roll calculados separadamente a partir de GRV.
- Azimute magnético derivado do vetor Raw.
- Azimute magnético derivado do vetor Uncalibrated.

Começar com publicação TCP e atualização de tela a **2 Hz**, configuráveis entre 0,2 e 5 Hz na primeira versão. Separar a taxa de publicação da taxa interna dos sensores/fusão. Não reduzir automaticamente RV/GRV a 2 Hz: usar inicialmente os intervalos de aquisição do perfil existente (nominalmente 20.000 µs na base inspecionada) e publicar somente a amostra válida mais recente de cada relatório. Confirmar quais taxas Raw/Uncalibrated o sensor aceita e registrar as efetivamente confirmadas.

## 2. Pontos de integração existentes

| Caminho | Papel na implementação |
| --- | --- |
| `src/lab/main.cpp` | Loop cooperativo; integrar o tick do streaming sem bloquear os sensores/rede. |
| `src/lab/network.cpp` | Servidor TCP, HELLO/META, parser de comandos, buffer TX e desconexão. |
| `src/lab/capture_service.*`, `capture_format.cpp` | Aquisição existente e exemplo de serialização; manter CAPTURE/CAPTURE_IMU compatíveis. |
| `src/lab/calibration_service.*` | Sessão com proprietário, configurações e restauração; reutilizar padrões, sem disparar calibração guiada. |
| `src/board/p4/imu_sparkfun/p4_imu.cpp` | Adaptador de laboratório SparkFun: recebe atualmente RV, GRV e Magnetic Field Calibrated. |
| `src/board/p4/p4_imu.h` | Tipos e API da IMU; acrescentar amostras/caches/configuração necessários. |
| `src/board/p4/imu_sparkfun/hooks.h` | Integração e confirmação das configurações de relatórios. |
| `src/board/p4/SparkFun_BNO08x_Arduino_Library-1.0.6/src/sh2_SensorValue.*` | Decoder/tipos Raw e Uncalibrated já disponíveis; conferir antes de duplicar código. |
| `src/main.cpp` | Produção: `imu_vec_body_to_world`, `imu_update_angles_from_quat`, `norm_deg360`. |
| `src/lab/orientation.*` | Atualmente calcula azimute, inclinação do feixe e roll; não confundir inclinação com pitch de Euler. |
| `docker/scripts/status.py` | Conexão, leitura de linhas, validação e argumentos existentes. |
| `docker/scripts/experiment.py` | Convenções de metadados de sessão. |
| `docker/scripts/experiment_orientation.py` | Referência auxiliar; atualmente normaliza quaternion, diferentemente do helper C++ de laboratório. |
| `docker/compose.yaml`, `docker/Dockerfile` | Imagem Python e serviços status/calibrate-imu; acrescentar monitor sem dependência ROS. |
| `tests/lab/`, `docker/tests/` | Testes nativos e testes de cliente com servidor simulado. |

Arquivos novos sugeridos: `src/lab/stream_service.h/.cpp`, `docker/scripts/monitor_imu.py`, testes de sessão/protocolo/ângulos e `docs/LAB_MONITOR_IMU.md` com instruções de operação. Ajustar a divisão conforme a arquitetura encontrada.

## 3. Relatórios e significado das medidas

| Nome SH-2 | ID | Unidade/conteúdo |
| --- | --- | --- |
| Raw Magnetometer | `0x16` | Três componentes ADC e timestamp do sensor. |
| Magnetic Field Uncalibrated | `0x0F` | Três componentes em µT, ainda corrigidas para soft iron; viés estimado de hard iron separado. |
| Magnetic Field Calibrated | `0x03` | Três componentes em µT, corrigidas para hard e soft iron. |
| Rotation Vector | `0x05` | Quaternion RV com referência magnética. |
| Game Rotation Vector | `0x08` | Quaternion GRV; rumo arbitrário e sujeito a deriva. |

O objetivo é tornar visível a perturbação que chega ao sensor. Raw é o canal direto para isso; Uncalibrated não significa ausência de todas as correções. Não converter contagens ADC em µT com fator presumido. Não subtrair o viés de hard iron da saída Uncalibrated, pois isso retiraria justamente a característica que se quer observar.

Preservar o perfil de calibração dinâmica encontrado no início. A sessão de monitoramento não deve iniciar calibração, salvar DCD, apagar calibração, aplicar tare ou escrever NVS. Habilitar um relatório não equivale a habilitar/desabilitar calibração dinâmica. Não adicionar um desligamento implícito de calibração para tornar o gráfico mais conveniente.

A implementação pode guardar o viés Uncalibrated internamente para diagnóstico, mas a primeira versão não precisa exibi-lo nem gravá-lo em cada linha: os dados pedidos são os campos e ângulos acima.

Referências técnicas: [SH-2 Reference Manual, §§6.5.15–6.5.20](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf) e [BNO08X Datasheet, §§2.1.3, 2.2 e 3.1](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf). Validar também o suporte no firmware físico, não apenas a presença de tipos na biblioteca.

## 4. Convenções dos ângulos

### 4.1 RV e GRV: reproduzir as relações da produção

Calcular no firmware de laboratório e transmitir os ângulos prontos. O cliente valida e apresenta; evitar duas implementações concorrentes como fonte dos valores da tela.

Usar quaternion `q=(w,x,y,z)`, transformação corpo → mundo e frente da trena **+X da IMU**, vetor `(1,0,0)`. Para cada quaternion, separadamente:

```text
fx = 1 - 2*(y*y + z*z)
fy = 2*(x*y + w*z)
fz = 2*(x*z - w*y)
azimuth_deg = wrap360(degrees(atan2(fx, fy)))
inclination_deg = degrees(atan2(fz, hypot(fx, fy)))
roll_deg    = degrees(atan2(2*(w*x + y*z), 1 - 2*(x*x + y*y)))
```

No RV, o referencial é ENU: leste, norte, cima; azimute horário a partir do norte. No GRV, aplicar a mesma geometria, mas identificar na tela e nos metadados **referência arbitrária**, sem chamá-lo de norte magnético. Não zerar nem alinhar automaticamente o GRV ao RV.

**Decisão do usuário:** mostrar os mesmos ângulos usados pela produção: `imu_azimuth_deg`, `imu_inclination_deg` e `imu_roll`, estendendo o cálculo ao quaternion GRV. A produção grava a inclinação em `p.pitch`; usar o rótulo `Inclinação (pitch)` e colunas `rv_inclination_deg`/`grv_inclination_deg` para evitar ambiguidade. Não exibir `imu_pitch` de Euler em seu lugar. Frente para cima deve produzir inclinação positiva. Preservar o schema existente de CAPTURE.

**Não incorporar `g_azimuth_offset_deg`.** Reutilizar as relações geométricas da produção, mas calcular todos os azimutes sem correção angular adicional. Não criar parâmetro de offset na CLI ou no protocolo, nem importar declinação/NVS da produção. RV mantém referência magnética; GRV mantém referência arbitrária; os azimutes dos vetores seguem a direção do campo observado. Não aplicar tare ou alinhamento automático.

Validar finitude, norma e singularidades. Preservar a política existente de tolerância de norma (0,02 na base inspecionada), sem normalização silenciosa divergente entre firmware e Python. Tratar azimute sem projeção horizontal e roll singular como indisponíveis, nunca como zero. Inclinação válida pode continuar atualizando mesmo que outro ângulo seja singular.

### 4.2 Azimutes derivados dos vetores magnéticos

Não aplicar diretamente `atan2(Bx,By)` aos valores em eixos da IMU: a produção aplica `atan2(leste,norte)` ao **vetor frontal em coordenadas de mundo**, não ao vetor magnético no corpo. Uma transposição ingênua introduz inversões/saltos de 90°.

Usar a direção vertical obtida do **GRV**, para compensar inclinação sem usar o rumo magnético do RV. Definir:

```text
u = normalize(transpose(R_grv) * (0,0,1))   # vertical para cima no corpo
f = normalize((1,0,0) - dot((1,0,0),u)*u)  # frente projetada no plano horizontal
l = cross(u,f)                            # esquerda horizontal
Nf = dot(m,f)
Nl = dot(m,l)
azimuth_mag_deg = wrap360(degrees(atan2(-Nl,Nf)))
```

Aplicar uma vez com `m=Raw` e outra com `m=Uncalibrated`. A fórmula expressa o azimute horário da frente +X em relação à direção horizontal do campo. Com dispositivo nivelado, eixos +Y à esquerda e +Z para cima, reduz a `wrap360(degrees(atan2(-My,Mx)))`. O yaw arbitrário do GRV cancela no cálculo de `u`; não usar seu azimute como correção magnética.

Esta é uma extensão geométrica coerente com a convenção da produção, não uma rotina de magnetômetro já existente em `src/main.cpp`. Documentar e testar essa distinção.

Antes de usar Raw nessa fórmula, **verificar o mapeamento dos eixos físicos Raw para os eixos dos relatórios fusionados** e a comparabilidade das escalas entre eixos. Não assumir que a ordem dos três números garante o mesmo referencial. Usar somente um mapeamento fixo documentado, com permutações/sinais e, se indispensável, fatores fixos conhecidos; nunca estimar correções dinâmicas a partir da exposição ao ímã. Os valores Raw exibidos/salvos devem permanecer os recebidos; registrar o mapeamento usado apenas para o ângulo.

O azimute Raw é **indicativo**, sujeito a offsets e escalas do sensor; ADC não é uma bússola calibrada. Se não for possível estabelecer o mapeamento necessário no clone/documentação/bancada, manter o azimute Raw inicialmente indisponível, explicar a limitação na documentação e não alegar validação dessa entrega até resolvê-la. Não inventar uma transformação para preencher a tela.

Calcular cada azimute magnético somente com vetor novo válido e GRV válido suficientemente próximo no tempo (limite inicial sugerido: 200 ms entre recepções locais no firmware, declarado nos metadados). Não atualizar esse azimute com magnetômetro antigo e GRV novo. Campo horizontal quase nulo, frente quase vertical, dependência ausente ou incompatibilidade de epoch mantêm o último ângulo válido. Definir tolerâncias em unidades apropriadas: ADC para Raw e µT para Uncalibrated; não copiar indiscriminadamente um limiar adimensional de quaternion.

## 5. Sessão TCP e restauração

Comandos propostos, terminados por `\n`, na conexão já existente (porta padrão 5000):

```text
STREAM_IMU START rate_hz=2
STREAM_IMU STOP session_id=<id>
STREAM_IMU PING session_id=<id>
```

Adicionar uma capability `stream_imu=1`/`stream_schema=1` ao anúncio compatível com os clientes atuais. Não mudar sem necessidade HELLO 3 nem o schema de CAPTURE. Validar os parsers existentes, alguns deles estritos. O monitor deve recusar firmware sem capability, com mensagem clara.

Sequência:

1. Conectar, ler HELLO/META e consultar estado/configuração. A sessão pertence à conexão TCP.
2. START valida parâmetros e exclusão mútua. Rejeitar se houver captura, calibração, reset pendente, restauração pendente ou sessão de outro dono. Bloquear também comandos conflitantes recebidos pela serial durante streaming; STATUS permanece permitido.
3. Capturar snapshot do estado prévio **antes de qualquer alteração**: relatórios habilitados, intervalos/configurações de feature relevantes, controles de calibração e política DCD conhecidos. Não substituir esse snapshot pelos defaults de compilação. Recusar START se não for possível garantir a restauração do que será alterado.
4. Habilitar os cinco relatórios, estabelecer caches por relatório e confirmar as configurações pelo mecanismo SH-2 disponível. Diferenciar envio aceito de confirmação do sensor. Ter timeout e rollback se a configuração falhar parcialmente.
5. Responder START aceito/pronto, entregar um único STREAM_META e o cabeçalho dos dados, depois publicar snapshots a 2 Hz. Não indicar READY antes de concluir a configuração; relatórios sem primeira amostra podem aparecer vazios.
6. STOP interrompe novas publicações, restaura o snapshot e só então responde `# OK STREAM_IMU STOP session_id=... restored=1`. Deve ser idempotente para a última sessão da conexão. Cliente deve tolerar dados já em trânsito enquanto aguarda o ACK final.

Máquina de estados sugerida: `IDLE → CONFIGURING → STREAMING → RESTORING → IDLE`, com falha de restauração explícita e bloqueio de operações conflitantes até recuperação. Não reportar `restored=1` se houve timeout ou falha. Falhas de sessão/configuração devem ser mostradas uma vez; a regra de silêncio aplica-se aos erros pontuais de amostra.

Restauração também deve ocorrer ao desconectar, perder Wi-Fi, expirar a sessão ou falhar o envio. `close_client()` deve iniciar uma restauração cooperativa que continue sendo executada mesmo sem socket, não apenas limpar um flag. Usar `try/finally` no cliente para enviar STOP em saída normal, Ctrl+C, SIGTERM e erro de gravação, com espera limitada pelo ACK. Não é possível enviar STOP após SIGKILL ou perda física da rede: o firmware deve resolver esse caso sozinho.

Heartbeat sugerido: PING a cada 5 s, timeout de 15 s. O servidor atual expira clientes após 60 s sem comando recebido; adaptar conscientemente esse controle, pois streaming de saída não comprova que o cliente continua consumindo. Priorizar leitura de STOP/PING e respostas de controle para que publicações não causem starvation.

Usar I/O não bloqueante e filas limitadas. Se o cliente atrasar, descartar snapshots ainda não iniciados, preservando o mais recente; nunca truncar uma linha parcialmente enviada nem intercalar controle no meio dela. Não bloquear o polling da IMU ou acumular fila ilimitada. O buffer TX atual tem 2048 bytes: verificar os limites reais do formato escolhido e testar fragmentação TCP.

Em reset inesperado da IMU, invalidar a sessão/epoch e interromper o fluxo com um aviso único. Não misturar amostras anteriores com posteriores nem reiniciar silenciosamente a mesma sessão. Restaurar o perfil anterior na medida possível após recuperação e informar o resultado.

**Limite da restauração:** restaurar as configurações alteradas não desfaz a evolução interna da fusão/calibração dinâmica durante a exposição. Não prometer rollback dos estados estimados e não tentar obtê-lo apagando DCD ou reiniciando a IMU. Não alterar persistência como efeito colateral do monitor.

## 6. Amostras, atualização independente e transporte

Manter cache independente para Raw, Uncalibrated, Calibrated, RV e GRV, com geração/sequência, recepção monotônica, epoch e validade. As amostras SH-2 são assíncronas: um snapshot de rede **não representa aquisição simultânea**.

Para cada relatório, só substituir o cache depois de validar integralmente a nova amostra. Erro I²C/decoder, duplicata ou ausência não deve sobrescrever valor bom com zero/NaN. Status de calibração baixo não é, por si só, erro de transporte ou motivo para esconder Raw/Uncalibrated — essa condição faz parte da observação.

Formato recomendado: linhas CSV com prefixos de controle `#`, cabeçalho próprio de streaming e versão própria. Separar envelope de transporte de dados científicos simples. Por linha, transmitir:

- `session_id`, `seq`, `device_ms`, `updated_mask` e idades dos cinco grupos em milissegundos (vazio antes da primeira amostra).
- As 17 grandezas: 9 componentes magnéticas, 6 ângulos RV/GRV e 2 azimutes magnéticos.
- Validade/novidade granular suficiente para os dois azimutes derivados e singularidades angulares; definir bits estáveis e documentar o schema.

O envelope permite que o cliente distinga valor retido de nova medição. Não é necessário gravar esse envelope inteiro no CSV científico. A serialização exata deve ser especificada no documento de operação e compartilhada pelos testes de firmware/parser.

Linha bem formada com falha de um grupo válido estruturalmente não deve impedir atualização dos outros grupos. Linha truncada/desalinhada/oversized é descartada integralmente: não tentar recuperar colunas por adivinhação. Sequências e IDs devem impedir que uma linha de sessão anterior atualize a atual.

## 7. Terminal e interface de execução

Criar `monitor-imu` em `docker/compose.yaml`, estendendo o padrão do serviço `status`, mesma imagem Python, rede host, UID/GID, volume `/data`, `stdin_open: true` e `tty: true`. O serviço deve usar `python scripts/monitor_imu.py` como entrypoint. Não depender do compose de experimentos ROS.

Interface proposta:

```text
--host / LAB_HOST                  endereço da trena
--port / LAB_PORT                  padrão 5000
--timeout / LAB_TIMEOUT            timeout de operações, não duração da sessão
--rate-hz                         padrão 2; faixa inicial 0.2..5
--save true|false                 padrão false
--output-dir                     padrão /data no Docker
--notes                          descrição livre das condições
```

Exemplos previstos (a implementar), executados da raiz do clone:

```sh
mkdir -p docker/data
docker compose -f docker/compose.yaml build monitor-imu
LAB_HOST=192.168.1.100 docker compose -f docker/compose.yaml run --rm monitor-imu --rate-hz 2 --save false
LAB_HOST=192.168.1.100 docker compose -f docker/compose.yaml run --rm monitor-imu --rate-hz 2 --save true --notes "Mesa; trena fixa; aproximação e retirada do ímã"
```

Painel fixo, atualizado no mesmo lugar, sem rolar uma linha para cada leitura:

```text
MM1 IMU | 2 Hz | gravação: não | Ctrl+C para encerrar
Campo                  X           Y           Z       Unidade
Raw                    ...         ...         ...     ADC
Uncalibrated           ...         ...         ...     µT
Calibrated             ...         ...         ...     µT

Orientação             Azimute     Inclinação  Roll    Unidade
RV (magnético)         ...         ...         ...     graus
GRV (arbitrário)        ...         ...         ...     graus
Azimute Raw indicativo: ...    Azimute Uncalibrated: ...
```

Antes da primeira leitura válida, mostrar `—`. Se uma aquisição falhar, **reter somente os valores afetados**, mantendo os demais atualizados. Não imprimir erro a cada evento, não piscar mensagens e não substituir valor retido por erro. Um indicador discreto de idade/última atualização por grupo é aceitável para não apresentar leitura congelada como recente, mas não um histórico de falhas.

Restaurar cursor/modo de terminal ao sair. Em stdout não TTY, fornecer saída simples sem escapes, ainda sem spam de erros de aquisição. Interrupção de conexão/sessão pode gerar um único aviso claro.

## 8. Salvamento opcional e metadados

`--save false`: nenhum arquivo de sessão ou dados. `--save true`: criar diretório exclusivo por execução (UTC e identificador), sem sobrescrever anteriores, contendo:

1. `metadata.json`: escrito **uma única vez no início**, depois de receber STREAM_META e antes de gravar amostras. Não repetir metadados por linha nem reescrever ao final.
2. `samples.csv`: cabeçalho único e somente dados simples durante a execução.

Metadados mínimos, alinhados aos experimentos existentes:

- Identificador de sessão, horário inicial UTC, comando/configuração do cliente, versão/commit do cliente quando disponível e notas do operador.
- Host/porta, HELLO/META relevante, boot_id, firmware/build/produto da IMU e epoch inicial.
- Relatórios/IDs/unidades, intervalos pedidos e confirmados, taxa TCP e versão do schema.
- Configuração prévia e configuração da sessão, máscara de calibração e política DCD; distinguir informação lida/confirmada de desconhecida, sem inventar estados.
- Convenções dos ângulos, quaternion wxyz, frente +X, ENU RV, GRV arbitrário, ausência de correção angular adicional, inclinação do feixe (campo pitch da produção).
- Método/mapeamento do azimute Raw, compensação de inclinação por GRV, limites de idade, tolerâncias de validade e política de retenção/gravação.
- Condições informadas em `--notes` (montagem, presença/distância/orientação do ímã etc.). Não inventar condições não fornecidas nem gravar senha Wi-Fi/segredos.

Cabeçalho científico proposto:

```csv
elapsed_s,mag_raw_x_adc,mag_raw_y_adc,mag_raw_z_adc,mag_uncal_x_uT,mag_uncal_y_uT,mag_uncal_z_uT,mag_cal_x_uT,mag_cal_y_uT,mag_cal_z_uT,rv_azimuth_deg,rv_inclination_deg,rv_roll_deg,grv_azimuth_deg,grv_inclination_deg,grv_roll_deg,mag_raw_azimuth_deg,mag_uncal_azimuth_deg
```

`elapsed_s` é tempo monotônico do PC relativo ao início; não afirmar sincronização com o instante físico do sensor. Uma linha por snapshot TCP aceito. **Na tela reter o último valor; no CSV deixar vazio o campo sem atualização válida naquele snapshot**, para não fabricar repetição de medidas novas. Não preencher retroativamente nem registrar erros por amostra. Essa diferença de política deve constar em metadata e README; mantém o arquivo simples sem esconder lacunas.

Usar flush periódico limitado (por exemplo, 1 s) e ao sair; cabeçalho e metadata antes dos dados. Falha de disco deve encerrar a gravação/sessão de forma controlada, enviar STOP e informar uma vez; não continuar fingindo que está salvando. Metadados são constantes mesmo que o término seja inesperado; nenhum status final fictício deve ser inserido no arquivo inicial.

## 9. Etapas de implementação

1. Conferir a base atual e os contratos dos parsers; escrever/ajustar testes das convenções e da sessão.
2. Acrescentar Raw/Uncalibrated ao adaptador e confirmação dos intervalos; expor caches independentes, sem romper a API usada pelos experimentos.
3. Implementar os cálculos angulares e testes de referencial. Reutilizar helper comum onde viável sem alterar o comportamento de produção/CAPTURE.
4. Implementar `stream_service`, snapshot de configuração, exclusão mútua, rollback e restauração cooperativa.
5. Integrar START/STOP/PING, metadata inicial, serialização, timeout e backpressure ao TCP existente.
6. Implementar cliente, painel, gravação opcional e finalização por sinais; acrescentar serviço compose.
7. Atualizar documentação operacional e executar regressões/build. Separar validação simulada de validação física pendente.

## 10. Verificação e critérios de aceitação

### Testes automatizados necessários

- Decoder/caches com Raw/Uncalibrated/Calibrated/RV/GRV intercalados, duplicatas, valores inválidos e falha em apenas um grupo.
- Frente +X: quaternion identidade → azimute 90°, inclinação 0°, roll 0°; rotação +90° em Z → azimute 0°. Pitch Euler +30° deve produzir inclinação exibida -30°; frente elevada 30° deve exibir +30°. Repetir para RV e GRV e verificar que nenhuma correção angular da produção é aplicada.
- Azimute magnético nivelado: vetor +X → 0°, -Y → 90°, -X → 180°, +Y → 270°. Testar 0°/360° e quadrantes.
- Compensação de inclinação com vetores sintéticos de campo e rotações conhecidas; alterar somente yaw do GRV não muda o azimute magnético calculado para o mesmo vetor/vertical. Testar transformação fixa dos eixos Raw, dependências antigas e singularidades.
- START/STOP repetido, BUSY via TCP/serial, configuração parcialmente aplicada, rollback e restauração de intervalos não padrão.
- Desconexão sem STOP, heartbeat perdido, reset IMU, envio parcial, cliente lento e STOP durante transmissão. Nenhuma confirmação falsa de restauração.
- Servidor simulado com fragmentação/concatenação de linhas, schema incompatível, grupo inválido e demais grupos atualizando, sessão incorreta e ausência de dados.
- `--save false` não cria arquivos; `true` cria metadata uma vez e CSV somente de valores/tempo, com lacunas corretas. Ctrl+C/SIGTERM/erro de disco liberam terminal e tentam STOP.
- Clientes status, calibração e experimentos anteriores continuam funcionando após parar o monitor.

Executar os testes nativos existentes (`sh tests/lab/run.sh`), testes Python conforme `docker/README.md`, validação `docker compose -f docker/compose.yaml config` e build PlatformIO do ambiente `mm1_p4_lab`. Confirmar os comandos vigentes no clone; não apresentar builds/testes não executados como aprovados.

### Aceitação em bancada

- Painel responde a 2 Hz por mais de 60 s sem cair pelo timeout antigo, sem fila crescente nem linhas de erro a cada falha transitória.
- Aproximar/retirar ímã com trena fixa altera Raw/Uncalibrated quando acima do ruído; não exigir que o calibrado apague a alteração nem presumir o comportamento do RV.
- Verificar os azimutes em orientações conhecidas e inclinadas, particularmente o mapeamento Raw e o sinal de pitch. Não chamar o azimute Raw de referência calibrada.
- Produzir falha de um relatório em simulação e comprovar retenção independente na tela/lacuna no CSV; verificar recuperação de conexão em bancada.
- Registrar configurações antes/depois de STOP e desconexão abrupta. Intervalos/habilitações temporários devem ser restaurados, e CAPTURE_IMU e calibração devem voltar a funcionar.
- Não depender de movimento do braço, gravação DCD, reset manual ou regravação de firmware para sair normalmente do streaming.

## 11. Prompt para usar no clone de destino

> Implemente a feature descrita integralmente em `docs/PLANO_MONITOR_STREAMING_IMU.md` neste clone do mm1-black. Primeiro confira as instruções locais e o código atual, pois o plano foi escrito em outra revisão. Crie o serviço Docker monitor-imu, o modo de streaming TCP no firmware de laboratório, os relatórios e cálculos definidos, a retenção independente de valores, a gravação opcional com metadata inicial único e a restauração de configuração no encerramento/perda de conexão. Preserve os comandos/experimentos existentes. Use a frente +X e as convenções de produção explicitadas no plano; exiba a inclinação usada nos tiros da produção em vez do pitch Euler, não incorpore g_azimuth_offset_deg nem qualquer correção angular adicional e não invente o mapeamento Raw. Execute os testes/builds disponíveis e documente separadamente o que depende de bancada. Entregue código, testes e instruções de uso; não se limite a propor outro plano.
