# Cliente de bancada: STATUS via TCP

Este diretório contém o ambiente Python do segundo incremento. Ele consulta o servidor do firmware `mm1_p4_lab`: recebe `HELLO` e `META`, envia `STATUS` e mostra a resposta completa. Laser, IMU, `CAPTURE`, CSV e movimentação do manipulador pertencem às próximas etapas.

## Testar a placa

Requisitos: Docker Engine e Docker Compose no Linux, com acesso ao daemon, e a placa ligada e conectada à rede. O PC precisa alcançar o IP mostrado na serial. Todos os comandos abaixo partem da **raiz do repositório**.

```sh
docker compose -f docker/compose.yaml build status
docker compose -f docker/compose.yaml run --rm status --host 192.168.1.123
```

Substitua `192.168.1.123` pelo **IPv4 real da placa**. A porta padrão é 5000; para ajustá-la ou mudar o timeout:

```sh
docker compose -f docker/compose.yaml run --rm status --host 192.168.1.123 --port 5000 --timeout 5
```

Exemplo de saída (valores ilustrativos):

```text
# HELLO MM1LAB 2 stage=network_only boot_id=0123456789abcdef connection_id=1
# META capture=unavailable commands=STATUS
# OK STATUS stage=network_only wifi=CONNECTED ip=192.168.1.123 rssi_dbm=-50 tcp=LISTENING port=5000 hosted=1 sensors=NOT_IMPLEMENTED attempt=1 disconnect_reason=0 uptime_ms=1234
```

A execução termina após uma resposta válida. Não há espera por `DONE` nem tentativa de captura. Feche outros clientes TCP antes do teste: o firmware aceita apenas uma conexão por vez.

## Configuração e registro opcional

Argumentos de linha de comando têm prioridade sobre as variáveis de ambiente. Para guardar os parâmetros:

```sh
cp docker/.env.example docker/.env
```

Edite `docker/.env` com o IP, porta e timeout. Para salvar resultados, configure `LAB_STATUS_LOG=/data/status.jsonl`. Ajuste `LAB_UID` e `LAB_GID` com os números retornados por `id -u` e `id -g`, para que o contêiner tenha permissão de escrita em `docker/data`. Então execute:

```sh
docker compose --env-file docker/.env -f docker/compose.yaml run --rm status
```

Também é possível salvar diretamente sem `.env` (o Compose usa UID/GID 1000 por padrão):

```sh
LAB_UID=$(id -u) LAB_GID=$(id -g) docker compose -f docker/compose.yaml run --rm status --host 192.168.1.123 --log /data/status.jsonl
```

Cada consulta acrescenta uma linha JSON ao arquivo `docker/data/status.jsonl`, preservando registros anteriores. O registro inclui destino, sucesso/falha, erro quando houver, horários UTC de início/fim do PC e duração medida com relógio monotônico. Em caso de sucesso, inclui também as três linhas recebidas e seus campos. São horários da transação no PC, não instantes de medição dos sensores. O relógio vem do host; não há NTP no cliente nem no ESP.

O volume mantém os registros após `--rm`. `.env` e os dados gerados ficam fora do Git e do contexto de build. A imagem não contém credenciais Wi-Fi. Sem `--log` ou `LAB_STATUS_LOG`, a saída aparece somente no terminal.

## Rede e limites

O Compose usa **`network_mode: host`**, sem bridge adicional nem publicação de portas. O ambiente de referência é Docker Engine no Linux; o contêiner compartilha as interfaces e rotas do PC. Não usa `privileged`, USB ou acesso ao socket Docker. Rotas, firewall e isolamento do AP ainda precisam permitir a comunicação. Veja a [documentação da rede host](https://docs.docker.com/engine/network/drivers/host/).

O cliente aceita IPv4 numérico para evitar uma espera de DNS fora do timeout. O timeout aplica-se separadamente à conexão, ao envio e à leitura de cada linha completa; bytes chegando aos poucos não reiniciam o prazo da linha. Cada linha recebida pode ter até 1024 bytes de conteúdo, com LF ou CRLF. Os cumprimentos, a versão 2 do protocolo, a etapa `network_only` e os campos de `STATUS` são verificados. Os estados informados pelo firmware são preservados, sem convertê-los em uma avaliação dos sensores.

Código de saída:

| Código | Significado |
| --- | --- |
| 0 | Resposta de protocolo válida e registro salvo, se solicitado. Não é uma validação dos sensores. |
| 1 | Falha de conexão, timeout, EOF, protocolo inesperado ou erro de gravação. |
| 2 | Configuração/argumento inválido ou IP ausente. |
| 130 | Consulta interrompida pelo usuário. |

Não há reenvio automático. Para consultar novamente, execute o comando de novo. Em caso de conexão recusada, confira porta e `LISTENING` na serial; em timeout, confira IP, rede e firewall. Se o registro falhar, confira UID/GID e permissões em `docker/data`.

## Testes sem a placa

Depois de construir a imagem, execute a suíte com servidores TCP simulados em loopback:

```sh
docker compose -f docker/compose.yaml run --rm --entrypoint python status -m unittest discover -s tests -v
```

Ela verifica a troca real por sockets, leitura fragmentada e agrupada, CRLF, EOF nas três fases, timeouts, limite de linha, mensagens inválidas, códigos de saída e persistência do JSONL. Não acessa a placa nem o manipulador.

Para experimentar manualmente, abra dois terminais. No primeiro:

```sh
docker compose -f docker/compose.yaml run --rm --entrypoint python status tests/mock_server.py --port 15000
```

No segundo:

```sh
docker compose -f docker/compose.yaml run --rm status --host 127.0.0.1 --port 15000
```

O simulador aceita uma consulta e encerra; aguarda conexão por no máximo 60 s. Seus dados são fictícios, incluindo `port=5000` na resposta de exemplo. Se 15000 estiver ocupada no PC, escolha outra porta nos dois comandos.

Opcionalmente, com Python 3.12 instalado no PC, os mesmos scripts funcionam sem Docker:

```sh
python3 docker/scripts/status.py --host 192.168.1.123
python3 -m unittest discover -s docker/tests -v
```

## Validação na placa real

O usuário executou `docker compose -f docker/compose.yaml run --rm status`, com o destino já configurado, e recebeu `HELLO`, `META` e `# OK STATUS` do firmware na placa. A resposta informou `wifi=CONNECTED`, `ip=192.168.0.10`, `rssi_dbm=-39`, `tcp=LISTENING`, `port=5000` e `hosted=1`. A saída completa está registrada em [LAB_WIFI_P4.md](../docs/LAB_WIFI_P4.md#resultado-na-placa-real).

O resultado confirma a comunicação do cliente Docker com o servidor embarcado e complementa os 14 testes automatizados e a verificação de persistência com o simulador. `sensors=NOT_IMPLEMENTED` e `capture=unavailable` são esperados no firmware atual. Esse teste não exercitou aquisição de sensores nem reconexão após perda de rede. Use sempre o IP atual da placa; o endereço observado pode mudar com DHCP.

## Evolução

A imagem fixa Python `3.12.14-slim-bookworm`; o cliente e os testes usam somente a biblioteca padrão. `requirements.txt` registra essa ausência de dependências externas e receberá versões fixadas quando necessário. Após editar scripts ou dependências, execute novamente o build.

O contexto de build é exclusivamente este diretório. As próximas etapas acrescentarão captura, dados persistentes e análise no mesmo ambiente. `external/manipulador/` permanece uma localização proposta para o futuro submódulo; ele ainda não foi adicionado e não é necessário para `STATUS`. A rede host será mantida nessa integração.
