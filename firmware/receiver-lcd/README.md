# Receptor ESP-NOW com LCD

Firmware para um segundo ESP32-C3 que recebe o broadcast de bateria enviado pelo
gateway via ESP-NOW e mostra os principais valores em um LCD 16x2 I2C.

Ligação padrão:

- LCD VCC -> 5V ou 3V3, conforme o backpack do LCD
- LCD GND -> GND
- LCD SDA -> GPIO8
- LCD SCL -> GPIO9
- Botão de página -> GPIO4 e GND

Configurações padrão em `platformio.ini`:

- Canal ESP-NOW: `1`
- Endereço I2C do LCD: `0x27`
- Tamanho do LCD: `16x2`
- Botão de troca de página: `GPIO4` com pull-up interno

Leitura do LCD:

- Página 1: SOC e potência em números grandes com `LCDBigNumbers`.
- Página 2: tensão, corrente, potência e SOC.
- Página 3: comparação entre SOC do BMS e SOC estimado pela tabela LiFePO4
  48V da Jackery. Essa estimativa por tensão só é confiável com bateria em
  repouso.
- Página 4: temperatura e estado de carga/descarga.
- Página 5: falhas e idade da última leitura.
- Se nenhum pacote novo chegar por mais de 10 segundos, a linha 2 mostra
  `Link OLD` na página 5, e a página principal marca `!`.
- O botão de página deve ligar `GPIO4` ao `GND`; o firmware usa `INPUT_PULLUP`,
  interrupção por mudança de estado e debounce não bloqueante no estilo da
  `EasyButtonAtInt01`.
- O LCD usa `Wire.setPins()` no ESP32-C3 para manter SDA/SCL configuráveis. A
  `SoftI2CMaster` é suportada pelo adapter da `LCDBigNumbers` em AVR, mas não é
  ativada neste alvo ESP32-C3.

Se o LCD acender mas não mostrar texto, tente trocar `LCD_I2C_ADDR` para
`0x3F`. O canal ESP-NOW precisa ser igual ao `ESP_NOW_WIFI_CHANNEL` do firmware
transmissor. O formato binário do pacote é compartilhado por
`../../shared/espnow_battery_packet.h`.

Compilar e fazer upload:

```sh
pio run -e esp32-c3-lcd-receiver
pio run -e esp32-c3-lcd-receiver -t upload
pio device monitor -b 115200
```

Teste local das telas do LCD, sem depender do gateway ESP-NOW:

```sh
pio run -e esp32-c3-lcd-pages-test
pio run -e esp32-c3-lcd-pages-test -t upload
pio device monitor -b 115200
```

Esse modo gera leituras falsas a cada segundo para testar a tela principal,
as páginas secundárias e o botão em `GPIO4`.

Se mais de um ESP32 estiver conectado, liste as portas primeiro:

```sh
pio device list
```

Depois faça upload explicitamente para a porta do receptor:

```sh
pio run -e esp32-c3-lcd-receiver -t upload --upload-port COM6
```

Se o display continuar mostrando `Sem dados`, confirme que transmissor e
receptor usam o mesmo `ESP_NOW_WIFI_CHANNEL`.
