# Energia do nó sensor

Quanto tempo o nó fica acordado e com o rádio ligado por ciclo, e quanta carga isso custa (Etapa 5), e o que a confirmação e o reenvio da Etapa 7.1 acrescentaram.

## 1. Método

**Não há medição de corrente** (decisão do projeto: sem multímetro nem INA). A carga é estimada como **tempo medido × faixa de corrente do datasheet**, fase por fase, e o resultado é uma faixa, não um valor ([`ferramentas/estimar_energia.py`](../ferramentas/estimar_energia.py)).

| Estado | Corrente | Fonte |
|---|---|---|
| CPU ativa a 240 MHz, sem rádio | 30–68 mA | ESP32 Series Datasheet v5.3, tab. 4-2 (modem-sleep) |
| Rádio ligado, sem transmitir | 30–100 mA | entre a CPU sozinha e a recepção (RX 95–100 mA, tab. 5-4) |
| Esperando uma resposta (receptor ligado) | 95–100 mA | RX 802.11b, tab. 5-4 |
| Transmitindo, 802.11b 1 Mbps, +19,5 dBm | 240 mA | tab. 5-4 |
| Light sleep / deep sleep (chip) | 0,8 mA / 10 µA | tab. 4-2 |

**Tempos:** o firmware mede cada fase com o `esp_timer` (linha `[FASES]`). O PC mede a parte que o firmware não vê (ROM + bootloader), pela diferença entre a linha `rst:` da ROM e a primeira linha do `setup()` ([`ferramentas/analisar_energia.py`](../ferramentas/analisar_energia.py)). O "rádio" de um ciclo é ligar + enviar + desligar o Wi-Fi.

## 2. Ciclo de produção (Etapa 5)

Medido com o firmware de produção (env `producao` instrumentado, `p4_4b_qio_80m`, mediana de 11 ciclos):

| Fase | Mediana (ms) |
|---|---|
| ROM + bootloader | 36,0 |
| Inicialização ESP-IDF/Arduino até o `setup()` | 11,2 |
| Leitura dos sensores (90,1 ms dela em light sleep) | 97,5 |
| Ligar Wi-Fi + ESP-NOW | 19,8 |
| Enviar (até o ACK MAC) | 1,8 |
| Desligar o Wi-Fi | 1,0 |
| **Acordado** (`acordado_ant_ms`) | **~170** |
| **Rádio ligado** | **22,6** |

| Parcela | Carga por ciclo (mC) |
|---|---|
| CPU (boot, leitura) | 1,70–3,86 |
| Rádio | 0,83–2,36 |
| Light sleep | 0,07 |
| **Total** | **2,60–6,29** |

A 15 min (96 ciclos por dia), o rádio gasta 0,02–0,06 mAh/dia. Quem decide a autonomia é a **corrente da placa dormindo** (regulador, ponte USB, LED), ainda não medida ([`auditoria_errc.md`](auditoria_errc.md), seção 2).

## 3. Etapa 7.1: antes × depois

Bancada, 19/10/2026, NÓ 1 com o env `bancada` (período de 10 s, logs ligados), COORD ao lado. Mesmo hardware e mesmas condições nos dois casos: "antes" = firmware v2 (Etapa 5/HMAC), "depois" = firmware v3 (fila e confirmação). Dados: [`dados/etapa7_1/brutos/`](../dados/etapa7_1/brutos/) (`energia_antes`, `t1_normal`, `t2_coord_desligado`); análise: [`ferramentas/analisar_reenvio.py`](../ferramentas/analisar_reenvio.py).

> Nas duas capturas o AHT20 do NÓ 1 está sem resposta no I²C (`estados` = 5, pendência de hardware). Sem as esperas de medição, a leitura não entra em light sleep (0 ms, contra 90 ms na Etapa 5) e fica mais longa com a CPU ligada. Os números absolutos, por isso, não são os de produção. A comparação antes × depois vale, porque as duas estão na mesma condição; só a fase de envio mudou.

| Fase (mediana, ms) | Antes (v2, 16 ciclos) | Depois (v3, 16 ciclos) | Diferença |
|---|---|---|---|
| ROM + bootloader | 35,8 | 35,5 | — |
| Iniciar sensores + leitura | 135,9 | 136,4 | — |
| Ligar Wi-Fi + ESP-NOW | 19,5 | 19,9 | +0,4 |
| **Enviar** | **2,0** | **7,3** | **+5,3** (envio de 42 B + espera e recepção da confirmação) |
| Desligar o Wi-Fi | 0,9 | 1,0 | — |
| **Acordado (`esp_timer`)** | **171,1** | **177,7** | **+6,6 (+3,9 %)** |
| **Rádio ligado** | **22,4** | **28,2** | **+5,8** |

- **Tempo de resposta** (do ACK MAC da leitura nova até a confirmação válida): **mediana 3,4 ms, p95 6,4–7,3 ms, máximo 13,2 ms** (98 respostas, testes 1–3). Dentro da estimativa de 2–6 ms do passo 2, e bem abaixo do prazo de espera de 30 ms.
- **Carga a mais por ciclo normal:** 5,3 ms com o receptor ligado = **+0,50–0,53 mC**. No ciclo de produção: **2,60–6,29 → 3,10–6,82 mC (+8 a +19 %)**, ou **+0,013 mAh/dia** a 15 min.
- **A confirmação depois da gravação** (opção a do passo 2, descartada) teria custado ~60 ms a mais por ciclo (2 a 3× o ciclo).

## 4. Esvaziando a fila

Depois de 20 min com o coordenador fora (teste 2, 123 leituras na fila), o nó esvaziou a fila em 11 ciclos de 12 reenvios:

| Ciclo | Acordado (mediana) | Rádio (mediana) | Carga a mais |
|---|---|---|---|
| Normal | 176,8 ms | 27,3 ms | — |
| Com rajada de 12 reenvios | 204,1 ms (máx. 238) | 54,6 ms (máx. 88,9) | **+3,0–4,2 mC** (12 × 0,87 ms no ar a 240 mA + o resto com o rádio ligado) |
| **Por leitura reenviada** | | **~2,3 ms** | **0,25–0,35 mC** |

O custo total de esvaziar só depende de quantas leituras estão na fila: B = 12 define em quanto tempo. O prazo máximo acordado (3 s) fica longe: o ciclo mais longo com rajada levou 238 ms.

## 5. Coordenador fora do ar

| Ciclo | Acordado (mediana) | Rádio (mediana) | Carga estimada |
|---|---|---|---|
| Coordenador fora (3 tentativas sem ACK), T2 | 346,9 ms | 197,4 ms | **19–48 mC** (7× um ciclo normal) |
| Idem, T3 | 342,9 ms | 193,4 ms | |
| Idem, T4 (outra hora do dia) | 526,2 ms | 377,7 ms | até ~91 mC |

O tempo até o `FAIL` varia com o canal: a camada MAC retransmite o quadro sozinha, com espera aleatória crescente, e a Espressif não documenta quantas vezes ([`comunicacao.md`](comunicacao.md), 6.1).

Esse custo não vem da Etapa 7.1: é a política de retransmissão da Etapa 4 (até 3 envios, cada um esperando o `FAIL` da camada MAC, ~34 ms, com retransmissões automáticas do quadro). Antes da Etapa 7.1, as retransmissões tentavam salvar a leitura. **Agora a leitura fica guardada na fila**, e insistir no mesmo ciclo deixou de ser necessário para não perder dados.

**Regra adaptativa (Etapa 8, passo 0, adotada):** se o ciclo anterior terminou sem ACK, a leitura nova tem **1 tentativa** (`ENVIOS_APOS_FALHA`); com ACK, continuam as 3 da Etapa 4. Na operação normal nada muda. Numa queda, só o primeiro ciclo paga as 3 tentativas.

Comparação nas mesmas condições de canal (duas capturas seguidas, 19/10/2026 à tarde, 5 min de coordenador fora em cada; `dados/etapa8/brutos/p0_tentativas3` com o env `teste_tentativas3` × `p0_tentativa_adaptativa`):

| Ciclo com o coordenador fora | 3 tentativas (antes) | 1 tentativa (adaptativa) | Redução |
|---|---|---|---|
| Acordado, mediana | 680 ms | 385 ms | −43 % |
| Acordado, p95 / máx. | 2118 / 2257 ms | 917 / 1060 ms | −57 % / −53 % |
| Rádio, mediana | 506 ms | 236 ms | −53 % |
| Rádio, p95 | 1941 ms | 741 ms | −62 % |
| Leituras gravadas | 49/49, 0 duplicatas | 49/49, 0 duplicatas | — |

**Achado:** à tarde, a camada MAC levou mais de 100 ms para desistir de um quadro sem ACK. O callback não chegou no prazo de 100 ms do firmware (`t1_us` = −1), e o `esp_wifi_stop()` **esperou** a MAC terminar as retransmissões (fase "desligar" de 130–520 ms, contra ~1 ms quando o `FAIL` chega). O custo de cada tentativa sem ACK, portanto, varia com a ocupação do canal: 34–44 ms ao meio-dia (T2), centenas de ms à tarde. Com a regra antiga, um ciclo chegou a 2,26 s, perto do prazo máximo acordado de 3 s. A Espressif não documenta o número de retransmissões da MAC nem uma forma de interrompê-las (pendência: investigar).

## 6. Limitações

- Carga **estimada** (tempo medido × corrente do datasheet), não medida. Exceção: a corrente do sensor de solo, medida com um resistor shunt lido pelo próprio ESP32 (4,38 mA, [`sensores.md`](sensores.md) 4.5).
- Bancada a 10 s com logs; na produção (15 min, sem logs) os tempos de rádio são os mesmos, e o ciclo fica sem a impressão.
- Corrente da placa dormindo e bateria: ainda não definidas (Etapa 5, pendente).
- AHT20 do NÓ 1 em erro durante as medições da Etapa 7.1 (seção 3).

## 7. Alimentação de campo e autonomia estimada (26/11/2026)

### 7.1 Arquitetura adotada

| Parte | Escolha | Motivo |
|---|---|---|
| Bateria | Uma célula 18650 Li-ion de marca | Recarregável (TCC A, 4.2); a tensão cai de 4,2 V a ~3,3 V com a descarga, o que permite acompanhar a carga pela tensão |
| Recarga e proteção | Módulo TP4056 com DW01A | Carga por USB; corte por descarga profunda, sobrecorrente e curto |
| Regulador | RT9080-33 no pino 3V3; `VIN` desligado | 2 µA de consumo próprio e 600 mA ([DS9080-09](https://www.richtek.com/assets/product_file/RT9080/DS9080-09.pdf)). O regulador da placa (provável BL1117C) gasta 2–5 mA e precisa de ≥ 4,6 V na entrada ([datasheet](https://www.belling.com.cn/media/file_object/bel_product/BL1117C/datasheet/BL1117C_V.F1.9_en.pdf), p. 4) |
| Sensor de solo | Alimentado pelo GPIO25 (drive 3) só durante a leitura | 4,38 mA medidos; mesma faixa ar–água do 3V3 ([`sensores.md`](sensores.md) 4.5) |
| LED da placa | Retirado | Aceso o tempo todo; consumo desconhecido (marcação ilegível) |
| Coordenador | Na tomada, dentro de uma casa perto da horta | Não dorme; ~95–100 mA em recepção (ESP32 DS v5.3, tab. 5-4) esgotariam uma 18650 em ~1 dia |

Descartados: power bank (saída regulada esconde a carga e pode desligar com carga baixa), pilhas AA (não recarregáveis, custo recorrente), LiFePO4 direto no 3V3 (passa de 3,6 V carregada; tensão quase plana), HT7333 (250 mA, abaixo dos ≥ 0,5 A da tab. 13 do [ESP32-WROOM-32 Datasheet v3.8](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf)), MOSFET dos sensores (o GPIO bastou).

### 7.2 Corrente com o nó dormindo

| Parte | µA (mín.–máx.) | Origem |
|---|---|---|
| ESP32 em deep sleep | 10 | ESP32 Series Datasheet v5.3 |
| Regulador RT9080-33 | 2–4 | DS9080-09, "Quiescent Current" |
| Divisor 100k + 100k | 16,5–21 | Lei de Ohm, célula de 3,3 a 4,2 V |
| AHT20 em repouso | 0–0,25 | AHT20 Data Sheet V1.0 |
| TP4056 sem USB | 0,3–2 | [Datasheet TP4056](https://www.mikrocontroller.net/attachment/273612/TP4056.pdf) (cópia; conferir no do fabricante) |
| Proteção DW01A | 3–6 | [DW01 rev. 1.0](https://www.mikrocontroller.net/attachment/200783/DW01-G-DS-10_EN.pdf) (cópia; conferir no do fabricante) |
| Ponte USB CH9102X | 0–150 | 0 se só o USB a alimenta (hipótese); 150 µA se o 3V3 a alimenta sem USB (CH9102 Datasheet 1C, 6.2, ISLP máx.) |
| **Total** | **31,8–193,2** | |

Fora da conta: o regulador da placa alimentado "ao contrário" (saída no 3V3, entrada solta), sem dado no datasheet. O teste de descarga mede tudo junto.

### 7.3 Carga por ciclo acordado

| Ciclo | mC por ciclo | Composição |
|---|---|---|
| Normal | 5,39–10,05 | Ciclo com confirmação (3,10–6,82 mC, seção 3) + espera de ~410 ms em light sleep a 0,8 mA (0,33 mC) + sensor de solo 4,38 ± 0,45 mA por 0,5–0,6 s (1,97–2,90 mC) |
| Coordenador fora | 25,05–60,73 | Rádio de 236 ms (regra adaptativa, seção 5), escalado da medição de 197 ms → 19–48 mC; mais a espera e o sensor |

Tempo acordado medido com o chaveamento (bancada, 26/11/2026): 581–586 ms, contra ~171–178 ms antes.

### 7.4 Autonomia

Cálculo: [`ferramentas/autonomia.py`](../ferramentas/autonomia.py). **Capacidade útil de exemplo: 3000 mAh × 0,8 = 2400 mAh.** Trocar pelos valores do datasheet da célula comprada (capacidade nominal e fração disponível até `V_CRIT_MV`): `python ferramentas/autonomia.py <mAh> <fração>`.

| Cenário | Ciclos/dia | Acordado (mAh/dia) | Dormindo (mAh/dia) | Total (mAh/dia) | Gasto em 30 dias (mAh) | Autonomia (dias) |
|---|---|---|---|---|---|---|
| Normal, 10 min | 144 | 0,22–0,40 | 0,76–4,64 | 0,98–5,04 | 29–151 | 476–2452 |
| **Normal, 15 min** | **96** | 0,14–0,27 | 0,76–4,64 | **0,91–4,91** | **27–147** | **489–2646** |
| Normal, 30 min | 48 | 0,07–0,13 | 0,76–4,64 | 0,84–4,77 | 25–143 | 503–2874 |
| Coordenador fora, 15 min | 96 | 0,67–1,62 | 0,76–4,64 | 1,43–6,26 | 43–188 | 384–1677 |

Leitura:

1. **Quem decide é o consumo dormindo**, não o rádio: mesmo a 10 min, o ciclo acordado é menos de um terço do gasto diário no pior caso. Por isso o intervalo de 10, 15 ou 30 min quase não muda a autonomia.
2. **A maior incerteza é a ponte USB (0–150 µA)** e a fuga do regulador da placa (não documentada). Juntas, elas separam ~1,3 ano de ~7 anos de estimativa.
3. Com autonomias acima de um ano, a **autodescarga da célula** (datasheet do fabricante, TODO) passa a ser da mesma ordem do consumo do nó.
4. Antes desta arquitetura, só o sensor de solo ligado o tempo todo gastaria ~105 mAh/dia, e o regulador da placa, 48–120 mAh/dia.

### 7.5 O que o teste de 30 dias em campo consegue mostrar

Em 30 dias o nó gasta **27–147 mAh**, 1 a 6 % de uma célula de 2400 mAh úteis. **A bateria não chega ao fim no teste**: a tensão cai pouco. O teste mostra o começo da curva de descarga, e a autonomia é extrapolada e comparada com a estimativa acima. O erro absoluto da medida de tensão (±120 mV com o divisor, [`sensores.md`](sensores.md) 5.2) é sobretudo um deslocamento constante e pesa pouco na **variação** ao longo dos dias; a temperatura, sim, mexe na tensão da célula e na leitura do ADC, e precisa ser considerada na análise (o AHT20 registra a temperatura no mesmo pacote).
