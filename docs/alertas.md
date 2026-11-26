# Regras locais e alertas (Etapa 9)

O coordenador gera alertas simples a partir das leituras e os expõe em `GET /api/v1/alertas` ([`api.md`](api.md), 4.9), sem mudança no formato que o app já consome. Código: lógica pura em [`coordenador/lib/alertas`](../coordenador/lib/alertas/src/alertas.h) (testada no PC), ligação ao firmware em [`coordenador/src/regras.cpp`](../coordenador/src/regras.cpp), limiares em [`coordenador/src/config.h`](../coordenador/src/config.h) (seção "Alertas").

## 1. Decisões

| Decisão | Escolha | Por quê |
|---|---|---|
| Onde os alertas vivem | **RAM, reconstruídos do histórico no boot** | o histórico já guarda todas as leituras e o boot já lê todos os registros: as regras são reaplicadas nessa leitura (observador do histórico), com o início certo, **sem nenhuma escrita a mais na flash**. Persistir na NVS ou no LittleFS duplicaria o que o histórico já tem |
| Contra oscilação | **banda de valor + N leituras seguidas** | a banda evita ativar e resolver em sequência perto do limiar; as N leituras evitam ativar por uma leitura espúria. Com um ciclo de 15 min, "tempo mínimo" é contado em leituras |
| Modo da bateria | **decidido no nó** (histerese dele, Etapa 5) e enviado nos **bits 4–5 de `flags`** | sem mudar o tamanho do pacote, da fila nem do registro; `VERSAO` continua 3 ([`protocolo.md`](protocolo.md), seção 14). O coordenador não repete os limiares de tensão |
| Último aviso | ao **entrar** no modo crítico, o nó envia um último pacote (modo crítico) antes de silenciar | o coordenador distingue "bateria crítica, vai silenciar" de "nó sem comunicar" |
| Hora do alerta | a da **leitura** que o começou, no mesmo modelo de `/leituras` (`valida`, `reconstruida`, `sem_hora`), mais `ha_s` pelo relógio interno | sem o DS3231 (Etapa 6), a hora pode não estar acertada; um alerta de um boot sem hora passa a `reconstruida` quando a hora é acertada no mesmo boot |
| API | só campos novos (v1 compatível); `/alertas` lista **só os ativos** por padrão | um app que não conhece o campo `estado` não mostra resolvidos como atuais |
| Nomes na API | `modo_energia` no nó e `solo_classe` em **cada leitura**, com `seco`, `umido` e `muito_umido` | são os que o app já esperava (repositório do app, `frontend/src/features/coordenador/tipos.ts`, branch `feat/coordenador`): o app passa a usá-los sem mudança |
| Onde roda | no `loop()`, no observador do histórico (a mesma tarefa que grava) | nada no callback de recepção do ESP-NOW |

## 2. Regras

| `tipo` | Ativa quando | Resolve quando | Valor |
|---|---|---|---|
| `solo_seco` | solo < `ALERTA_SOLO_SECO` por `ALERTA_LEITURAS_PARA_ATIVAR` leituras seguidas | solo ≥ seco + banda | % (sem o limite de 0–100 %) |
| `solo_encharcado` | solo > `ALERTA_SOLO_ENCHARCADO` por `ALERTA_LEITURAS_ENCHARCADO` leituras | solo ≤ encharcado − banda | % |
| `temperatura_alta` | T > `ALERTA_TEMP_ALTA` por N leituras | T ≤ alta − banda | °C |
| `temperatura_baixa` | T < `ALERTA_TEMP_BAIXA` por N leituras | T ≥ baixa + banda | °C |
| `bateria_baixa` | o nó informa modo `economia` ou `critico` (1 leitura: a histerese é do nó) | modo `normal` | mV |
| `bateria_critica` | o nó informa modo `critico` (o último pacote antes de silenciar) | modo diferente de `critico` | mV |
| `sem_comunicar` | nenhuma leitura há mais que `ALERTA_SEM_COMUNICAR_CICLOS` × o período do nó **no modo de bateria dele** | qualquer leitura nova, inclusive atrasada da fila do nó | s calado |
| `erro_sensor_ar` | temperatura ou umidade do ar em `erro`/`fora_de_faixa` por `ALERTA_ERROS_PARA_ATIVAR` leituras | 1 leitura com as duas `ok` | — |
| `erro_sensor_solo` | solo em `erro`/`fora_de_faixa` por N leituras | 1 leitura `ok` | — |
| `erro_sensor_alimentacao` | idem, só com `ALERTA_ERRO_ALIMENTACAO` ligado | 1 leitura `ok` | — |

- Solo sem calibração, ou com o sensor em erro: os alertas de solo ficam como estão (não ativam nem resolvem) e `solo_classe` vai `null`.
- Temperatura em erro: idem para os alertas de temperatura (o erro tem o seu próprio alerta).
- Leitura com o modo da bateria `desconhecido` (firmware anterior): não mexe nos alertas de bateria.
- O início de um alerta é a **primeira** leitura da sequência que o ativou.
- Leituras atrasadas da fila do nó chegam em ordem de `seq` (o coordenador só aceita em ordem, [`protocolo.md`](protocolo.md), seção 13) e entram nas regras como qualquer outra.

## 3. Classificação do solo

Pelo % da calibração **de cada nó** (cadastro na NVS; 0 % = solo seco ao toque, 100 % = ~1 h após a rega, [`sensores.md`](sensores.md), 4.3). Vai em `solo_classe` de cada leitura da API:

| `solo_classe` | Faixa | Texto no app |
|---|---|---|
| `seco` | abaixo de `ALERTA_SOLO_SECO` (25 %) | "Solo seco" |
| `umido` | de 25 % a 90 % | "Solo úmido" |
| `muito_umido` | acima de `ALERTA_SOLO_MUITO_UMIDO` (90 %) | "Solo muito úmido" |

Os dois limites são os provisórios que o app guardava (`classificacao.ts`, `LIMITES_PROVISORIOS`), agora no coordenador. O app já usa a `solo_classe` quando ela vem preenchida (`classeSolo()`), e só calcula localmente quando ela é `null`. O alerta `solo_encharcado` tem limite próprio (105 %), acima da classe `muito_umido`: o solo passa de 90 % depois de toda rega.

## 4. Limiares (todos provisórios)

| Constante (`config.h`) | Valor | Origem |
|---|---|---|
| `ALERTA_SOLO_SECO_DECIMOS` | 25 % | o provisório do app (`LIMITES_PROVISORIOS.soloSecoAbaixoPct`); **sem fonte agronômica**: a definir com o pessoal do projeto de AUP |
| `ALERTA_SOLO_MUITO_UMIDO_DECIMOS` | 90 % | o provisório do app (`soloMuitoUmidoAcimaPct`); a definir com AUP |
| `ALERTA_SOLO_ENCHARCADO_DECIMOS` | 105 % | logo após a rega a leitura chegou a ~110 % (584 mV) e voltou a ~100 % em 1 h ([`sensores.md`](sensores.md), 4.3–4.4) |
| `ALERTA_SOLO_BANDA_DECIMOS` | 5 % | ~12× o ruído da média de 64 leituras (3,2 mV ≈ 0,4 % com a calibração 1400/655; teste 3 de 22/11/2026) |
| `ALERTA_TEMP_ALTA_C100` | 35 °C | **sem fonte**: a definir com AUP |
| `ALERTA_TEMP_BAIXA_C100` | 5 °C | **sem fonte**: a definir com AUP |
| `ALERTA_TEMP_BANDA_C100` | 1 °C | > 3× a precisão do AHT20 (±0,3 °C, datasheet tab. 1) |
| `ALERTA_LEITURAS_PARA_ATIVAR` | 2 (30 min) | contra uma leitura espúria isolada |
| `ALERTA_LEITURAS_ENCHARCADO` | 4 (1 h) | a rega deixa o solo acima de 100 % por ~1 h ([`sensores.md`](sensores.md), 4.4) |
| `ALERTA_ERROS_PARA_ATIVAR` | 2 | idem |
| `ALERTA_ERRO_ALIMENTACAO` | desligado | o divisor está desmontado: a alimentação chega sempre `fora_de_faixa` (Etapa 5, passo 10) |
| `ALERTA_SEM_COMUNICAR_CICLOS` | 3 períodos (45 min no modo normal) | tolera 2 ciclos sem ACK; a fila do nó guarda e reenvia as leituras ([`protocolo.md`](protocolo.md), 13) |
| Períodos do nó (`ALERTA_PERIODO_NO_S`, `ALERTA_FATOR_ECONOMIA`, `ALERTA_PERIODO_CRITICO_S`) | 15 min, ×4, 1 h | cópia de `no_sensor/src/config.h`; mudar lá = mudar aqui |
| Bateria (`V_TX_MV`, `V_CRIT_MV` no nó) | 0 = desligado | a bateria ainda não foi escolhida (Etapa 5, passo 10) |

O env `teste_alertas` do coordenador usa os períodos da bancada (10 s; economia ×2; crítico 30 s, os do env `teste_tensao` do nó).

## 5. Custo

| Grandeza | Valor |
|---|---|
| RAM estática | **+4728 B** (56 064 → 60 792 B; a tabela de alertas tem 4704 B: 8 nós × 10 regras) |
| Flash | +5,8 KB (1 065 944 → 1 071 724 B) |
| Avaliação por leitura | ~0,2 ms (218 µs medidos na placa) |

## 6. Testes

**No PC** (`cd coordenador && pio test -e native`): 14 testes em `test_alertas` (classificação, N leituras e início, leitura isolada, banda sem oscilar, solo sem calibração ou em erro, encharcado, temperaturas, bateria pelo modo, modo desconhecido, sem_comunicar com leitura atrasada, erros de sensor, erro da alimentação desligado, tabela) e 1 novo em `test_cadastro` (% sem limite). No nó, 2 testes novos em `test_protocolo` (bits do modo e assinatura).

**Na placa** (22/11/2026; COORD `5AC9001351` no env `teste_alertas`, nó `horta1` `5AC9009194`; dados em [`dados/etapa9/brutos/`](../dados/etapa9/brutos/)): ver seção 7.

## 7. Resultados na placa

COORD no env `teste_alertas` (sem_comunicar com os períodos da bancada: 30 s no modo normal, 90 s no crítico); nó `horta1` no env `bancada`, salvo onde indicado. Log da COORD com o instante do PC ([`ferramentas/coord_log.py`](../ferramentas/coord_log.py)).

| Teste | Como | Resultado |
|---|---|---|
| A. Solo seco | sensor no ar (~2160 mV, −101 %) por ~20 min; depois na água | ativou com 2 leituras; **ficou ativo ~20 min sem oscilar**; resolveu na 1ª leitura na água (36,5 %, ainda mergulhando), 50 ms depois da gravação; depois estável em ~95 % sem reativar |
| B. Bateria | nó no env `teste_tensao` (rampa 3700 → 3100 → 3700 mV, 50 mV por ciclo; V_TX 3500, V_CRIT 3300) | `bateria_baixa` ativou em 3450 mV (economia); `bateria_critica` em 3250 mV (o **último pacote** antes de silenciar, modo crítico nos bits 4–5); `sem_comunicar` ativou com 95 s calado; voltando a 3350 mV, os dois resolveram; `bateria_baixa` resolveu em 3550 mV (normal, com a histerese do nó). Duas voltas iguais |
| C1. Nó desligado | cabo USB do nó solto por ~1 min | `sem_comunicar` ativou com 34 s (limite 30 s, verificação a cada 5 s); resolveu no 1º pacote depois de religar (`reinicio`), 50 ms depois |
| C2. Nó fora de alcance (fila) | `alertas surdo 60`: a COORD ignora os pacotes por 60 s | `sem_comunicar` ativou com 32 s; a leitura nova foi recusada (`fora_de_ordem`) e o nó reenviou a fila em ordem (seq 15–20, idade 61 → 10 s); a **1ª atrasada resolveu** o alerta; 0 perdidas, 0 duplicadas |
| D. Sensor em erro | fio SDA do AHT20 solto por ~30 s | `erro_sensor_ar` ativou com 2 leituras em erro (`estados` = 133); resolveu na 1ª `ok`. `erro_sensor_solo` (mesma regra) só nos testes do PC |
| E. Reinício com alertas ativos | COORD sem energia por ~145 s (power-on) com `solo_seco` ativo | no boot, `reconstruidos do historico: 1 ativos (61 transições na carga)`; a fila do nó (seq 46–61) chegou em seguida e resolveu o alerta; 0 perdidas |
| Leituras perdidas | toda a sessão de 22/11 (~3 h 20 min) e a de 26/11 | 492/492 e 10/10 gravadas; 0 perdidas, 0 duplicadas, 0 descartes na fila do ESP-NOW |

**Custo medido:** avaliação de 145–218 µs por leitura; heap livre de 204 940 B (mínimo 199 840 B) com a tabela de alertas (o número confiável do custo é o estático, seção 5).

## 8. Conferência com o app (sem celular)

O PC não tem Wi-Fi. Uma placa com o firmware [`experimentos/ponte_http`](../experimentos/ponte_http/) entra no Wi-Fi da horta e faz as requisições que o app faria ([`ferramentas/ponte_http.py`](../ferramentas/ponte_http.py)); as respostas reais passam pelo **próprio código do app** (`classificacao.ts` e os títulos de `avisos-sensores.tsx`, branch `feat/coordenador`) em [`ferramentas/simular_app.mjs`](../ferramentas/simular_app.mjs). Dados em [`dados/etapa9/brutos/app2/`](../dados/etapa9/brutos/app2/) (26/11/2026; nó com 4 alertas ativos: solo seco, bateria baixa, bateria crítica e sem comunicar).

| Tela do app | O que mostraria |
|---|---|
| Sensores (`horta1`) | "Visto há 1 min" · "Trocar bateria" (`modo_energia` = `critico`) · "Solo seco" (`solo_classe` = `seco`) |
| Avisos | "Atenção nos sensores" (solo_seco) · "Bateria fraca" (bateria_baixa) · "Atenção nos sensores" (bateria_critica) · "Atenção nos sensores" (sem_comunicar) |
| Todos os valores possíveis | `seco`, `umido`, `muito_umido`, `normal`, `economia`, `critico`: todos com texto no app; **nenhum problema de contrato** |

A tela de avisos do app só tem título próprio para `bateria_baixa`; os outros tipos aparecem como "Atenção nos sensores" (títulos por tipo: mudança opcional no app).

**Tempo das rotas** (20 requisições cada pela ponte, 0 falhas; "na COORD" = atendimento, linhas `[HTTP]`):

| Rota | Cliente: mediana / p95 | COORD: mediana |
|---|---|---|
| `/status` | 68 / 104 ms | 5,9 ms |
| `/nos` | 66 / 92 ms | 6,3 ms |
| `/leituras/atuais` | 64 / 97 ms | 5,2 ms |
| `/alertas` | 72 / 87 ms | 6,7 ms |
| `/alertas?incluir=resolvidos` | 64 / 81 ms | (com a anterior) |
| `/leituras?limite=10` | 158 / 206 ms | 58 ms |

As rotas leves continuam em ~5–7 ms na COORD, como na Etapa 8 ([`api.md`](api.md), seção 5): a avaliação dos alertas não pesou nas respostas. O tempo no cliente é maior que o do PC na Etapa 8 (~28 ms) porque inclui a pilha Wi-Fi da placa da ponte.

## 9. Limitações e pendências

- **Limiares provisórios** (seção 4): os de agronomia ficam a definir com o pessoal do projeto de AUP.
- `sem_comunicar` não é reconstruído entre boots quando a última leitura não tem hora válida (sem o DS3231, Etapa 6): depois de um power-on sem acerto de hora, só volta a ser avaliado com uma leitura nova.
- No modo crítico o nó fica calado por projeto, e o `sem_comunicar` também ativa (na produção, depois de 3 h). Dá para suprimi-lo quando o último modo foi crítico (a decidir).
- `erro_sensor_alimentacao` desligado até o divisor ser montado (Etapa 5, passo 10).
- Títulos próprios na tela de avisos do app para os outros tipos (mudança no app, opcional).
