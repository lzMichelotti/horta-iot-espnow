
## t1_entrega

**Nó (linhas [ENVIO])**

| Grandeza | Valor |
|---|---|
| Ciclos (pacotes montados) | 310 |
| Com ACK da camada MAC | 308 (99.4 %) |
| Tentativas por pacote (1/2/3) | 308 / 0 / 2 |
| Envio→callback, tentativa com ACK (ms) | mediana 3.10 · p5 3.06 · p95 5.45 · mín 3.05 · máx 30.78 (n=308) |
| Envio→callback, tentativa com FAIL (ms) | mediana 37.4 · p5 32.9 · p95 44.4 · mín 32.2 · máx 46.5 (n=6) |
| Tentativas sem callback no prazo | 0 |
| Erros de esp_now_send | nenhum |
| Ligar Wi-Fi + ESP-NOW, 1º ciclo do boot (ms) | mediana 96.0 · p5 96.0 · p95 96.0 · mín 96.0 · máx 96.0 (n=1) |
| Ligar Wi-Fi + ESP-NOW, demais ciclos (ms) | mediana 19.2 · p5 19.2 · p95 19.2 · mín 19.1 · máx 19.3 (n=309) |
| Tempo total de envio (ms) | mediana 3.2 · p5 3.1 · p95 5.8 · mín 3.1 · máx 175.9 (n=310) |
| Ciclo acordado (ms) | mediana 130.6 · p5 130.6 · p95 132.6 · mín 129.6 · máx 380.7 (n=310) |
| Boots do nó na captura | [8] |

**Coordenador (linhas [CSV])**

| Grandeza | Valor |
|---|---|
| Quadros recebidos | 307 |
| Classes | {'primeiro': 1, 'novo': 306} |
| Perdidos (lacunas de seq) | 0 |
| Taxa de entrega (aceitos / (aceitos + perdidos)) | 100.00 % |
| RSSI (dBm) | mediana -45 · p5 -46 · p95 -42 · mín -73 · máx -33 (n=307) |
| Ruído (dBm) | mediana -96 · p5 -96 · p95 -96 · mín -96 · máx -96 (n=307) |
| SNR = RSSI − ruído (dB) | mediana 51 · p5 50 · p95 54 · mín 23 · máx 63 (n=307) |
| tent_ant > 1 ou ANTERIOR_SEM_ACK recebidos | 0 |

**Cruzamento nó × coordenador (pacotes com o coordenador no ar)**

| Grandeza | Valor |
|---|---|
| Pacotes enviados na janela | 307 |
| Chegaram à aplicação do coordenador | 307 (100.00 %) |
| ACK no nó, mas ausente no coordenador | 0 |
| Sem ACK no nó, mas recebido no coordenador | 0 |


## t2_coord_desligado

**Nó (linhas [ENVIO])**

| Grandeza | Valor |
|---|---|
| Ciclos (pacotes montados) | 75 |
| Com ACK da camada MAC | 39 (52.0 %) |
| Tentativas por pacote (1/2/3) | 39 / 0 / 36 |
| Envio→callback, tentativa com ACK (ms) | mediana 3.11 · p5 3.07 · p95 5.15 · mín 3.06 · máx 5.64 (n=39) |
| Envio→callback, tentativa com FAIL (ms) | mediana 33.6 · p5 29.8 · p95 39.6 · mín 27.6 · máx 49.6 (n=108) |
| Tentativas sem callback no prazo | 0 |
| Erros de esp_now_send | nenhum |
| Ligar Wi-Fi + ESP-NOW, 1º ciclo do boot (ms) | mediana 109.9 · p5 109.9 · p95 109.9 · mín 109.9 · máx 109.9 (n=1) |
| Ligar Wi-Fi + ESP-NOW, demais ciclos (ms) | mediana 19.5 · p5 19.5 · p95 19.5 · mín 19.4 · máx 19.6 (n=74) |
| Tempo total de envio (ms) | mediana 5.5 · p5 3.1 · p95 160.8 · mín 3.1 · máx 168.0 (n=75) |
| Ciclo acordado (ms) | mediana 132.6 · p5 130.6 · p95 290.1 · mín 130.6 · máx 371.7 (n=75) |
| Boots do nó na captura | [9] |

**Coordenador (linhas [CSV])**

| Grandeza | Valor |
|---|---|
| Quadros recebidos | 39 |
| Classes | {'primeiro': 2, 'novo': 37} |
| Perdidos (lacunas de seq) | 0 |
| Taxa de entrega (aceitos / (aceitos + perdidos)) | 100.00 % |
| RSSI (dBm) | mediana -45 · p5 -46 · p95 -40 · mín -47 · máx -30 (n=39) |
| Ruído (dBm) | mediana -96 · p5 -96 · p95 -96 · mín -96 · máx -96 (n=39) |
| SNR = RSSI − ruído (dB) | mediana 51 · p5 50 · p95 56 · mín 49 · máx 66 (n=39) |
| tent_ant > 1 ou ANTERIOR_SEM_ACK recebidos | 2 |

**Cruzamento nó × coordenador (pacotes com o coordenador no ar)**

| Grandeza | Valor |
|---|---|
| Pacotes enviados na janela | 39 |
| Chegaram à aplicação do coordenador | 39 (100.00 %) |
| ACK no nó, mas ausente no coordenador | 0 |
| Sem ACK no nó, mas recebido no coordenador | 0 |


## t3_reinicio

**Nó (linhas [ENVIO])**

| Grandeza | Valor |
|---|---|
| Ciclos (pacotes montados) | 60 |
| Com ACK da camada MAC | 57 (95.0 %) |
| Tentativas por pacote (1/2/3) | 57 / 0 / 3 |
| Envio→callback, tentativa com ACK (ms) | mediana 3.10 · p5 3.06 · p95 5.54 · mín 3.06 · máx 6.26 (n=57) |
| Envio→callback, tentativa com FAIL (ms) | mediana 32.6 · p5 27.8 · p95 35.1 · mín 27.7 · máx 35.4 (n=9) |
| Tentativas sem callback no prazo | 0 |
| Erros de esp_now_send | nenhum |
| Ligar Wi-Fi + ESP-NOW, 1º ciclo do boot (ms) | mediana 96.0 · p5 94.8 · p95 97.2 · mín 94.7 · máx 97.4 (n=4) |
| Ligar Wi-Fi + ESP-NOW, demais ciclos (ms) | mediana 19.5 · p5 19.4 · p95 19.5 · mín 19.4 · máx 19.5 (n=56) |
| Tempo total de envio (ms) | mediana 3.2 · p5 3.1 · p95 13.3 · mín 3.1 · máx 149.4 (n=60) |
| Ciclo acordado (ms) | mediana 130.6 · p5 130.6 · p95 212.8 · mín 130.6 · máx 353.7 (n=60) |
| Boots do nó na captura | [10, 11, 12, 13] |

**Coordenador (linhas [CSV])**

| Grandeza | Valor |
|---|---|
| Quadros recebidos | 57 |
| Classes | {'primeiro': 1, 'novo': 53, 'reinicio': 3} |
| Perdidos (lacunas de seq) | 0 |
| Taxa de entrega (aceitos / (aceitos + perdidos)) | 100.00 % |
| RSSI (dBm) | mediana -45 · p5 -51 · p95 -44 · mín -73 · máx -31 (n=57) |
| Ruído (dBm) | mediana -96 · p5 -96 · p95 -96 · mín -96 · máx -96 (n=57) |
| SNR = RSSI − ruído (dB) | mediana 51 · p5 45 · p95 52 · mín 23 · máx 65 (n=57) |
| tent_ant > 1 ou ANTERIOR_SEM_ACK recebidos | 1 |

**Cruzamento nó × coordenador (pacotes com o coordenador no ar)**

| Grandeza | Valor |
|---|---|
| Pacotes enviados na janela | 57 |
| Chegaram à aplicação do coordenador | 57 (100.00 %) |
| ACK no nó, mas ausente no coordenador | 0 |
| Sem ACK no nó, mas recebido no coordenador | 0 |


## t4_duplicata

**Nó (linhas [ENVIO])**

| Grandeza | Valor |
|---|---|
| Ciclos (pacotes montados) | 38 |
| Com ACK da camada MAC | 35 (92.1 %) |
| Tentativas por pacote (1/2/3) | 35 / 0 / 3 |
| Envio→callback, tentativa com ACK (ms) | mediana 3.10 · p5 3.06 · p95 4.62 · mín 3.06 · máx 4.95 (n=35) |
| Envio→callback, tentativa com FAIL (ms) | mediana 31.9 · p5 28.4 · p95 36.1 · mín 27.6 · máx 36.5 (n=9) |
| Tentativas sem callback no prazo | 0 |
| Erros de esp_now_send | nenhum |
| Ligar Wi-Fi + ESP-NOW, 1º ciclo do boot (ms) | mediana 96.2 · p5 96.2 · p95 96.2 · mín 96.2 · máx 96.2 (n=1) |
| Ligar Wi-Fi + ESP-NOW, demais ciclos (ms) | mediana 19.9 · p5 19.8 · p95 19.9 · mín 19.7 · máx 19.9 (n=37) |
| Tempo total de envio (ms) | mediana 3.2 · p5 3.1 · p95 144.4 · mín 3.1 · máx 157.8 (n=38) |
| Ciclo acordado (ms) | mediana 130.5 · p5 130.5 · p95 277.0 · mín 130.5 · máx 348.7 (n=38) |
| Boots do nó na captura | [14] |

**Coordenador (linhas [CSV])**

| Grandeza | Valor |
|---|---|
| Quadros recebidos | 49 |
| Classes | {'primeiro': 1, 'novo': 34, 'duplicado': 7, 'antigo': 7} |
| Perdidos (lacunas de seq) | 0 |
| Taxa de entrega (aceitos / (aceitos + perdidos)) | 100.00 % |
| RSSI (dBm) | mediana -46 · p5 -46 · p95 -36 · mín -47 · máx -31 (n=49) |
| Ruído (dBm) | mediana -96 · p5 -96 · p95 -96 · mín -96 · máx -96 (n=49) |
| SNR = RSSI − ruído (dB) | mediana 50 · p5 50 · p95 60 · mín 49 · máx 65 (n=49) |
| tent_ant > 1 ou ANTERIOR_SEM_ACK recebidos | 2 |

**Cruzamento nó × coordenador (pacotes com o coordenador no ar)**

| Grandeza | Valor |
|---|---|
| Pacotes enviados na janela | 35 |
| Chegaram à aplicação do coordenador | 35 (100.00 %) |
| ACK no nó, mas ausente no coordenador | 0 |
| Sem ACK no nó, mas recebido no coordenador | 0 |


## t5_canal6

**Nó (linhas [ENVIO])**

| Grandeza | Valor |
|---|---|
| Ciclos (pacotes montados) | 25 |
| Com ACK da camada MAC | 0 (0.0 %) |
| Tentativas por pacote (1/2/3) | 0 / 0 / 25 |
| Envio→callback, tentativa com ACK (ms) | — |
| Envio→callback, tentativa com FAIL (ms) | mediana 35.2 · p5 29.1 · p95 43.0 · mín 27.9 · máx 48.6 (n=75) |
| Tentativas sem callback no prazo | 0 |
| Erros de esp_now_send | nenhum |
| Ligar Wi-Fi + ESP-NOW, 1º ciclo do boot (ms) | mediana 110.0 · p5 110.0 · p95 110.0 · mín 110.0 · máx 110.0 (n=1) |
| Ligar Wi-Fi + ESP-NOW, demais ciclos (ms) | mediana 19.6 · p5 19.6 · p95 19.6 · mín 19.6 · máx 19.6 (n=24) |
| Tempo total de envio (ms) | mediana 156.4 · p5 143.4 · p95 165.8 · mín 142.0 · máx 173.0 (n=25) |
| Ciclo acordado (ms) | mediana 284.6 · p5 274.6 · p95 299.2 · mín 269.6 · máx 361.7 (n=25) |
| Boots do nó na captura | [15] |


## t5_canal2

**Nó (linhas [ENVIO])**

| Grandeza | Valor |
|---|---|
| Ciclos (pacotes montados) | 25 |
| Com ACK da camada MAC | 23 (92.0 %) |
| Tentativas por pacote (1/2/3) | 23 / 0 / 2 |
| Envio→callback, tentativa com ACK (ms) | mediana 3.32 · p5 3.09 · p95 9.50 · mín 3.07 · máx 13.96 (n=23) |
| Envio→callback, tentativa com FAIL (ms) | mediana 36.3 · p5 28.6 · p95 42.3 · mín 27.9 · máx 44.0 (n=6) |
| Tentativas sem callback no prazo | 0 |
| Erros de esp_now_send | nenhum |
| Ligar Wi-Fi + ESP-NOW, 1º ciclo do boot (ms) | mediana 114.5 · p5 114.5 · p95 114.5 · mín 114.5 · máx 114.5 (n=1) |
| Ligar Wi-Fi + ESP-NOW, demais ciclos (ms) | mediana 19.9 · p5 19.8 · p95 19.9 · mín 19.8 · máx 19.9 (n=24) |
| Tempo total de envio (ms) | mediana 3.5 · p5 3.1 · p95 118.9 · mín 3.1 · máx 168.1 (n=25) |
| Ciclo acordado (ms) | mediana 131.6 · p5 130.6 · p95 264.8 · mín 130.6 · máx 368.7 (n=25) |
| Boots do nó na captura | [16] |

**Coordenador (linhas [CSV])**

| Grandeza | Valor |
|---|---|
| Quadros recebidos | 22 |
| Classes | {'primeiro': 1, 'novo': 21} |
| Perdidos (lacunas de seq) | 0 |
| Taxa de entrega (aceitos / (aceitos + perdidos)) | 100.00 % |
| RSSI (dBm) | mediana -48 · p5 -48 · p95 -47 · mín -49 · máx -47 (n=22) |
| Ruído (dBm) | mediana -96 · p5 -96 · p95 -96 · mín -96 · máx -96 (n=22) |
| SNR = RSSI − ruído (dB) | mediana 48 · p5 48 · p95 49 · mín 47 · máx 49 (n=22) |
| tent_ant > 1 ou ANTERIOR_SEM_ACK recebidos | 0 |

**Cruzamento nó × coordenador (pacotes com o coordenador no ar)**

| Grandeza | Valor |
|---|---|
| Pacotes enviados na janela | 22 |
| Chegaram à aplicação do coordenador | 22 (100.00 %) |
| ACK no nó, mas ausente no coordenador | 0 |
| Sem ACK no nó, mas recebido no coordenador | 0 |

