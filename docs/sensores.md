# Sensores do nó

Funcionamento, método de leitura e validação de cada grandeza medida pelo nó sensor (Etapa 2). Ligações em [`placas.md`](placas.md#ligações-do-nó-1).

| Grandeza | Sensor | Interface | Pino |
|---|---|---|---|
| Temperatura do ar | AHT20 | I²C | GPIO21/22 |
| Umidade relativa do ar | AHT20 | I²C | GPIO21/22 |
| Umidade do solo | Capacitivo V1.2 | ADC1 | GPIO34 |
| Tensão de alimentação | Divisor resistivo | ADC1 | GPIO35 |

**Bancada × campo.** As medições deste documento foram feitas em bancada (vaso com planta, em casa) para validar métodos, código e procedimentos. Os valores de calibração do solo são de bancada e devem ser refeitos no solo da horta com o procedimento da seção 4.6.

## 1. Barramento I²C

- Duas linhas, SDA (dados) e SCL (clock), em dreno aberto: os dispositivos só puxam a linha para 0 V; resistores de pull-up a levam a 3,3 V.
- Cada transação começa com START + endereço de 7 bits + bit R/W. O dispositivo com aquele endereço responde com ACK (puxa o SDA no 9º pulso); sem resposta, NACK.
- O ESP32 suporta Standard-mode (100 kHz) e Fast-mode (400 kHz); o `Wire.begin()` sem frequência usa 100 kHz (`esp32-hal-i2c.c` do núcleo 3.3.12).
- O ESP-IDF recomenda pull-ups externos de 2 a 5 kΩ, pois os internos (~45 kΩ) *"are not strong enough"*; o datasheet do AHT20 sugere 2,0–4,7 kΩ.

Fontes: [Arduino-ESP32 I²C](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2c.html), [ESP-IDF v5.5 I²C](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/peripherals/i2c.html).

**Verificação na placa NÓ 1** (scanner I²C):

| Verificação | Resultado |
|---|---|
| Dispositivos encontrados | 1, em **0x38** (AHT20) |
| Pull-ups externos | presentes na plaquinha do AHT20 (SDA e SCL ficam em 1 mesmo com o pull-down interno do ESP32 ligado); valor não medido |
| Clock | 100 kHz |

## 2. AHT20 — temperatura e umidade do ar

Fonte: Aosong/ASAIR, *AHT20 Data Sheet*, V1.0, maio/2021 — <https://www.aosong.com/userfiles/files/media/Data%20Sheet%20AHT20.pdf>.

### Características (datasheet, tabelas 1–3)

| Parâmetro | Temperatura | Umidade relativa |
|---|---|---|
| Faixa | −40 a 85 °C | 0 a 100 %UR |
| Precisão típica | ±0,3 °C | ±2 %UR |
| Resolução | 0,01 °C | 0,024 %UR |
| Tempo de resposta (τ63 %) | 5–30 s | 8 s |

Faixa normal de operação: 0–80 %UR. Exposição longa acima de 80 %UR causa deriva temporária (+3 %UR após 60 h a 90 %UR). Alimentação de 2,2 a 5,5 V; corrente de 980 µA medindo e no máximo 250 nA em repouso.

### Protocolo (datasheet, seção 7)

| Etapa | Ação | Seção |
|---|---|---|
| 1 | Esperar ≥ 100 ms após energizar | 7.1 |
| 2 | Ler 1 byte de estado; se `estado & 0x18 ≠ 0x18`, o sensor não está calibrado | 7.4, passo 1 |
| 3 | Esperar 10 ms e enviar `0xAC 0x33 0x00` (disparar medição) | 7.4, passo 2 |
| 4 | Esperar 80 ms; consultar o estado até o bit 7 (ocupado) ser 0 | 7.4, passo 3 |
| 5 | Ler 7 bytes: estado, 20 bits de umidade, 20 bits de temperatura, CRC | figura da pág. 13 |
| 6 | Verificar o CRC-8 (valor inicial 0xFF, polinômio x⁸+x⁵+x⁴+1 = 0x31) | 7.4, passo 4 |

Conversões (seção 8), com S sendo o valor de 20 bits:

- UR [%] = S_UR / 2²⁰ × 100
- T [°C] = S_T / 2²⁰ × 200 − 50

O datasheet recomenda clock I²C entre 10 e 400 kHz e intervalo de pelo menos 1 s entre medições, para limitar o autoaquecimento a 0,1 °C (seção 4.4).

### Lacunas do datasheet e como foram resolvidas

1. **Bytes cobertos pelo CRC.** O datasheet não diz. Verificado na prática: o CRC é calculado sobre os **6 primeiros bytes** (estado + 5 de dados). Bateu em todas as leituras; sobre 5 bytes não bate.
2. **Inicialização dos registradores 0x1B, 0x1C, 0x1E.** O datasheet remete a um exemplo do site do fabricante em vez de documentá-la. O driver não executa essa sequência: se o sensor não estiver calibrado, retorna erro. Os sensores usados vieram calibrados de fábrica (estado 0x18).
3. **"Comando 0x71".** É o byte de endereço de leitura (0x38 << 1 | 1); "ler o estado" é simplesmente ler 1 byte do sensor.

### Escolha do driver

| | Adafruit AHTX0 2.0.6 | SparkFun AHT20 | enjoyneering AHTxx | **Driver próprio (escolhido)** |
|---|---|---|---|---|
| Verifica CRC | não | não | sim | sim |
| Espera do bit de ocupado com limite de tempo | não | não | sim | sim |
| Inicialização | `0xBE` (datasheets antigos) | `0xBE` | `0xBE` | a do datasheet V1.0 |
| Dependências | 3 bibliotecas | nenhuma | nenhuma | nenhuma |
| Licença | não declarada | MIT | não declarada | do projeto |

Motivos da escolha: segue a versão atual do datasheet, verifica o CRC, não trava se o sensor parar de responder (sem limite de tempo, o nó nunca voltaria ao deep sleep e esgotaria a bateria), não adiciona dependências e permite explicar o protocolo byte a byte. Código: [`no_sensor/src/aht20.h`](../no_sensor/src/aht20.h) e [`aht20.cpp`](../no_sensor/src/aht20.cpp).

Estados retornados: `ok`, `sem_resposta` (NACK ou número errado de bytes), `nao_calibrado`, `timeout` (ocupado por mais de 200 ms) e `erro_crc`.

### Validação

| Medição | Resultado |
|---|---|
| Leituras válidas em estado normal | 13/13 no teste do driver (CRC conferido em todas); 303/303 no teste de estabilidade (seção 7) |
| Tempo de conversão medido | 80–81 ms (datasheet: 80 ms) |
| Duração da leitura de 7 bytes a 100 kHz | ~0,85 ms |

### Estado anômalo observado

Durante os testes, o AHT20 entrou num estado em que **a primeira leitura após o comando 0xAC sempre falha** e a seguinte funciona normalmente, trazendo dados válidos.

| Observação | Evidência |
|---|---|
| Sintoma | a 1ª leitura após o 0xAC falha em ~15 ms com `ESP_ERR_INVALID_STATE`; a 2ª traz estado 0x18 e dados com CRC correto |
| Persistência | sobrevive ao reset do ESP32 (o sensor continua alimentado pelo 3V3) |
| Correção | desligar e religar a alimentação |
| Descartado | barramento travado (SDA e SCL em 1; bus clear sem pulsos); tempo de conversão (esperas de 80 a 1000 ms não mudam o resultado) |
| Gatilho | desconhecido: apareceu 6 vezes em 6 depois de rodar um scanner I²C, mas 0 em 6 depois de um ciclo de alimentação, e uma vez sem scanner nenhum |

`ESP_ERR_INVALID_STATE` indica apenas que a transação não terminou (`i2c_master.c:727`, ESP-IDF 5.5.5). O limite padrão de clock stretching usado pelo driver é 2 ms (`I2C_LL_SCL_WAIT_US_VAL_DEFAULT`), o que não explica os ~15 ms observados. Sem analisador lógico, o mecanismo não foi identificado; o datasheet não descreve esse comportamento.

**Tratamento adotado:**

1. No driver, a consulta ao byte de estado repete as leituras que falham no I²C, dentro do limite de 200 ms. O campo `tentativasExtras` conta as repetições (0 em operação normal). Validado em estado normal; no teste de estabilidade, uma leitura em 303 precisou de 2 tentativas extras (151 ms em vez de 97 ms) e terminou `ok`, o primeiro registro do caminho de recuperação funcionando em execução real.
2. Na etapa 5, alimentar o AHT20 por um GPIO e ligá-lo a cada ciclo, como prevê o datasheet (seção 4.5, item 5). Isso elimina o estado na origem e zera o consumo de repouso do sensor.

## 3. ADC do ESP32

Fontes:

- [DS] *ESP32 Series Datasheet* v5.3, seção 4.9.1 — <https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf>
- [Arduino] <https://docs.espressif.com/projects/arduino-esp32/en/latest/api/adc.html>
- [IDF-oneshot] <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/peripherals/adc_oneshot.html>
- [IDF-cali] <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/peripherals/adc_calibration.html>

### Conceitos

- **Resolução:** dois ADCs SAR de 12 bits (0–4095) [DS]; o padrão do Arduino é 12 bits [Arduino].
- **Atenuação:** a entrada é atenuada antes do conversor para ampliar a faixa medível. Padrão do núcleo: 11 dB (`esp32-hal-adc.c:62`), chamado `ADC_ATTEN_DB_12` no ESP-IDF 5.

| Atenuação | Faixa efetiva [DS tab. 4-4] | Erro total após calibração [DS] |
|---|---|---|
| 0 dB | 100–950 mV | ±23 mV |
| 2,5 dB | 100–1250 mV | ±30 mV |
| 6 dB | 150–1750 mV | ±40 mV |
| 11 dB | 150–2450 mV | ±60 mV |

  A página do Arduino-ESP32 dá 150–3100 mV para 11 dB; o datasheet limita a faixa precisa a 2450 mV (*"above 3000 (… approx. 2450 mV), the ADC accuracy will be worse"*). Adotamos o datasheet.

- **Não linearidade:** DNL até ±7 LSB e INL até ±12 LSB, medidos com capacitor externo de 100 nF e Wi-Fi desligado [DS tab. 4-3]. Sem calibração, a diferença entre chips chega a ±6 % [DS].
- **Calibração de fábrica:** no ESP32, o esquema é o de ajuste linear (*line fitting*), com a referência gravada em eFuse (Vref ou Two Point) [IDF-cali]. As placas do projeto têm Vref em eFuse ([`ambiente.md`](ambiente.md)).
- **`analogReadMilliVolts()`:** lê o valor bruto e o converte em mV com essa calibração [Arduino]. É a função usada no projeto; `analogRead()` devolve o valor bruto, sem correção entre chips.
- **Só ADC1 (GPIO32–39):** *"ADC2 is also used by Wi-Fi"* [IDF-oneshot]; com o rádio ativo, a leitura do ADC2 pode retornar `ESP_ERR_TIMEOUT`, *"the ADC result is invalid"* (`adc_oneshot.h`).
- **Ruído:** o datasheet recomenda amostragem múltipla com filtro ou média [DS]; o ESP-IDF recomenda um capacitor cerâmico de 100 nF na entrada [IDF-cali]. O nó não usa o capacitor (não disponível).
- **Atenuação no Arduino-ESP32 3.3.12:** usar `analogSetAttenuation()` (global). `analogSetPinAttenuation()` não age em pino ainda não inicializado (só registra um erro), e a calibração em mV é criada com a atenuação global (`esp32-hal-adc.c`). Como o padrão do núcleo já é 11 dB, todas as medições deste documento foram feitas em 11 dB.
- **Teto da leitura:** com o pino ligado direto ao 3V3 (através de 100 kΩ), a leitura travou em 3134 mV, coerente com os ~3100 mV da documentação do Arduino-ESP32 e bem acima dos 2450 mV da faixa precisa.

### Medição de ruído (GPIO34, sensor de solo no ar, 11 dB, 2000 leituras seguidas)

| Grandeza | Valor |
|---|---|
| Tempo por leitura | 40 µs (`analogRead` e `analogReadMilliVolts`) |
| Média | 2195 mV (bruto 2550) |
| Desvio-padrão | 23 mV (29 LSB) |
| Pico a pico | 219 mV |
| Valores a mais de 4 desvios da mediana | 2 em 2000 |

O histograma dos valores brutos mostra a não linearidade do ADC: o código 2559 aparece 425 vezes e nenhum valor entre 2560 e 2577 ocorre.

| Filtro (bloco de N leituras) | Tempo | Desvio: média | Desvio: mediana |
|---|---|---|---|
| N = 1 | 0,04 ms | 23,5 mV | 23,5 mV |
| N = 4 | 0,16 ms | 11,4 mV | 11,4 mV |
| N = 16 | 0,64 ms | 5,2 mV | 6,0 mV |
| N = 64 | 2,56 ms | **2,0 mV** | 3,4 mV |

**Decisão:** média de 64 leituras de `analogReadMilliVolts()`, com atenuação de 11 dB. O ruído cai para ~2 mV, abaixo do erro de calibração (±60 mV), com custo de 2,6 ms. A média foi melhor que a mediana porque quase não houve valores espúrios. Confirmado na calibração: desvio de 2,5 mV entre médias consecutivas no ar. A atenuação de 11 dB é a única que cobre a saída do sensor de solo (736–2203 mV; a de 6 dB para em 1750 mV).

## 4. Sensor capacitivo de umidade do solo V1.2

### 4.1 Identificação

Não há datasheet. Fontes disponíveis:

- **Ficha do vendedor** (não é documentação técnica): operação de 3,3 a 5,5 V, saída analógica de 0 a 3 V, conector PH2.0-3P.
- **Serigrafia da placa:** U1 (CI de 8 pinos, provavelmente um 555), U2 (3 pinos, posição típica de regulador), T4 (diodo, pela aparência), resistores R1–R4 e capacitores C1–C6. A marcação dos CIs não é legível nem nas fotos do fabricante; o CI fica registrado como **não identificado**, e o comportamento foi verificado na prática.

### 4.2 Princípio de funcionamento

As duas trilhas largas de cobre da haste, cobertas pelo verniz da placa, formam um capacitor cujo dielétrico é o meio em volta. A permissividade relativa da água (~80) é muito maior que a do solo seco (poucas unidades) e a do ar (1), então mais água no solo significa mais capacitância. Pelo que os componentes indicam (inferência, sem esquema oficial): o oscilador gera uma onda quadrada, que passa por um filtro RC formado por um resistor e pela haste; quanto maior a capacitância, menor a amplitude que chega ao diodo e ao capacitor de saída. Resultado: **mais umidade, menor tensão**.

Vantagem sobre o sensor resistivo (dois garfos metálicos): não há cobre exposto nem corrente contínua pela terra, então a haste não sofre eletrólise nem corrosão.

### 4.3 Calibração de bancada

Método: sensor alimentado pelo 3V3 da placa (~3,38 V), uma média de 64 leituras por segundo, mãos afastadas do sensor. A janela usada em cada ponto exclui o período de acomodação.

| Ponto | Janela | n | Média | Desvio |
|---|---|---|---|---|
| Ar | 30–90 s | 60 | 2194,1 mV | 2,5 mV |
| **Solo seco ao toque (acomodado 14 h)** | 30–600 s | 570 | **1400,0 mV** | 4,0 mV |
| Água da torneira | 30–240 s | 210 | 736,3 mV | 4,5 mV |
| **Solo ~1 h após a rega** | 65 s | 65 | **654,5 mV** | 3,5 mV |
| Solo logo após a rega | pico | — | ~584 mV | — |

Figura: [`figuras/calibracao_solo.png`](figuras/calibracao_solo.png).

**Mapeamento adotado (índice de 0 a 100 %):** linear entre o solo seco ao toque (0 %) e o solo cerca de 1 h após a rega (100 %):

    % = (1400 − V) / (1400 − 655) × 100,  limitado a 0–100

A escala ar → água foi descartada: o solo real ocupa só metade dela, e o solo encharcado fica abaixo da água (584 < 736 mV), provavelmente pela condutividade da água do solo, com sais dissolvidos. A escala adotada usa a faixa inteira do uso real e é intuitiva para irrigação (0 % = hora de regar; 100 % = recém-regado).

Limitações:

1. É um **índice relativo** à calibração, não umidade volumétrica. Esta exigiria o método gravimétrico (pesar, secar em estufa, pesar), fora do escopo.
2. Com dois pontos, a relação é tratada como linear. Um ponto intermediário acomodado melhoraria a curva.
3. A calibração vale para o solo e para a alimentação em que foi feita.

### 4.4 Acomodação após inserir ou regar

- **Após inserir na terra**, a leitura cai devagar por **horas** (1875 → 1745 mV na primeira hora; ~1420 mV após 13 h) e recomeça alta a cada reinserção. Testes: no ar, a leitura volta na hora ao valor original (2203 mV), então a placa não absorve água; a deriva não acompanhou a umidade do ar (a UR caiu de 91 para 77 % e a leitura continuou caindo). Interpretação: acomodação da terra em volta da haste (contato e umidade migrando para a superfície do sensor).
- **Após regar**, a resposta é imediata (1412 → 584 mV em segundos) e, conforme a água escoa, a leitura sobe e estabiliza em ~620–650 mV ao longo da primeira hora.
- **Consequência para o campo:** descartar as primeiras horas depois da instalação (sugestão: 24 h) e calibrar só com o sensor acomodado.

Figura: [`figuras/acomodacao_solo.png`](figuras/acomodacao_solo.png).

### 4.5 Alimentação do sensor

**Estabilização após ligar.** Com o sensor alimentado por um GPIO, a saída sobe como a carga de um capacitor (constante de tempo de ~50 ms) e estabiliza em 300–400 ms (10 ciclos medidos). **Espera recomendada antes de ler: 500 ms.** Figura: [`figuras/estabilizacao_ligar_solo.png`](figuras/estabilizacao_ligar_solo.png).

**Dependência da alimentação** (mesmo solo, minutos de diferença; tensão medida pelo divisor):

| Alimentação do sensor | Tensão | Saída |
|---|---|---|
| VIN (USB) | 5,11 V | 684 mV |
| 3V3 da placa | 3,39 V | 654,5 mV |
| GPIO25, drive 3 (~40 mA) | 3,30 V | 533 mV |
| GPIO25, drive 2 (~20 mA, padrão) | 3,23 V | 422 mV |
| GPIO25, drive 1 (~10 mA) | 3,09 V | 172 mV |
| GPIO25, drive 0 (~5 mA) | 2,89 V | sem sinal |

Figura: [`figuras/alimentacao_solo.png`](figuras/alimentacao_solo.png). Níveis de *drive strength*: ESP32 Datasheet v5.3, notas da tabela IO_MUX; API `gpio_set_drive_capability()` do ESP-IDF 5.5.

Conclusões:

1. O sensor consome o suficiente para derrubar a tensão de um GPIO.
2. A saída é praticamente plana acima de ~3,4 V e desaba abaixo disso: **comportamento compatível com um regulador de 3,3 V na placa (U2) saindo de regulação**. Hipótese, já que o componente não foi identificado.
3. No 3V3 da placa (3,39 V), o sensor opera na borda da região plana: cada 10 mV de variação na alimentação muda a saída em ~15 mV. A calibração vale enquanto o 3V3 for estável.
4. **Não alimentar o sensor direto por GPIO.** Para ligá-lo e desligá-lo (etapa 5), usar um transistor chaveando uma alimentação regulada e, de preferência, um pouco acima de 3,5 V, na região plana.

### 4.6 Procedimento de calibração em campo

1. Instalar o sensor no canteiro na posição definitiva, até a linha branca, com a terra firme em volta.
2. Aguardar a acomodação (sugestão: 24 h), com o nó registrando.
3. **Ponto seco (0 %):** com a terra seca ao toque, antes de uma rega, registrar 10 min e usar a média.
4. **Ponto úmido (100 %):** regar normalmente e, depois de ~1 h (água escoada), registrar 10 min e usar a média.
5. Atualizar `SOLO_MV_SECO` e `SOLO_MV_UMIDO` em `config.h` (ou no coordenador, se o pacote levar a tensão em mV; decisão da etapa 3).
6. Cuidados: mesma alimentação do uso real; mãos e corpo longe do sensor durante a medição; registrar data, hora e condição do solo.

## 5. Tensão de alimentação (divisor resistivo)

### 5.1 Conceito e escolha dos resistores

O ADC mede com precisão até 2450 mV em 11 dB, e o datasheet limita a tensão de um pino à alimentação do chip (VIH máx = VDD + 0,3 V, tab. 5-3). Baterias comuns passam disso, então a tensão é reduzida por um divisor: `V_adc = V × R2 / (R1 + R2)`.

| Critério | Efeito |
|---|---|
| Razão | `V_máx × R2/(R1+R2) ≤ 2450 mV`. Com R1 = R2, mede até ~4,9 V (cobre uma célula de lítio, LiFePO4 e 3 pilhas AA) |
| Corrente drenada | `V/(R1+R2)`, contínua, inclusive dormindo. O chip ESP32 em deep sleep consome 10 µA (datasheet); 10k + 10k em 4,2 V gastariam 210 µA; 100k + 100k, 21 µA |
| Impedância vista pelo ADC | R1‖R2. Resistores altos economizam bateria, mas o capacitor de amostragem do ADC pode não carregar por completo. A Espressif não publica a impedância de entrada do ADC |
| Capacitor de filtro | ~100 nF no ponto médio fornece a carga que o ADC puxa e filtra o ruído (recomendação do ESP-IDF); custo: constante de tempo de (R1‖R2)·C, 5 ms com 50 kΩ |
| Divisor chaveado | um MOSFET ligado só durante a leitura elimina a corrente de repouso (etapa 5) |

**Escolha:** R1 = R2 = 100 kΩ (5 %), sem capacitor (não disponível), ligado ao GPIO35 (ADC1_CH7).

### 5.2 Validação sem multímetro (troca dos resistores)

Medindo o divisor duas vezes com os resistores trocados de lugar: `V1 = V·R2/(R1+R2)` e `V2 = V·R1/(R1+R2)`, logo **V = V1 + V2** e **razão = V1/(V1+V2)**, independentemente do valor nominal dos resistores.

| Grandeza | Valor |
|---|---|
| V1 (montagem original, 30 leituras) | 1696,0 mV |
| V2 (resistores trocados, 108 leituras) | 1691,2 mV |
| Tensão do 3V3 (V1 + V2) | 3387 mV |
| **Razão medida** | **0,5007** (nominal 0,5000) |
| Diferença entre os resistores | 0,28 % |

Limites: a soma usa o próprio ADC, então um erro de ganho do ADC não aparece; a validação contra um multímetro fica pendente. Com o divisor, o erro de ±60 mV do ADC vira **±120 mV** na tensão de entrada, o erro dominante (a tolerância dos resistores, depois de calibrada a razão, contribui ~0,1 %).

## 6. Faixas válidas e estados

Cada leitura retorna valor + estado (`ok`, `erro`, `fora_de_faixa`). Código: [`no_sensor/src/sensores.cpp`](../no_sensor/src/sensores.cpp); constantes em [`config.h`](../no_sensor/src/config.h).

| Grandeza | Válido | Fonte | Fora disso |
|---|---|---|---|
| Temperatura | −40 a 85 °C | AHT20, tab. 2 | `fora_de_faixa`; falha de I²C ou CRC → `erro` |
| Umidade do ar | 0 a 100 % | AHT20, tab. 1 | idem |
| Solo (tensão no ADC) | 150 a 2450 mV | ESP32 DS, tab. 4-4 | < 150 mV → `erro` (sem sinal: sensor desligado ou solto); > 2450 → `fora_de_faixa` |
| Solo (índice) | 0 a 100 % | calibração (4.3) | limitado a 0–100 e marcado `fora_de_faixa` |
| Alimentação (tensão no ADC) | 150 a 2450 mV | ESP32 DS, tab. 4-4 | `fora_de_faixa`; a faixa da bateria será definida na etapa 5 |

**Teste de falhas:** com o fio do SCL solto (aconteceu depois de um fim de semana parado), temperatura e UR ficaram em `erro` e solo e alimentação continuaram `ok`. Ao recolocar o fio, o AHT20 voltou a `ok` na leitura seguinte, sem reiniciar a placa (o firmware tenta iniciar o sensor de novo a cada ciclo).

**Observação:** perto de 100 %, o ruído faz o índice do solo alternar entre `ok` e `fora_de_faixa` (ex.: logo após a rega). O comportamento é o esperado; no campo, isso indica solo recém-regado.

## 7. Teste de estabilidade

Nó parado por 10 min (05/10/2026), leitura a cada 2 s, solo do vaso secando há 5 dias. Script: [`ferramentas/analisar_sensores.py`](../ferramentas/analisar_sensores.py). Figura: [`figuras/estabilidade_10min.png`](figuras/estabilidade_10min.png).

| Grandeza | n | Média | Desvio | Mín | Máx | Estados |
|---|---|---|---|---|---|---|
| Temperatura do ar | 303 | 20,18 °C | 0,05 °C | 20,06 | 20,30 | ok: 303 |
| Umidade do ar | 303 | 59,24 % | 0,47 % | 58,00 | 59,97 | ok: 303 |
| Solo (tensão) | 303 | 997,4 mV | 3,5 mV | 985,9 | 1010,8 | ok: 303 |
| Solo (índice) | 303 | 54,0 % | 0,47 % | 52,2 | 55,6 | ok: 303 |
| Alimentação | 303 | 3369 mV | 2,8 mV | 3362 | 3378 | ok: 303 |

- Tempo para ler as 4 grandezas: 97,5 ms em média (dominado pelos 80 ms de conversão do AHT20); máximo de 151,5 ms na leitura que usou 2 tentativas extras do AHT20.
- As variações de temperatura e UR acompanham o ambiente (deriva lenta, não ruído); o desvio do solo (3,5 mV ≈ 0,5 % do índice) e o da alimentação (2,8 mV) estão na ordem do ruído residual da média de 64 leituras.
