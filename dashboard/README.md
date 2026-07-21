# Solmar BMS dashboard

Dashboard web estatico para a equipe acompanhar a bateria quando nao esta perto
do barco. Ele visualiza os pacotes MQTT publicados pelo gateway
`esp32-s3-central-a7670`.

Abra `index.html` no navegador e conecte usando os valores padrao:

```text
Broker WebSocket: wss://broker.hivemq.com:8884/mqtt
Filtro de topico: solmar/bms/felicity-fla12171/#
```

O ESP publica no mesmo broker por MQTT TCP:

```text
Host: broker.hivemq.com
Porta: 1883
Topicos retidos:
solmar/bms/felicity-fla12171/readings/v1/<device_id>/<tipo>
solmar/bms/felicity-fla12171/location/v1
```

Tipos publicados atualmente:

- `battery_info`
- `cell_voltages`
- `charge_discharge`
- `version_info`

O pacote `location/v1` usa o schema `solmar.location.v1` e alimenta o mapa do
dashboard com a posicao real da central quando houver fix valido de GPS.

O dashboard tambem tem:

- modo claro/escuro salvo no navegador;
- grafico de teia de aranha para comparar as tensoes das celulas;
- botao `Minha posicao` para marcar a posicao do operador no mapa e calcular a
  distancia ate o barco.

Se trocar o broker ou o topico no firmware, ajuste os campos da pagina. Para
broker publico, prefira usar um prefixo de topico unico por instalacao para
evitar conflito com outros usuarios.
