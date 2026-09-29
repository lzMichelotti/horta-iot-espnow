# Placas

Todas são ESP32 DevKit "DOIT V1" (clone, ponte USB-serial CH9102), chip ESP32-D0WD-V3 rev v3.1, flash de 4 MB. A identificação é pelo **número de série do CH9102**, que não muda com a porta USB; a etiqueta física deve repetir esse nome.

| Etiqueta | Função prevista | Serial CH9102 | MAC STA | MAC AP | Observações |
|---|---|---|---|---|---|
| COORD | Coordenador | `5AC9002039` | `88:57:21:70:91:fc` | *(Passo 4)* | Reinicia a cada abertura da porta serial: usar `mpremote ... sleep 2` |
| NÓ 1 | Nó sensor 1 | `5AC9001351` | `88:57:21:70:93:70` | *(Passo 4)* | Montado com AHT20 e sensor de solo |

MAC STA lido pelo esptool durante a gravação.

## Ligações do NÓ 1

| Sensor | Sinal | Pino da placa | GPIO | Observação |
|---|---|---|---|---|
| AHT20 | SDA | D21 | 21 | Pino SDA padrão do `Wire` na variante da placa |
| AHT20 | SCL | D22 | 22 | Pino SCL padrão do `Wire` na variante da placa |
| AHT20 | VCC / GND | 3V3 / GND | — | |
| Solo capacitivo V1.2 | AOUT | D34 | 34 | ADC1_CH6, só entrada (funciona com Wi-Fi ligado) |
| Solo capacitivo V1.2 | VCC / GND | 3V3 / GND | — | |

Nenhum sensor usa pinos de strapping (0, 2, 5, 12, 15), ADC2 ou os pinos da flash (6–11).

## Hardware da placa

| Item | Valor | Fonte |
|---|---|---|
| LED onboard | Azul, GPIO2 (TODO: confirmar se acende com nível alto) | `LED_BUILTIN = 2` na variante `doitESP32devkitV1` do Arduino-ESP32, confirmado piscando o LED |
| Ponte USB-serial | WCH CH9102 (`1a86:55d4`) | ID USB |
| Módulo | TODO: ler a blindagem (WROOM-32E?) | foto pendente |
| Regulador 3,3 V | TODO: ler o código do componente | foto pendente |
| Número de pinos | TODO | contagem pendente |
