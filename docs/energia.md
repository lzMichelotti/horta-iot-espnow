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

**Melhoria possível (decisão pendente):** com o coordenador fora, uma tentativa só custaria **+3,2–8,1 mC** em vez de +17–42 mC por ciclo. O ganho depende de quanto tempo o coordenador fica fora na prática. Com 1 tentativa, uma falha curta (interferência) atrasa a leitura um ciclo em vez de ser resolvida na hora.

## 6. Limitações

- Carga **estimada** (tempo medido × corrente do datasheet), não medida.
- Bancada a 10 s com logs; na produção (15 min, sem logs) os tempos de rádio são os mesmos, e o ciclo fica sem a impressão.
- Corrente da placa dormindo e bateria: ainda não definidas (Etapa 5, pendente).
- AHT20 do NÓ 1 em erro durante as medições da Etapa 7.1 (seção 3).
