# Testes nativos dos incrementos 3 e 4

Compilam as implementações reais de orientação, parser, polling UART, aquisição e CSV com relógio/UART/IMU simulados. Também compilam o driver real da IMU com GPIO e SH-2 simulados, nos modos lab e original. Não acessam a placa nem leem as credenciais locais: `stubs/lab_config.h` isola os parâmetros do ensaio. Com GCC C++17 e os sanitizers, a partir da raiz:

```sh
sh tests/lab/run.sh
```

Alternativa sem instalar o compilador no PC, usando o repositório apenas para leitura:

```sh
docker run --rm -v "$PWD:/work:ro" -w /work gcc:14.2.0 sh tests/lab/run.sh
```

O script trata warnings como erros, habilita AddressSanitizer/UndefinedBehaviorSanitizer e usa executáveis temporários removidos ao terminar. Os testes cobrem fórmula angular, singularidade, BCD/checksum, fragmentação, campos CSV, pedido inválido, cache antigo, falha de sensor, timeout, resposta tardia e cancelamento. O teste do driver verifica seleção de relatórios, qualidade fracionária, wrap/duplicata de sequência, reset, NACK versus leitura vazia, pacote de 384 bytes e limites de falha I²C/escrita.

O stub da aquisição testa a política de novidade/reset; o teste do driver exercita seus callbacks e HAL com sinais/tempo simulados. Nenhum deles valida o transporte elétrico ou o firmware do BNO. Esses caminhos também precisam de ensaio físico. O build PlatformIO verifica a integração com Arduino, driver real e biblioteca SH-2. Os testes TCP do cliente são executados pela [imagem Python](../../docker/README.md#testes-sem-a-placa).


O incremento 4 acrescenta `test_calibration.cpp` (máquina de estados e protocolo TCP/serial compartilhado, com sensores simulados) e `test_sh2_async.c` (comandos reais da biblioteca SH-2, codificação, ACKs, prazos, wrap e erro de envio). Inclui 4/5/6 posições, duplicatas/ordem/dono, qualidade oscilante ou antiga, timeout, desconexão, reset, SAVE único e restauração da máscara original. `test_imu_driver.cpp` cobre relatórios auxiliares, idades/duplicatas e reaplicação da política de autosave no reset.

Se LeakSanitizer não puder operar sob ptrace/sandbox, use `ASAN_OPTIONS=detect_leaks=0 sh tests/lab/run.sh`; ASan e UBSan continuam ativos. A persistência física de DCD, a efetividade do comando sem ACK que desabilita autosave e a carga do barramento precisam de [ensaio na placa](../../docs/LAB_CALIBRACAO_IMU_P4.md).


Em 02/10/2026, `test_sh2_boot.c` passou a testar a abertura SH-2 real com reset/anúncio em ordens diferentes, atrasos, ausência e timeout; `test_shtp_boot.c` verifica propagação da falha de abertura da HAL e liberação da instância. O teste do driver também confere SDA31/SCL30 e que NACK no endereço alternativo não apaga a etapa/código da falha no endereço principal.

`test_sh2_protocol.c` integra SH-2 e SHTP reais com uma HAL simulada: verifica o pacote F9, as quatro respostas F8, GET_CAL/F1 e relatórios RV/Game RV/magnético. Reproduz o timeout causado pela ausência de comprimentos no anúncio, cobre tabelas completas/parciais/ausentes, respostas insuficientes e truncadas, além dos contadores de diagnóstico. Este teste não usa os stubs de SHTP dos testes unitários anteriores.

O caso real `read_rc=-6`, seguido de `rc=0` na limpeza, agora tem regressão no teste de sessão: o erro original e seu diagnóstico permanecem, a máscara fica intacta e nenhum SET/SAVE é enviado. Os testes SH-2 verificam contagem de respostas, rejeição por comando/sequência e limpeza dos contadores ao iniciar a operação seguinte.
