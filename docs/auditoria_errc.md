# Auditoria do parecer da ERRC 2026

Análise de cada ponto do parecer do artigo "Monitoramento de Hortas Urbanas sem Infraestrutura de Rede" (ERRC 2026), com testes de bancada feitos em 13/10/2026 para verificar se o ponto procede **neste** projeto e se justifica mudar algo antes do campo.

Dados brutos: [`dados/auditoria_errc/brutos/`](../dados/auditoria_errc/brutos/). Firmware dos testes: [`experimentos/radio/`](../experimentos/radio/). Análise: `ferramentas/analisar_auditoria.py` (tempos e entrega) e `ferramentas/estimar_energia.py` (carga e autonomia).

## Resumo

| Ponto do parecer | Procede? | Evidência | Mudança no projeto |
|---|---|---|---|
| Wi-Fi tem consumo excessivo | **Não para o ESP-NOW** como usado aqui | Rádio ligado 23 ms/ciclo na produção; o rádio é ~0,02–0,06 mAh/dia | Nenhuma no protocolo. Explicar e quantificar no artigo |
| BLE/Zigbee aumentariam a vida da bateria | **Não neste hardware** | BLE: rádio 205–965 ms/ciclo (6–27× o ESP-NOW), sem confirmação de entrega, piora o coordenador. Zigbee: o ESP32 não tem rádio 802.15.4 | Nenhuma. Comparação entra no artigo |
| LoRa (boa cobertura) | Parcial: só para alcance | Não vem no ESP32; mesmo em SF7 gasta mais por pacote; LoRaWAN depende de gateway e backhaul | Nenhuma. Fica como trabalho futuro para hortas grandes |
| eMules / drones | Ideia válida, outra camada | O próprio projeto já usa "mula de dados": quem visita a horta leva os dados no celular | Nenhuma. Citar no artigo |
| Caracterizar carga e estimar vida útil | **Procede** | O artigo não tinha números próprios | Já feito aqui (seção 3); entra no artigo |
| Trabalhos relacionados sem método | **Procede** | Seção 2 com subseção única; sem string, bases, período, data | Reescrever a seção 2 (seção 6 deste documento) |
| Limitações resolvíveis com protocolos simples | **Procede em parte** | CCMP custa +1 ms, mas **não bloqueia** quadros sem cifra (teste E4) | **Feito:** HMAC-SHA256 no pacote (protocolo v2); ataques H1–H3 recusados (seção 5.1) |
| Falta uma declaração exigida pelo evento | Procede | — | Acrescentar ao artigo (fora do repositório) |

## 1. Testes realizados

Montagem: NÓ 1 (com sensores, não usados nos testes de rádio) e COORD na mesma bancada, ~1 m, canal 1. O nó acorda do deep sleep a cada 3 s, liga o rádio, envia 22 B (o tamanho do `PacoteLeitura`), desliga e dorme. ~100 ciclos por teste (E3, E4: 40; E7, E8: ~80). O primeiro ciclo de cada captura é descartado (as duas placas reiniciam juntas ao abrir as portas seriais). O firmware de teste não tem as otimizações da Etapa 5 (QIO 80 MHz, light sleep), por isso os tempos absolutos são maiores que os da produção; **a comparação entre modos é válida porque todos usam a mesma base**.

| # | Nó | Coordenador | Entrega | Retransmissões | Rádio ligado (mediana) | Acordado (mediana) |
|---|---|---|---|---|---|---|
| E1 | ESP-NOW | SoftAP + ESP-NOW | **98/98 (100 %)** | 0 % | 35,7 ms | 101 ms |
| E2 | ESP-NOW + CCMP | idem, peer cifrado | **98/98 (100 %)** | 0 % | 36,7 ms | 102 ms |
| E3 | ESP-NOW, **LMK errada** | idem, peer cifrado | **0/39** (ACK no nó: 39/39) | 0 % | 38,2 ms | 103 ms |
| E4 | ESP-NOW **sem cifra**, MAC do peer cifrado | idem, peer cifrado | **39/39 aceitos** | 0 % | 36,0 ms | 101 ms |
| E5 | ESP-NOW | SoftAP + ESP-NOW **+ varredura BLE** | 95/98 (96,9 %) | **35 %** (2–3 envios) | 59,6 ms (p95 172) | 125 ms |
| E6 | BLE, controlador (VHCI), 100 ms de anúncio | SoftAP + ESP-NOW + BLE | 98/98 (100 %) | não há ACK | **275 ms** | 344 ms |
| E7 | BLE, controlador, 30 ms de anúncio | idem | 77/79 (97,5 %) | não há ACK | **205 ms** | 274 ms |
| E8 | BLE, biblioteca Arduino (Bluedroid), 100 ms | idem | 78/78 (100 %) | não há ACK | **965 ms** | 1037 ms |
| E9a | ESP-NOW | SoftAP + ESP-NOW + **celular conectado** pedindo uma página | **157/157 (100 %)** | **0 %** | 36,7 ms (envio 4,4 ms) | 103 ms |
| E9b | ESP-NOW | idem **+ varredura BLE** | 157/157 (100 %) | **22 %** (2–3 envios) | 63,8 ms (envio 31,5 ms, p95 139) | 130 ms |

Memória livre na COORD (heap): 229 KB só com SoftAP + ESP-NOW; **150 KB com o controlador BLE varrendo (−79 KB)**.

Firmware (flash usada): nó ESP-NOW 905 KB; nó BLE só controlador 576 KB; nó BLE Bluedroid 1179 KB; COORD + BLE: +146 KB.

E9a e E9b: celular conectado ao SoftAP `horta-exp`, com o navegador recarregando a cada 2 s uma página servida pela COORD (`WebServer` do núcleo), simulando o app. A página foi atendida em 3,7 ms em média (E9a; máximo 4,4 ms) e em 4,1 ms (E9b; máximo 6,2 ms). Nos dois casos o celular ficou conectado até o fim (~6 min com tráfego). O "C1, instável" da documentação não apareceu como queda de conexão nessa janela, mas o efeito sobre o ESP-NOW se repetiu: com o BLE ligado, 22 % dos envios precisaram de retransmissão. **Conclusão: a arquitetura atual (SoftAP + celular + ESP-NOW, sem BLE) funcionou sem perda nem retransmissão.**

## 2. "IEEE 802.11 não tem bom desempenho para IoT e consome energia demais"

O argumento vale para Wi-Fi **com associação** (procurar AP, autenticar, DHCP, TCP), que custa segundos de rádio ligado: Kviesis et al. (2023) mediram ~5 s com Wi-Fi contra milissegundos com ESP-NOW. O ESP-NOW usa só quadros de gerenciamento 802.11 (*action*, *vendor-specific*), sem associação nem IP ([ESP-IDF v5.5, ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/network/esp_now.html)).

Medido na produção (Etapa 5, `p4_4b_qio_80m`, mediana de 11 ciclos): o nó fica **170 ms acordado**, dos quais **22,6 ms com o rádio ligado** (ligar 19,8 + enviar 1,8 + desligar 1,0), com 0,7 ms de quadro no ar (65 B a 1 Mbps). O resto é boot (47 ms) e sensores (97,5 ms, 90 ms deles em light sleep).

Com as correntes do [ESP32 Series Datasheet v5.3](https://documentation.espressif.com/esp32_datasheet_en.pdf) (tab. 4-2 e 5-4), aplicadas como **faixa** a cada fase (sem medição de corrente):

| Parcela do ciclo | Carga (mC) |
|---|---|
| CPU (boot, leitura) | 1,70 – 3,86 |
| Rádio | 0,83 – 2,36 |
| Light sleep | 0,07 |
| **Total** | **2,60 – 6,29** |

Com 96 ciclos/dia, o rádio gasta **0,02–0,06 mAh/dia**. Só o deep sleep do chip (10 µA) já gasta 0,24 mAh/dia.

**Conclusão:** neste projeto o rádio não é o problema. Quem decide a autonomia é a **corrente da placa dormindo** (regulador, ponte USB, LED da DevKit), que ainda não medimos (TODO em `docs/placas.md`). Sensibilidade, para 2000 mAh:

| Corrente da placa dormindo | Autonomia |
|---|---|
| 10 µA (só o chip, datasheet) | > 10 anos (a autodescarga da bateria domina) |
| 0,1 mA (hipótese) | ~2 anos |
| 1 mA (hipótese) | ~83 dias |
| 5 mA (hipótese) | ~17 dias |

Esses números são estimativa, não medição; o artigo deve dizer isso e apontar a medição em campo pela queda de `alimentacao_mv` (já prevista).

## 3. BLE, Zigbee e LoRa

### 3.1 BLE (testado)

- **Custo de ligar**: o controlador Bluetooth leva **172 ms** para iniciar (E6, E7) e a pilha Bluedroid da biblioteca Arduino, **645 ms + 219 ms para desligar** (E8), contra **30 ms** do Wi-Fi/ESP-NOW. No ESP32, começar do deep sleep a cada ciclo custa caro no BLE.
- **Carga do rádio por ciclo** (mesma faixa de correntes): ESP-NOW 1,2–3,7 mC; BLE controlador 6,4–27,7 mC; BLE Bluedroid 29–97 mC. **O melhor caso do BLE é pior que o pior caso do ESP-NOW.**
- **Sem confirmação**: anúncio BLE não tem ACK; o nó não sabe se entregou, e a taxa de entrega só cai menos se anunciar por mais tempo (30 ms: 97,5 %; 100 ms: 100 %, a 1 m). Para ter ACK seria preciso conexão BLE, que custa ainda mais tempo acordado.
- **Alcance**: orçamento de enlace (potência + sensibilidade, datasheet do ESP32) de **117,5 dB** no 802.11b a 1 Mbps (+19,5 dBm, −98 dBm) contra **94 dB** no BLE padrão (0 dBm, −94 dBm) ou 103 dB no máximo (+9 dBm). Na literatura, Eridani et al. (2021) mediram 15 m (Bluetooth) × 185 m (ESP-NOW) nas mesmas placas, e Labib et al. (2021) relatam ESP-NOW com até 15× o alcance do BLE (ambos revisados por Becker et al., 2025).
- **Coordenador**: o coordenador teria de escutar BLE e manter o SoftAP do app. A documentação oficial de coexistência ([ESP-IDF v5.5, RF Coexistence](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-guides/coexist.html)) marca SoftAP *conectado* + BLE como **"C1: suportado, mas instável"** e recepção ESP-NOW + BLE como **"S: estável só em modo STA"**. Medido (E5): com a varredura BLE ligada, 35 % dos envios ESP-NOW precisaram de retransmissão (0 % sem BLE), a mediana do envio foi de 3,4 para 27,3 ms e o heap caiu 79 KB. Com um celular conectado e pedindo páginas (E9b), 22 % de retransmissões, contra 0 % sem BLE (E9a).

### 3.2 Zigbee

O ESP32 clássico (o chip das DevKits do projeto) **não tem rádio IEEE 802.15.4**: o datasheet lista só Wi-Fi + Bluetooth/BLE, e no ESP-IDF 5.5.5 instalado `SOC_IEEE802154_SUPPORTED` só existe para o ESP32-C6 e o ESP32-H2. Usar Zigbee exigiria trocar de chip ou acrescentar um módulo nos nós e no coordenador, e o coordenador continuaria precisando de Wi-Fi para o celular.

### 3.3 LoRa

Não vem integrado ao ESP32. Pelo [datasheet do SX1276](https://cdn.sparkfun.com/assets/7/7/3/2/2/SX1276_Datasheet.pdf) (Rev. 7, tab. 6 e seção 4.1.1.7), para 22 B (BW 125 kHz, CR 4/5):

| SF | Tempo no ar | Carga só do transceptor (+17 / +20 dBm) |
|---|---|---|
| 7 | 56,6 ms | 4,9 / 6,8 mC |
| 9 | 206 ms | 17,9 / 24,7 mC |
| 12 | 1483 ms | 129 / 178 mC |

Mesmo em SF7, **só o transceptor** gasta mais que a fase de rádio inteira do ESP-NOW (0,8–2,4 mC), e o ESP32 continua acordado esperando o fim da transmissão. O ganho do LoRa é **alcance** (sensibilidade de −123 a −136 dBm), útil para hortas de centenas de metros, não para a horta urbana do projeto. "Boa cobertura de redes LoRa" se refere a LoRaWAN com gateway e servidor de rede, que dependem de Internet; LoRa ponto a ponto exigiria um módulo (SX1276/SX1262) em cada nó e no coordenador. **Impacto: não justifica mudar agora.** Vale citar como extensão para hortas maiores (os próprios Allen Ronald et al., 2025, sugerem isso).

### 3.4 eMules (drones)

Uma mula de dados (*data mule*) que passa e coleta resolve áreas sem cobertura. Na arquitetura do projeto, essa função **já existe no último trecho**: o coordenador guarda o histórico e quem visita a horta leva os dados no celular. Usar mulas entre nó e coordenador exigiria que o nó escutasse (rádio de despertar ou janelas de recepção), o que custa energia; numa horta de dezenas de metros, a estrela direta é mais simples. Cabe como argumento e trabalho futuro.

## 4. Alcance esperado (correção ao artigo)

O artigo espera "comunicação confiável a pelo menos 100 m sem obstáculos". Becker et al. (2025, IEEE/IFIP WONS), com ESP-NOW a 1 Mbps e antenas a 0,95 m (altura típica de sensor), mediram **entrega acima de 99 % até 56 m em terreno aberto**, oscilando entre 56 e 70 m e **zero acima de 70 m**. Os 250 m de Kviesis et al. (2023) foram com outra montagem. Recomendo trocar a meta para "até ~50 m em campo aberto, a verificar", ou citar as duas referências e justificar a altura das antenas.

## 5. Limitações da Seção 5: o que dá para resolver agora

### 5.1 Segurança

Achados dos testes E2–E4:

- O CCMP nativo funciona e custa só **+1 ms** de rádio (36,7 × 35,7 ms) e +16 B no quadro. Um nó com a chave errada não entrega nada à aplicação (E3).
- **Mas o coordenador aceita quadros sem cifra vindos do MAC de um peer cifrado (E4: 39/39)**, e o callback de recepção (`esp_now_recv_info_t`, ESP-IDF 5.5) não informa se o quadro veio cifrado. A documentação oficial não trata desse caso. Como o MAC de origem é forjável, **ligar só o CCMP não impede injeção**.
- O CCMP limita o coordenador a **7 peers cifrados** (padrão; até 17 recompilando o ESP-IDF), o que conflita com o "até 20 pares" do artigo.
- O ACK do ESP-NOW é dado antes da decifragem (E3: ACK 39/39, entrega 0/39).

**Decisão (implementada, protocolo v2):** autenticar na aplicação. Detalhes em [`protocolo.md`, seção 12](protocolo.md):

- HMAC-SHA256 sobre os 22 B do pacote (inclui `boot` e `seq`), truncado em **16 B**. A RFC 2104 (seção 5) recomenda não truncar abaixo de metade da saída do hash nem de 80 bits; os 8 B cogitados antes ficariam abaixo disso. O pacote passa de 22 para **38 B** (quadro de ~81 B, ~0,84 ms no ar a 1 Mbps).
- Chave de 32 B **por nó**, derivada de uma chave mestra que só o coordenador tem: `chave_no = HMAC-SHA256(mestra, MAC STA do nó)`. O coordenador recalcula a chave pelo MAC de origem e não precisa de tabela de chaves; o nó só conhece a própria. Geração: `ferramentas/chaves.py` (os `segredos.h` ficam fora do Git).
- mbedTLS do ESP-IDF, com o acelerador SHA do ESP32 (`CONFIG_MBEDTLS_HARDWARE_SHA=y`): **sem dependência nova**. Conferido com os vetores da RFC 4231. Custo medido na placa (`no_sensor/test/test_hmac_esp32`): **assinar 136 µs**, validar 137 µs, menos de 0,1 % do ciclo acordado (170 ms).
- Antirrepetição: o coordenador recusa `(boot, seq)` que não avance (`duplicado`, `antigo`, `boot_antigo`). Ao religar, ele retoma o último `(boot, seq)` de cada nó a partir do **próprio histórico**, então um pacote gravado antes do reinício também não passa.
- Funciona com qualquer número de nós, sem cadastrar peers, e pode coexistir com o CCMP se a confidencialidade for desejada.

Ataques repetidos contra a v2 (13/10/2026, COORD de produção, NÓ 1 a ~1 m, `dados/auditoria_errc/brutos/h*`):

| # | Ataque | Recebidos | Aceitos | Rejeição na COORD |
|---|---|---|---|---|
| H1 | Firmware real do nó com **chave aleatória** (atacante sem a mestra), MAC legítimo | 5 | **0** | `assinatura_invalida` |
| H2 | Pacote **sem assinatura** (22 B, como o E4) do MAC legítimo | 6 | **0** | `versao_desconhecida` |
| H3 | **Repetição** de pacotes assinados válidos (o atual e o anterior) | 2 repetidos | **0** gravados | classe `duplicado` / `antigo` |
| — | Reinício da COORD com o nó enviando | — | — | 1º pacote classificado `novo` (não `primeiro`): `(boot, seq)` retomado do histórico |

O ACK do ESP-NOW continua sendo da camada de enlace, dado antes de qualquer validação (como no E3): um nó com a chave errada "entrega" do seu ponto de vista. Isso não afeta a integridade do histórico; o diagnóstico fica no `[RESUMO]` da COORD (`rejeitados`).

A rede do coordenador **já usa WPA2-PSK** (`segredos.h`); o artigo diz que ela é "acessível a qualquer pessoa ao seu alcance", o que não confere. A API HTTP (etapa 8) vai usar um token gerenciado pelo app, sem o usuário digitar senha.

### 5.2 Coordenador como ponto único de falha

O nó tem 8 KB de memória RTC lenta, que sobrevive ao deep sleep. A produção usa ~0,2 KB (`.rtc.data` + `.rtc.force_slow` no ELF). Sobram ~7,8 KB, ou **~350 leituras de 22 B (~3,6 dias a cada 15 min)** guardadas no nó até o coordenador voltar. O par `(boot, seq)` já permite ao coordenador ordenar e deduplicar. Reenvio em lote: até 11 leituras por quadro no ESP-NOW v1 (250 B). Viável e simples; decidir se entra antes do campo.

### 5.3 Escala

Por canal, cada nó ocupa ~0,7 ms a cada 15 min (fração de 8×10⁻⁷ do tempo): o ar não é o limite. O limite prático é o coordenador (gravação no LittleFS de ~60 ms por registro, fila de 16; medido na Etapa 7) e os 20 peers (só com criptografia nativa). Um teste de rajada com vários MACs simulados pode medir isso, se for necessário para o texto.

## 6. Trabalhos relacionados

O parecer pede: seção única, critérios explícitos na tabela e método de busca (string, bases, período, data). Os quatro trabalhos do TCC A não têm método registrado.

Proposta de protocolo (executar e registrar a data):

- **String**: `("ESP-NOW" OR "ESP32") AND ("agriculture" OR "garden" OR "horticulture" OR "horta") AND ("wireless sensor network" OR "IoT") AND ("offline" OR "local storage" OR "without internet" OR "edge")`, com versão em português para a SOL.
- **Bases**: IEEE Xplore, ACM Digital Library e Scopus (via Portal de Periódicos CAPES/UFSM), SBC OpenLib (SOL); Google Scholar como complemento.
- **Período**: 2019–2026.
- **Inclusão**: rede de sensores com ESP32/ESP8266 para monitoramento agrícola ou ambiental, com avaliação experimental. **Exclusão**: só simulação, sem comunicação entre nós, texto completo indisponível.
- **Critérios da tabela**: protocolo entre nós; operação sem Internet; persistência local; acesso local pelo usuário; nós a bateria com consumo medido; avaliação em campo; custo reportado.

Candidatos encontrados nesta auditoria (conferir o texto completo antes de citar):

- Becker et al. (2025), *ESP-NOW Performance in Outdoor Environments*, IEEE/IFIP WONS, pp. 41–48 (texto completo lido).
- Siswanto, Dewintari e Sapar, *ESP-NOW based multi-node IoT system for agricultural solar dryers with hybrid offline-online monitoring*, IJRES 15(2):426–438, DOI 10.11591/ijres.v15.i2.pp426-438. **Atenção:** opera localmente sem Internet, com sincronização opcional; a linha "Operação sem conexão à Internet: só este trabalho" da tabela atual precisa ser revista.
- Urazayev et al. (2023), *Indoor performance evaluation of ESP-NOW*, IEEE SIST.
- Eridani, Rochim e Cesara (2021), *Comparative performance study of ESP-NOW, Wi-Fi, Bluetooth...*, IEEE iSemantic.
- Labib et al. (2021), *An efficient networking solution for extending and controlling wireless sensor networks using low-energy technologies*, PeerJ Computer Science 7:e780.
- Contexto nacional (SOL): Santos, Corado e Araújo (2019), WCAMA, DOI 10.5753/wcama.2019.6426; Pereira e Campos (2025), ENCompIF, DOI 10.5753/encompif.2025.8279.

## 7. Correções factuais no texto atual do artigo

- "Cada mensagem contém a identificação do nó": o nó é identificado pelo **MAC de origem**; não há campo de id.
- "1 MB … cerca de sete meses": o histórico tem **1920 KB**; registros de 48 B, ~28 mil registros, **~146 dias** com 2 nós a cada 15 min.
- "Cada nó fará leituras a cada 30 minutos": o período decidido é **15 min**.
- "período acordado de cerca de três segundos" (de Kviesis): o medido aqui é **~170 ms**.
- "ESP-NOW admite até 20 pares": para **receber** sem cifra não é preciso cadastrar o remetente; com criptografia nativa, 7 (padrão).
- "a rede Wi-Fi criada pelo coordenador é acessível a qualquer pessoa": usa **WPA2-PSK**.
- DS3231: adiado (Etapa 6); hoje a hora vem do relógio provisório (acerto pela serial e hora herdada).
- "Seção 2 com subseção 2.1": fundir.
