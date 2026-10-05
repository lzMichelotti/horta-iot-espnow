# Placas

Todas são ESP32 DevKit "DOIT V1" (clone, ponte USB-serial CH9102), chip ESP32-D0WD-V3 rev v3.1, flash de 4 MB. A identificação é pelo **número de série do CH9102**, que não muda com a porta USB.

**Etiqueta física** (fita crepe ou etiqueta adesiva no verso da placa, sem cobrir a antena do módulo): função + últimos 4 dígitos do serial + final do MAC AP, por exemplo `COORD · 2039 · AP …91:fd`. Assim a placa é identificada sem computador e o dado que vai no firmware dos nós (MAC AP do coordenador) fica à vista.

| Etiqueta | Função prevista | Serial CH9102 | MAC STA | MAC AP | Observações |
|---|---|---|---|---|---|
| COORD | Coordenador | `5AC9002039` | `88:57:21:70:91:fc` | `88:57:21:70:91:fd` | Reinicia a cada abertura da porta serial (circuito de auto-reset) |
| NÓ 1 | Nó sensor 1 | `5AC9001351` | `88:57:21:70:93:70` | `88:57:21:70:93:71` | Montado com AHT20 e sensor de solo |
| NÓ 2 | Nó sensor 2 / reserva | TODO | TODO | TODO | Terceira placa ainda não conectada |

Como os MACs foram lidos (três fontes concordam):

- esptool, durante a gravação (MAC base = STA);
- C++ no NÓ 1: `esp_read_mac(mac, ESP_MAC_WIFI_STA / ESP_MAC_WIFI_SOFTAP)`;
- MicroPython no COORD (quando ele ainda rodava MicroPython): `network.WLAN(IF_STA / IF_AP).config('mac')`, com a interface ativa.

O MAC AP é sempre o STA + 1: o ESP32 tem um MAC base gravado em eFuse na fábrica e deriva 4 endereços dele (STA = base, SoftAP = base + 1, Bluetooth = base + 2, Ethernet = base + 3). <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/misc_system_api.html>

**No ESP-NOW, os nós enviam para o MAC AP do coordenador: `88:57:21:70:91:fd`.**

## Ligações do NÓ 1

| Sensor | Sinal | Pino da placa | GPIO | Observação |
|---|---|---|---|---|
| AHT20 | SDA | D21 | 21 | Pino SDA padrão do `Wire` na variante da placa |
| AHT20 | SCL | D22 | 22 | Pino SCL padrão do `Wire` na variante da placa |
| AHT20 | VCC / GND | 3V3 / GND | — | |
| Solo capacitivo V1.2 | AOUT | D34 | 34 | ADC1_CH6, só entrada (funciona com Wi-Fi ligado) |
| Solo capacitivo V1.2 | VCC / GND | 3V3 / GND | — | precisa de alimentação regulada |
| Divisor 100k/100k | ponto médio | D35 | 35 | ADC1_CH7; hoje mede o 3V3, na etapa 5 mede a bateria |

Nenhum sensor usa pinos de strapping (0, 2, 5, 12, 15), ADC2 ou os pinos da flash (6–11). Esquema completo e cuidados de montagem em [`pinagem.md`](pinagem.md).

## Hardware da placa

| Item | Valor | Fonte |
|---|---|---|
| LED onboard | Azul, GPIO2, acende com nível alto (testado: `Pin(2).value(1)` deixou o LED aceso) | `LED_BUILTIN = 2` na variante `doitESP32devkitV1` do Arduino-ESP32, confirmado piscando o LED |
| Ponte USB-serial | WCH CH9102 (`1a86:55d4`) | ID USB |
| Módulo | TODO: ler a blindagem (WROOM-32E?) | foto pendente |
| Regulador 3,3 V | TODO: ler o código do componente | foto pendente |
| Número de pinos | TODO | contagem pendente |
