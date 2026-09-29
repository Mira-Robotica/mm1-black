# Testes nativos do incremento 3

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
