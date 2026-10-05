# Pinagem do nó sensor (NÓ 1)

Ligações do nó como montado na Etapa 2 (bancada, protoboard). Constantes correspondentes em [`no_sensor/src/config.h`](../no_sensor/src/config.h); funcionamento e medições em [`sensores.md`](sensores.md).

## Esquema

```
                         ESP32 DOIT DevKit V1 (NÓ 1)
                       ┌───────────────────────────┐
   AHT20 ── VCC ───────┤ 3V3                       │
          ├ GND ───────┤ GND                       │
          ├ SDA ───────┤ D21  (GPIO21, SDA do Wire)│
          └ SCL ───────┤ D22  (GPIO22, SCL do Wire)│
                       │                           │
   Solo V1.2 ─ VCC ────┤ 3V3                       │
             ├ GND ────┤ GND                       │
             └ AOUT ───┤ D34  (GPIO34, ADC1_CH6)   │
                       │                           │
   3V3 ──[R1 100k]──┬──[R2 100k]── GND             │
                    └──────────────┤ D35  (GPIO35, ADC1_CH7)
                       └───────────────────────────┘
```

O divisor mede hoje o próprio 3V3 da placa, para validar a leitura. Na etapa 5, a entrada (ponta de cima do R1) passa a ser a bateria, e os resistores podem mudar conforme a tensão máxima dela.

## Tabela

| Sinal | Pino da placa | GPIO | Função | Observação |
|---|---|---|---|---|
| AHT20 SDA | D21 | 21 | I²C dados | pull-up na plaquinha do AHT20 |
| AHT20 SCL | D22 | 22 | I²C clock | pull-up na plaquinha do AHT20 |
| AHT20 VCC / GND | 3V3 / GND | — | alimentação | |
| Solo AOUT | D34 | 34 | ADC1_CH6 | só entrada; ADC1 funciona com Wi-Fi ligado |
| Solo VCC / GND | 3V3 / GND | — | alimentação | **precisa ser 3V3 regulado** (ver abaixo) |
| Divisor (ponto médio) | D35 | 35 | ADC1_CH7 | R1 = R2 = 100 kΩ, 5 %; razão medida 0,5007 |

Nenhuma ligação usa pinos de strapping (0, 2, 5, 12, 15), o ADC2 ou os pinos da flash (6–11).

## Cuidados de montagem (aprendidos na bancada)

1. **VIN e 3V3 ficam em pontas opostas da mesma fileira de pinos da DOIT.** Ligar o sensor de solo no VIN (5 V) por engano aconteceu nos testes. O D35 suporta a tensão do divisor, mas o GPIO34 não pode receber mais de ~3,6 V.
2. **O sensor de solo não deve ser alimentado direto por um GPIO.** A tensão do pino cai com a carga (3,23 V com o *drive* padrão), e a saída do sensor depende muito da alimentação (ver `sensores.md`). Para ligá-lo e desligá-lo, usar um transistor chaveando o 3V3 (decisão da etapa 5).
3. **Jumpers soltos:** um fio de SCL frouxo deixou o AHT20 em `erro` depois de um fim de semana parado. O firmware reporta o erro e retoma sozinho quando o fio volta, mas, no campo, as ligações devem ser soldadas ou feitas com conectores travados.
4. **Placa encaixada na protoboard:** os pinos de uma placa encaixada ficam ligados às fileiras. Nenhum fio de outro circuito pode ocupar essas fileiras.
5. **Corpo perto do sensor de solo** altera a leitura (a mão a alguns centímetros mudou a saída em dezenas a centenas de mV). Medir com as mãos afastadas.
