# Incremento 5: SparkFun, I²C1 e reset da IMU

Implementado em 07/10/2026; estado atualizado em **13/10/2026**, revisão `fd0149c+mm1.4`. O usuário relatou melhora de estabilidade após corrigir a fiação e remapear SDA/SCL/NRST para **GPIO25/34/49**. O relato anterior de calibração do magnetômetro e Rotation Vector em **3/3** permanece como progresso funcional. Séries quantitativas de estabilidade e persistência DCD ainda não foram registradas; o usuário considera possível prosseguir com as próximas etapas.

## Resultado de bancada informado pelo usuário — 07/10/2026

O usuário realizou testes promissores com esta versão: a IMU respondeu bem, foi possível calibrar o magnetômetro e o índice de qualidade do Rotation Vector aumentou para **3/3**. Esse relato confirma progresso funcional na bancada após a primeira implementação, que falhava na sondagem. O índice 3/3 é a qualidade informada pela IMU; não constitui uma medição de precisão angular contra uma referência externa.

O usuário também informou que **nem tudo funcionou 100%** e solicitou salvar este estado do repositório antes de prosseguir. As falhas remanescentes não foram detalhadas neste relato. Não foram fornecidos novos logs, número de tentativas ou condições de alimentação que permitam concluir estabilidade, identificar a causa da falha anterior ou confirmar SAVE/restauração e persistência DCD após desligar/ligar. Este registro é um marco de progresso parcial, sem encerrar os critérios de aceite do plano.

## Relato de bancada e remapeamento — 13/10/2026

O usuário identificou **mau contato nos fios da linha I²C**. Também relatou que, por erro de montagem, ligou a alimentação do módulo BNO086 com polaridade invertida. Segundo ele, o módulo continuou funcionando após corrigir a ligação. Isso registra o funcionamento observado, sem estabelecer por medição a ausência de dano elétrico.

Após o incidente, os GPIOs **30, 31 e 32** apresentaram instabilidade e foram retirados de uso pelo usuário nesta montagem. Há suspeita de que tenham sido afetados pela inversão da alimentação; a relação causal e eventual dano permanente não foram confirmados. Na montagem anterior, GPIO31 era SDA, GPIO30 era SCL e GPIO32 era NRST.

A configuração atual está em [`src/board/p4/imu_sparkfun/config.h`](../src/board/p4/imu_sparkfun/config.h): **SDA=25, SCL=34, NRST=49**, com I²C1 de hardware a 100 kHz e reset físico conectado. Esse é o caminho efetivo no repositório, sem o segmento `bno08x/` citado no relato. O usuário percebeu maior estabilidade com essa ligação e considera possível retomar as próximas etapas. Não foram informados neste relato novos logs, contagem de partidas ou condições de alimentação para quantificar a melhora.

A espera após liberar NRST permanece em **150 ms**, conforme o ajuste de bancada do usuário, que também relatou melhora de inicialização ao testar esse tempo no reset nativo da biblioteca pela IDE Arduino. Isso não estabelece um tempo máximo de prontidão garantido nem permite atribuir a melhora isoladamente ao delay, pois houve mudanças na fiação e nos pinos.

O achado físico não elimina as correções de software para falhas de transporte e pacotes malformados. Elas foram preservadas nos commits de firmware e testes anteriores a este registro. A calibração do magnetômetro e o RV 3/3 relatados em 07/10 não são uma nova confirmação de SAVE/restauração ou persistência DCD após o remapeamento; essas verificações e as séries de estabilidade seguem como resultados a registrar nos próximos ensaios.

## Integração e pinagem

Somente `mm1_p4_lab` usa `src/board/p4/imu_sparkfun/p4_imu.cpp` e a biblioteca fornecida em `SparkFun_BNO08x_Arduino_Library-1.0.6`. O adaptador chama `BNO08x::begin()` com `Wire1`, usa a HAL da SparkFun e registra um callback SH-2 que recebe **todos** os relatórios de cada pacote. Isso conserva RV, Game RV e Magnetic Field mesmo quando chegam juntos; o getter da SparkFun guarda somente o último evento.

| Sinal/configuração | Laboratório |
| --- | --- |
| Controlador | HP I²C1, instância Arduino `Wire1` |
| SDA / SCL | GPIO25 / GPIO34, sem inversão ou varredura de outros pinos |
| Frequência | 100 kHz |
| Endereço | 0x4B, alternativa 0x4A se não houver ACK |
| NRST | GPIO49, ativo em nível baixo |
| INT | Não conectado; atendimento por polling |
| Buffer Wire/SparkFun | 32 bytes, como no padrão da SparkFun; a HAL recompõe os cabeçalhos repetidos de I²C |
| Timeout Wire | 50 ms por transação, padrão do core explicitado no adaptador |

Os parâmetros estão em [`config.h`](../src/board/p4/imu_sparkfun/config.h). O I²C0 permanece reservado ao touch/áudio em GPIO7/8. O executável de laboratório usa o driver `i2c_master` do Arduino 3.3.12/IDF 5.5.5, sem o driver legado do Display Panel. `mm1_p4` exclui explicitamente a biblioteca SparkFun e o adaptador; continua com o driver antigo, seu mapa de pinos e `lib_ignore = Wire`. Esse alvo original não incorpora o remapeamento e não corresponde à fiação atual da bancada. `denky32` já exclui toda a árvore `board/`.

## Revisão da biblioteca e alterações locais

A base é [SparkFun v1.0.6](https://github.com/sparkfun/SparkFun_BNO08x_Arduino_Library/releases/tag/v1.0.6), commit `fd0149c280b4c7420e2dea4d034587373a8ca8b9`. O SH-2 é o snapshot **incluído nessa revisão**, sem importar outra versão CEVA. O pacote não fornece uma revisão independente do upstream SH-2; não foi atribuída uma por suposição. O [manifesto SHA-256](../src/board/p4/imu_sparkfun/upstream-sha256.json) identifica os arquivos fornecidos pelo usuário antes das alterações. Os diagnósticos identificam a integração local como `fd0149c+mm1.4`.

A comparação mostrou que o `sh2.c` fornecido coincide com a base do driver antigo, descontadas as extensões `MM1_LAB`. Por isso, trocar apenas a pasta não elimina os problemas já reproduzidos. As mudanças ficaram limitadas a três arquivos da biblioteca, com a API cooperativa em arquivos do projeto:

- `SparkFun_BNO08x_Arduino_Library.cpp`: hooks de diagnóstico/reset; preenchimento do tempo exigido pela HAL; buffer de 32 bytes; validação do comprimento e canal dos cabeçalhos repetidos; diagnóstico limitado de cabeçalhos inválidos e eventos SHTP; identificação de leitura realmente vazia; rejeição de cabeçalho/escrita inválidos; erro negativo em falha de escrita. O retorno zero original faz o SHTP repetir indefinidamente. No lab, a abertura não envia soft reset: o adaptador já realizou o reset físico.
- `sh2.c`: inclusão de `sh2_state.inc` e `sh2_ops.inc` para as operações cooperativas; conservação da máscara de calibração de cinco bits; comprimentos conhecidos dos relatórios usados pelo lab e rejeição de mensagens truncadas; contadores de respostas; verificação de reset **e** anúncio de canal no boot; fechamento seguro de abertura incompleta e cópia limitada da string de versão. Somente Product ID recebeu timeout síncrono de 2 s.
- `shtp.c`: propaga falha da abertura da HAL e libera a instância, permitindo uma tentativa posterior; valida o anúncio TLV inteiro antes de aplicá-lo, com limites para campos e strings; limita a cópia da versão SHTP e a montagem de fragmentos; rejeita limites de transmissão que não comportam carga útil. Essas verificações evitam sobrescrita de memória e envio sem progresso com pacotes malformados.

Os testes reproduzem a necessidade dos comprimentos conhecidos quando o anúncio não contém a tabela de tamanhos. Essa política é compatível com a tabela fixa usada pelo [SH-2 da CEVA](https://github.com/ceva-dsp/sh2/blob/main/sh2.c), mas não substitui o snapshot da SparkFun por esse upstream. Cabeçalhos públicos, decoder, utilitários, exemplos e APIs gerais da biblioteca foram preservados.

`CAL_IMU` continua com um único proprietário das operações: GET/SET/SAVE enviam uma vez e são atendidos por `sh2_service()` no loop. Uma resposta de outro comando/sequência ou recebida depois do prazo não confirma sucesso. SAVE nunca é repetido automaticamente. O comando de desabilitar autosave continua **sem ACK definido**, e seu envio é registrado separadamente da confirmação de Save DCD.

## Revisão das temporizações

| Temporização | Decisão |
| --- | --- |
| Bit-bang: meio período 5 µs, stretching 50 ms e orçamento agregado de 100 ms | Removidos do executável lab; o controlador e Wire passam a realizar as transferências. |
| Instalação antiga do barramento: 20 + 50 ms; sondagens e survey de GPIO | Removidos. |
| Soft reset antigo: retry 50 ms e espera 500 ms | Removidos. Também não se usa o soft reset da SparkFun (retries de 30 ms e espera de 300 ms). |
| Reset físico | Pulso baixo de 10 ms; primeira sondagem aos 150 ms após liberar NRST. Em NACK, novas sondagens a cada rodada de 10 ms até completar 500 ms após a liberação; ACK encerra imediatamente essa fase. |
| Abertura SH-2 | Mantidos os **200 ms originais** para receber reset/anúncio, em vez dos 2 s acrescentados ao driver antigo. Timeout passa a ser falha explícita. |
| Product ID | 2 s: a operação original não tinha timeout. Necessário para ausência de respostas não travar o firmware. |
| GET/SET/SAVE | Prazo cooperativo configurado em `LAB_CAL_OP_TIMEOUT_MS`; removido o fallback global de 2 s que alterava todas as operações síncronas do SH-2. |
| I²C | 50 ms por transação Wire. Leituras usam blocos de até 32 bytes e podem exigir várias transações para recompor um pacote; não há orçamento agregado de 100 ms herdado do bit-bang. A leitura encerra na primeira falha. |

A seção 6.5.3 do [datasheet BNO08x v1.16](datasheets/BNO080_085-Datasheet_v1.16.pdf) especifica NRST mínimo de 10 ns, inicialização mínima de 90 ms e configuração típica de 4 ms. O pulso de 10 ms acompanha a escolha conservadora da própria SparkFun; a espera atual de 150 ms antecede a sondagem porque INT não está conectado e incorpora o ajuste empírico relatado pelo usuário. Esses números **não são um máximo garantido de boot do módulo**; devem ser confrontados com os ensaios repetidos.

O timeout de transação do Wire não é a configuração elétrica de clock stretching: o core mantém `scl_wait_us=0`, usando a seleção padrão do IDF. Não houve alteração do core/SDK para aumentar esse limite. Validar o comportamento real do BNO086 antes de justificar novos tempos. Não se conclui que a comunicação melhorou apenas porque o build passou.

## Reset e recuperação remota

No boot e em `IMU_RESET`, o adaptador encerra a instância `Wire1` antes de configurar novamente I²C1, invalida os caches, pulsa GPIO49 e identifica a IMU. Se `Wire1.end()` falhar, registra `I2C_END` e encerra a tentativa antes de reinicializar o barramento ou pulsar NRST. O encerramento explícito evita reaproveitar estado de uma tentativa anterior com falha; I²C0 não é reinicializado. O pino fica alto após o pulso. `BNO08x::begin()` recebe RST=-1 porque o GPIO já é controlado pelo adaptador; assim não há segundo pulso nem acesso ao sensor antes de liberar o reset. Há uma tentativa de abertura SH-2 por inicialização. Antes disso, somente a sondagem de endereço pode ser repetida dentro da janela de prontidão de 500 ms. Se um endereço responde mas o SH-2 falha, a etapa/código dessa falha são preservados.

Em reset espontâneo sinalizado pelo SH-2, os caches de captura/calibração e a operação pendente são invalidados imediatamente. Reaplicam-se RV a 50 Hz e autosave desabilitado, verificando os retornos. Falha deixa a IMU indisponível. O serviço de calibração encerra a sessão, preservando `SAVE_UNKNOWN` se uma gravação estava em andamento. Não há limpeza de DCD, SAVE ou retomada de calibração automáticos.

O comando adicional abaixo está disponível tanto por TCP quanto por serial:

```text
IMU_RESET
# OK IMU_RESET state=SCHEDULED
STATUS
```

Ele agenda **uma** tentativa de reset físico e reabertura, inclusive após falha de boot. Retorna `# ERR BUSY` se houver captura ou sessão de calibração ativa. Pode ser usado para recuperar uma restauração falha depois que a sessão terminou. O ACK confirma o agendamento; consultar `STATUS` para o resultado. Não há repetição de reset/abertura SH-2 ou reenvio automático de SAVE. Somente o ACK é sondado novamente dentro da janela limitada.

A inicialização da SparkFun é síncrona: durante essa tentativa manual, o loop pode ficar suspenso pelos prazos de boot/Product ID e pelas transações I²C. Por isso, o comando só é admitido ocioso; manter o timeout do cliente em pelo menos 5 s. As operações normais de calibração continuam cooperativas. Reset espontâneo durante uma sessão é tratado por polling, sem chamar novamente o boot síncrono.

`STATUS` e `META` acrescentam transporte, controlador, pinos, frequência, biblioteca e revisão. `STATUS` também mostra `imu_init`, `imu_init_rc`, `imu_hw_resets`, `imu_init_ms`, `imu_part`, `imu_fw` e `imu_build`. `imu_resets` conta os resets físicos solicitados e as notificações espontâneas; a notificação do próprio boot físico não é contada duas vezes. `imu_hw_resets` conta os pulsos no NRST configurado (GPIO49 na montagem atual). Os contadores de amostras, erros e resets são monotônicos durante o boot do P4.

## Verificações e aceite pendente

Verificações históricas de 07/10/2026 (não repetidas integralmente nesta atualização):

- Builds `pio run -e mm1_p4_lab` e `pio run -e mm1_p4` aprovados. Mapa do lab com uma cópia SH-2 da SparkFun e `i2c_master`; produção com o SH-2 antigo e sem Wire/SparkFun.
- `ASAN_OPTIONS=detect_leaks=0 sh tests/lab/run.sh`: aquisição, driver original, calibração, operações cooperativas, boot, protocolo SH-2/SHTP e integração SparkFun/Wire1/reset aprovados com ASan/UBSan.
- A integração nova usa o wrapper, SH-2, SHTP e decoder reais, simulando apenas Wire/GPIO/relógio e pacotes do sensor. Cobre pinagem, pulso, endereço alternativo, RV/Game/mag no mesmo pacote, qualidade fracionária, duplicatas, FIFO vazio versus erro, pacote de 384 bytes, ausência de sensor, timeout de boot/Product ID, falha de escrita/restauração e reset durante SAVE sem repetição.
- `python3 -m unittest discover -s docker/tests`: 35 testes aprovados em loopback.

Na placa, compilar/gravar pelo procedimento habitual e executar a série do [plano](PLANO_AQUISICAO_SENSORES_P4.md#incremento-5-reset-da-imu-driver-sparkfun-e-i²c-de-hardware): energização completa e reset somente do P4, USB e bateria, sempre registrando revisão, condições, número de tentativas, `STATUS`, duração e erros. Exercitar também `IMU_RESET` ocioso e recusa em BUSY. Confirmar captura, GET/SET/verificação, cancelamento/restauração e SAVE explícito. Comparar com as 11 tentativas anteriores, sem confundir teste simulado com estabilidade física ou boa calibração magnética.


## Primeiro ensaio da migração: sondagem sem confirmação

Registro histórico com SDA31/SCL30/NRST32, anterior ao diagnóstico físico e ao remapeamento de 13/10/2026. As conclusões abaixo refletem a evidência disponível naquela revisão.

O log enviado pelo usuário para `fd0149c+mm1.1` registra `init=NO_ACK rc=-4 ms=111`, com zero relatórios e sem identificação da IMU. O Wi-Fi conectou e `STATUS` respondeu. O erro anterior de consulta à versão do C6 não impediu esses serviços no ensaio. O usuário repetiu `IMU_RESET` após a rede estar conectada: novamente `NO_ACK`, 111 ms, agora `imu_hw_resets=2` e nenhum relatório. A falha não se restringiu à energização inicial. Como o comando pulsa NRST novamente, esse resultado não elimina a hipótese de prontidão posterior aos 100 ms de cada liberação. Não há evidência suficiente para atribuir a falha da IMU a alimentação, fiação, reset ou stretching.

Naquela revisão, `NO_ACK` era atribuído a **qualquer** falha de `Wire.endTransmission()`. `rc=-4` era `SH2_ERR_IO` sintetizado pelo adaptador, não o retorno original do Wire; os contadores de I/O não incluíam sondagens. Portanto, zero erros contabilizados não comprovava um barramento funcional. Os 111 ms incluem o pulso de 10 ms e a espera de 100 ms, seguidos de uma sondagem por endereço, sem janela adicional de prontidão.

A revisão `fd0149c+mm1.2` mantém o pulso e a primeira sondagem, mas aceita um ACK posterior dentro de 500 ms após liberar NRST. Esse valor é um limite de recuperação/diagnóstico escolhido para o ensaio, **não um tempo garantido pelo fabricante nem uma espera fixa adicionada a todo boot**. Cada transação permanece limitada a 50 ms; a última transação pode ultrapassar o fim da janela em até esse limite, mais overhead. Erro de controlador/timeout ou falha SH-2 encerra a inicialização sem repetição. Os timeouts de anúncios/Product ID e os comandos da calibração não mudaram.

A sondagem vazia do Wire usa `i2c_master_probe()` no IDF. Na [implementação 5.5.5](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_i2c/i2c_master.c), ela opera a 100 kHz e configura timeout de SCL de 20 ms; não usa a configuração posterior do dispositivo. Não foram alterados o core, esse timeout ou os pinos para tentar corrigir uma causa ainda desconhecida.

Novos campos por inicialização em `STATUS` e no log serial:

| Campo | Interpretação |
| --- | --- |
| `imu_probe_4b_rc`, `imu_probe_4a_rc` | Último retorno do Wire por endereço: 0 ACK, 2 NACK, 4 outro erro, 5 timeout; -1 não sondado. |
| `imu_probe_attempts` | Quantidade de sondagens nesta inicialização. |
| `imu_probe_ms` | Tempo após liberar NRST ao concluir a última sondagem. |
| `imu_probe_sda`, `imu_probe_scl` | Níveis lidos nos pads do P4 após a última sondagem, sem reconfigurar I²C. |
| `imu_rst_low`, `imu_rst_high` | Níveis lidos no GPIO32 durante a fase baixa e após liberação; esperado 0/1. Confirmam o pad do P4, não a continuidade elétrica até o módulo. |

`imu_init=I2C_PROBE_TIMEOUT` e `I2C_PROBE_ERROR` agora distinguem essas falhas de `NO_ACK`. Os contadores de tráfego SH-2 continuam separados. A entrada do GPIO32 é habilitada junto da saída para permitir medir o nível sem interromper o pulso.

Os testes nativos acrescentam sensor que começa a responder somente após 240 ms, sensor ausente durante toda a janela, erro/timeout de Wire sem repetição e limpeza dos diagnósticos na tentativa seguinte. Build do lab e testes nativos aprovados; o relato posterior confirma funcionamento da IMU, mas não identifica a causa da falha anterior nem comprova sua eliminação em todas as condições.


### Fechamento após a interrupção

Na retomada, foi corrigida uma pendência de indentação em `BNO08x::isConnected()` que gerava `-Wmisleading-indentation` no teste nativo com `-Werror`. O comportamento do método foi preservado, com bloco explícito para a falha. As oito suítes nativas com ASan/UBSan e o build `mm1_p4_lab` passaram novamente. O binário foi conferido para conter `fd0149c+mm1.2` e os novos campos de sondagem; o mapa de link mantém apenas a cópia SparkFun do SH-2 e o I²C de hardware. Os novos campos de STATUS continuam dentro dos 1024 bytes aceitos pelo cliente. O relato de bancada acima atualiza a validação para sucesso parcial; o aceite físico completo continua pendente.


## Proteções de software preservadas na revisão mm1.4

Além do encerramento explícito de `Wire1` antes da reabertura, a HAL recusa cabeçalhos e fragmentos incoerentes antes de entregá-los ao SH-2. O parser SHTP valida os TLVs antes de registrar canais ou copiar nomes; nomes maiores que o destino de 32 bytes ou sem terminador são rejeitados. A regressão nativa cobre o caso que antes permitia overflow na pilha, além de anúncios truncados, fragmentos excedendo o buffer e limites de transmissão sem espaço para payload. A correção da montagem não torna esses dados seguros para copiar sem validação.

Os diagnósticos incluem erros de protocolo nos contadores e limitam os detalhes de pacotes inválidos a quatro registros por inicialização. O log de boot informa tamanho do buffer e espera pós-reset. O core Arduino/ESP-IDF não foi alterado. Esses limites e a recuperação foram verificados em simulação; não constituem prova de eliminação de todas as falhas físicas vistas nos logs anteriores.

## Verificação do estado salvo — 13/10/2026

- Firmware/configuração salvos em `5b09ba9`; testes salvos em `9d5a2a2`, antes de registrar o novo relato de bancada.
- `ASAN_OPTIONS=detect_leaks=0 sh tests/lab/run.sh`: nove suítes aprovadas com ASan/UBSan, incluindo pacotes SHTP malformados e integração com SDA25/SCL34/NRST49.
- `/home/pedro/.platformio/penv/bin/pio run -e mm1_p4_lab`: aprovado em 13/10/2026. Nenhuma gravação ou medição na placa foi executada pelo assistente; os resultados físicos acima são relatos do usuário.
