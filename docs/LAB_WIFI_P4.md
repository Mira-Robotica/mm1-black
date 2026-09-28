# Primeiro incremento P4: Wi-Fi, ping e diagnóstico TCP

O ambiente `mm1_p4_lab` compila somente a entrada de laboratório, a rede e a configuração SDIO do ESP32-C6. Ele é o ambiente padrão do projeto. Os alvos `mm1_p4` e `denky32` continuam selecionáveis; o `src/main.cpp` da aplicação original foi preservado e a entrada de laboratório fica em `src/lab/main.cpp`.

Esta etapa não inicializa laser ou IMU. Display, touch, áudio, SD, BLE, portal web e atualização pela rede não são iniciados. O backlight (GPIOs 33 e 26) e o amplificador (GPIO 53) ficam em nível baixo.

## 1. Configurar a rede

Edite `include/lab_config.h`, já criado localmente e ignorado pelo Git. Em outro checkout, copie `include/lab_config.example.h` para esse caminho.

```cpp
#define LAB_WIFI_SSID "nome_da_rede"
#define LAB_WIFI_PASSWORD "senha_da_rede"
```

Use uma rede de **2,4 GHz** compatível com o ESP32-C6, com DHCP e comunicação permitida entre o PC e o dispositivo. Senha vazia seleciona uma rede aberta. Esta configuração não implementa autenticação Enterprise ou portal cativo. Não é necessário acesso à internet.

O firmware aceita as credenciais apenas desse arquivo; não reutiliza as credenciais gravadas pela UI anterior. A senha não é impressa na serial/TCP, mas faz parte do binário compilado. Com SSID vazio, a serial informa `CONFIG_REQUIRED`; preencha, recompile e grave novamente.

Os demais parâmetros locais têm padrões: hostname `mm1-p4-lab`, TCP 5000, timeout de conexão/DHCP de 20 s e intervalo entre tentativas de 10 s. O hostname é enviado ao DHCP; o teste por IP não depende de resolução de nomes ou mDNS.

## 2. Compilar, gravar e abrir a serial

No terminal do PlatformIO, a partir da raiz:

```sh
pio run -e mm1_p4_lab
pio run -e mm1_p4_lab -t upload
pio device monitor -e mm1_p4_lab -b 115200
```

Se `pio` não estiver no PATH deste computador, use `/home/pedro/.platformio/penv/bin/pio`. Se houver várias portas, informe `--upload-port /dev/ttyUSB0` na gravação e `--port /dev/ttyUSB0` no monitor, ajustando a porta real. Use a conexão UART/USB de programação da placa; a definição atual mantém `ARDUINO_USB_CDC_ON_BOOT=0`.

A gravação substitui a aplicação em execução pelo teste de laboratório. Não é necessário apagar toda a flash ou atualizar o C6 para simplesmente experimentar esta etapa. Feche o monitor antes de uma nova gravação. Compile um ambiente por invocação.

## 3. Testar ping

Após a conexão, a serial mostra algo como:

```text
[WiFi] CONNECTED IP=192.168.1.123 mask=255.255.255.0 gateway=192.168.1.1 RSSI=-50 dBm
[LAB] Test from the PC: ping 192.168.1.123
[LAB] TCP port=5000 LISTENING (STATUS)
```

Os valores acima são ilustrativos. Use o **IP mostrado pela sua placa**:

```sh
ping -c 5 192.168.1.123
```

No Windows, use `ping -n 5 192.168.1.123`. O lwIP responde a ICMP automaticamente; não há um comando de ping no protocolo TCP. Não é necessário NTP.

## 4. Consultar STATUS

Na serial a 115200, envie `STATUS` seguido de Enter, com terminação LF ou CRLF. Isso funciona inclusive sem Wi-Fi e informa conexão, IP, RSSI, estado Hosted, tentativas e motivo numérico da última desconexão.

Pelo PC, conecte-se à porta 5000 com `nc IP 5000` e digite `STATUS`, ou use Python:

```python
import socket

with socket.create_connection(("192.168.1.123", 5000), timeout=5) as sock:
    sock.settimeout(5)
    stream = sock.makefile("rb")
    print(stream.readline().decode().strip())  # HELLO
    print(stream.readline().decode().strip())  # META
    sock.sendall(b"STATUS\n")
    print(stream.readline().decode().strip())
```

A resposta começa com `# OK STATUS stage=network_only`. `CAPTURE` responde `# ERR NOT_IMPLEMENTED`: sensores, lotes e CSV serão implementados nos próximos incrementos. Ainda não há cabeçalho CSV nem marcador `DONE`.

Aceita um cliente por vez, linhas de até 128 caracteres, comandos fragmentados e múltiplas linhas. Clientes adicionais são desconectados; uma conexão sem tráfego de entrada por 60 s é fechada. O envio é não bloqueante e uma resposta presa por 2 s encerra a conexão.

## 5. Diagnóstico e reconexão

- `CONFIG_REQUIRED`: SSID ausente ou comprimento de credencial inválido no arquivo local.
- Timeout de conexão: confira SSID/senha, faixa de 2,4 GHz e DHCP. O firmware aguarda 10 s e tenta novamente. `disconnect_reason` é o código de motivo do driver, não uma indicação de falha do laser/IMU.
- IP obtido, mas ping/TCP falham: confira IP atual, rota do PC, isolamento de clientes da rede/AP, VLAN e regras de ICMP/TCP.
- Falha antes de obter IP, no ESP-Hosted: confira alimentação e comunicação SDIO. Usar o firmware de fábrica do C6 fornecido pela Waveshare, com o qual conexão Wi-Fi e ping já foram confirmados nesta placa. Os pinos são CLK 18, CMD 19, D0–D3 14–17 e RESET 54; a inicialização não depende de BLE nem do painel.
- Ao perder Wi-Fi/IP, o firmware fecha a conexão TCP antiga. Ao recuperar a rede, imprime o IP e reabre o servidor. Pode ser necessário reconectar o cliente a outro IP atribuído pelo DHCP.

O código mantém o rádio em station nas retentativas, sem chamar `WIFI_OFF` ou apagar configurações NVS da aplicação. O timeout de 20 s cobre associação/DHCP após iniciar o driver; a inicialização física do ESP-Hosted possui seus próprios limites e recuperação no SDK. Falhas de transporte do C6 podem provocar recuperação/reinicialização pelo próprio framework e precisam de verificação em bancada.

O próximo incremento prevê empacotar o teste Python de `STATUS` em um contêiner autocontido em `./docker`, conforme o [plano de aquisição](PLANO_AQUISICAO_SENSORES_P4.md#incremento-2-contêiner-docker-e-teste-python-de-status). O Docker ainda não está implementado; o exemplo acima continua disponível para execução direta.

## Verificação desta implementação

Compilação confirmada com plataforma fixada em **pioarduino 55.03.312**, Arduino **3.3.12**, IDF **5.5.5**, variante **esp32p4_es** e CPU a **360 MHz**. A versão fixada corresponde ao framework instalado; não foi alterada a revisão da placa.

O ELF inclui `icmp_input` e não inclui símbolos da UI/LVGL/Display Panel, BLEDevice/inicialização NimBLE, biblioteca SD ou serviços de OTA da aplicação. O SDK retém rotinas de SDMMC para o transporte **SDIO do C6** e consulta de partição de boot; isso não significa inicialização do cartão SD ou de um serviço de atualização.

O usuário confirmou compilação, gravação, conexão Wi-Fi e ping em 28/09/2026. A consulta `STATUS` via Python, a reconexão e os demais critérios de bancada ainda precisam ser verificados.
