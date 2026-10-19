# Persistência no coordenador

Como o coordenador guarda o que recebe (Etapa 7; registro v2 e confirmação ao nó na Etapa 7.1): tabela de partições, formato do registro, organização do histórico no LittleFS, política de gravação, montagem e queda de energia, relógio provisório, consulta, exportação e cadastro dos nós. O nó sensor não tem sistema de arquivos; o único estado dele que sobrevive a um reset é o contador de boots na NVS ([`protocolo.md`](protocolo.md), seção 4).

Fontes principais:

- **[IDF-FS]** ESP-IDF v5.5, *File System Considerations*: <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-guides/file-system-considerations.html>
- **[IDF-PART]** ESP-IDF v5.5, *Partition Tables*: <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-guides/partition-tables.html>
- **[IDF-NVS]** ESP-IDF v5.5, *Non-Volatile Storage Library*: <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/storage/nvs_flash.html>
- **[IDF-TIME]** ESP-IDF v5.5, *System Time*: <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/system_time.html>
- **[LFS]** littlefs v2.11 (componente `joltwallet/littlefs` usado pelo núcleo Arduino-ESP32 3.3.12): `SPEC.md` e `lfs.c` do código-fonte
- **[PIO-PART]** PlatformIO, *Espressif 32 → Partition Tables*: <https://docs.platformio.org/en/latest/platforms/espressif32.html>

## 1. O que fica onde

| Dado | Onde | Sobrevive a | Por quê |
|---|---|---|---|
| Histórico de leituras | **LittleFS** (partição `littlefs`, 1920 KB) | reset, queda de energia, regravação do firmware | volume grande, append, leitura sequencial e paginada |
| Cadastro dos nós (nome, calibração do solo) | **NVS**, namespace `coordenador`, chave `nos` (um blob) | idem, e também à formatação do LittleFS | poucos bytes que mudam raramente; independente do histórico (seção 10) |
| Contador de boots do coordenador | NVS, namespace `coordenador` | idem | marca cada registro (`boot_coord`): base da reconstrução de hora (seção 8) |
| Hora UTC | relógio do sistema (temporizador RTC) | só reset por software | provisório até o DS3231 (Etapa 6) |
| Última leitura e (`boot`, `seq`) de cada nó | RAM, refeitos do histórico no boot | — (reconstruídos) | o app tem o valor atual e o antirrepetição é retomado logo depois de religar |

## 2. Decisões

| Decisão | Escolha | Justificativa |
|---|---|---|
| Sistema de arquivos | **LittleFS** | *"Designed as fail-safe, with own wear levelling and with fixed amount of RAM usage independent on the file system size"*; proteção contra queda de energia *"Yes (integrated)"*, contra *"Partial"* do SPIFFS e *"No"* do FatFS [IDF-FS]. O SPIFFS ainda *"tends to slow down when exceeding around 70% of the dedicated partition size"* [IDF-FS] |
| Tabela de partições | própria, baseada na `no_ota.csv` do Arduino-ESP32: app único de 2 MB, **LittleFS de 1920 KB** | sem OTA (o coordenador é gravado pela USB); o espaço da segunda app vira histórico (seção 3) |
| Registro | **52 B fixos** (v2, Etapa 7.1) = 24 B de metadados + os **26 B do pacote v3, sem alteração** + CRC-16 (v1, Etapa 7: 48 B com o pacote v2 de 22 B) | tamanho fixo permite endereçar o registro *i* por *i* × 52; guardar o pacote bruto preserva tudo o que o nó mandou, inclusive a idade da leitura |
| Identificação do nó no registro | **MAC** (6 B), não índice do cadastro | o registro continua certo se o cadastro for editado ou apagado |
| Solo | **mV** no registro; o % é calculado na consulta com a calibração **atual** | recalibrar em campo corrige todo o histórico |
| Organização | **segmentos em anel**: arquivos `/h/NNNNNNNN.seg` de 470 registros (500 no registro v1), no máximo 56 | rotação = apagar um arquivo inteiro (barato e atômico no LittleFS); cada segmento tem um resumo em RAM para pular segmentos na consulta (seção 5) |
| Gravação | **imediata, abrir + anexar + fechar por registro** (G1) | cada leitura aceita fica na flash ~60 ms depois de chegar; o lote seria ~8× mais rápido, mas perderia até um lote numa queda (seção 6) |
| Montagem | partição **sem** a assinatura do LittleFS → formata sozinho; LittleFS **existente que não monta → não formata** | não apagar um histórico que talvez dê para recuperar; o estado `nao_montado` aparece no console (e no app, Etapa 8) (seção 7) |
| Hora | registro guarda `utc_s` da **recepção** (0 se inválida) **e** `boot_coord` + `desde_boot_s`; a hora da **leitura** = recepção − `idade_s` do pacote, calculada na consulta | permite reconstruir a hora dos registros gravados antes do acerto no mesmo boot (seção 8) e corrigir as leituras reenviadas pelo nó (Etapa 7.1) |
| Confirmação ao nó (Etapa 7.1) | a borda (boot, seq) de cada nó só avança **depois do `close()`** | o coordenador confirma ao nó só o que está na flash; o nó guarda a leitura até lá ([`protocolo.md`](protocolo.md), seção 13) |
| Cadastro | NVS, não LittleFS | ver seção 10 |

## 3. Tabelas de partições

**Coordenador** ([`coordenador/particoes.csv`](../coordenador/particoes.csv)):

| Nome | Tipo | Subtipo | Endereço | Tamanho | Observação |
|---|---|---|---|---|---|
| `nvs` | data | nvs | 0x9000 | 20 KB | mesmo endereço do `default.csv`: o contador de boots e o cadastro foram preservados na troca de tabela |
| `otadata` | data | ota | 0xE000 | 8 KB | mantida da tabela base |
| `app0` | app | **factory** | 0x10000 | 2 MB | firmware atual: 999 KB (49 %) |
| `littlefs` | data | **littlefs (0x83)** | 0x210000 | **1920 KB** | histórico |
| `coredump` | data | coredump | 0x3F0000 | 64 KB | mantida da tabela base |

Diferenças para a `no_ota.csv` do Arduino-ESP32: a app é `factory` em vez de `ota_0` (não há OTA) e a partição de dados tem o subtipo `littlefs` em vez de `spiffs` [IDF-PART]. Detalhe do pioarduino: o builder só limita o tamanho do firmware pela partição `ota_0`; com `factory`, um firmware grande invadiria o LittleFS sem erro. `board_build.app_partition_name = app0` no `platformio.ini` faz o build falhar se não couber (`builder/main.py`, `_update_max_upload_size`) [PIO-PART].

**Nó sensor:** `max_app_4MB.csv` oficial do Arduino-ESP32 (app `factory` de 3968 KB, sem sistema de arquivos), com a `nvs` no mesmo endereço: o contador de boots do nó foi preservado. Mesmo ajuste do `app_partition_name`.

As duas tabelas foram lidas de volta da flash (`esptool read-flash 0x8000` + `gen_esp32part.py`) e conferidas com o que o firmware imprime no boot (`[PART]`): [`dados/etapa7/brutos/particoes_lidas.txt`](../dados/etapa7/brutos/particoes_lidas.txt).

## 4. Registro v2 (52 bytes)

Definido em [`coordenador/lib/registro/src/registro.h`](../coordenador/lib/registro/src/registro.h). Little-endian, `packed`; o `seq` do pacote cai no offset 28 (múltiplo de 4) e a `idade_s` no 46 (fora do alinhamento natural: lida só por cópia, `registro::idade`).

| Offset | Campo | Tipo | Bytes | Conteúdo |
|---|---|---|---|---|
| 0 | `versao` | uint8 | 1 | 2 (1 = registro da Etapa 7, 48 B) |
| 1 | `flags` | uint8 | 1 | bit 0: hora válida; bits 1–2: fonte do relógio (0 nenhuma, 1 manual, 2 herdada, 3 reservado ao DS3231); bits 3–7 reservados |
| 2 | `boot_coord` | uint16 | 2 | contador de boots do coordenador |
| 4 | `utc_s` | uint32 | 4 | UTC da **recepção**, em s desde 1970; **0 se a hora não é válida** |
| 8 | `desde_boot_s` | uint32 | 4 | s desde o boot do coordenador |
| 12 | `mac` | 6 × uint8 | 6 | MAC de origem do nó |
| 18 | `rssi` | int8 | 1 | dBm |
| 19 | `ruido` | int8 | 1 | piso de ruído, dBm |
| 20 | `classe` | uint8 | 1 | `protocolo::Classe` (só `primeiro`, `novo` e `reinicio` são gravados) |
| 21 | `reservado` | uint8 | 1 | 0 |
| 22 | `perdidos` | uint16 | 2 | lacuna de `seq` antes deste pacote (satura em 65535) |
| 24 | `pacote` | `PacoteLeitura` | 26 | os 26 bytes recebidos (protocolo v3), sem a assinatura; inclui `idade_s` no offset 46 |
| 50 | `crc` | uint16 | 2 | CRC-16/CCITT-FALSE dos bytes 0–49 |

- **CRC-16/CCITT-FALSE** (polinômio 0x1021, inicial 0xFFFF; verificação de `"123456789"` = 0x29B1; no Python, `binascii.crc_hqx(dados, 0xFFFF)`): detecta registro corrompido ou escrito pela metade. A assinatura HMAC não é gravada; ela já foi conferida na recepção.
- **Leitura** (`registro::ler`): versão → tamanho → CRC, copiando com `memcpy` (mesma regra do pacote: nunca ler campo de `struct packed` por ponteiro). Versão antes do tamanho: um registro de versão futura, com outro tamanho, é identificado como tal.
- **Por que guardar o pacote inteiro:** poucos bytes a mais que extrair só as grandezas, e o histórico passa a ter `motivo_boot`, `acordado_ant_ms`, `tentativas_ant`, `flags` e `idade_s` de cada ciclo: os dados de consumo, de diagnóstico e de reenvio da avaliação.
- **Hora da leitura** (`registro::utcLeitura`): recepção − idade, sem passar de 0. Usada no resumo dos segmentos (para o pulo de segmentos por período), na hora efetiva das consultas e na coluna `utc` da exportação (que ganhou a coluna `idade_s`).
- **Troca de versão (Etapa 7.1):** um arquivo com registros de 48 e de 52 B misturados não pode ser lido por posição. O histórico v1 (só dados de bancada) foi exportado (27 604 linhas, CRC conferido) e a partição foi formatada antes de gravar o firmware novo. Um firmware v2 que encontrar segmentos v1 os marca como fechados (tamanho não múltiplo de 52) e conta os registros como inválidos, sem travar (testado: 56 segmentos, 25 452 inválidos, carga em 1,8 s).

## 5. Segmentos em anel, retenção e desgaste

Lógica pura em [`lib/anel`](../coordenador/lib/anel/src/anel.h) (testada no PC); operações de arquivo em [`src/historico.cpp`](../coordenador/src/historico.cpp).

- Cada segmento é um arquivo `/littlefs/h/NNNNNNNN.seg` (8 dígitos) com até **K = 470** registros (24 440 B, que cabem em 6 blocos de 4 KB contando os ponteiros da lista CTZ do LittleFS [LFS]). Com o registro v1 eram 500 × 48 = 24 000 B; com 52 B, 500 registros ocupariam 7 blocos.
- Cheio, abre o número seguinte; acima de **56 segmentos**, apaga o mais antigo antes de gravar.
- **Id global** de um registro = número do segmento × K + posição. Cresce com o tempo, é estável enquanto K não mudar e serve de cursor de paginação.
- No boot, o coordenador lista `/h`, ordena os números e lê todos os segmentos, montando um **resumo por segmento** em RAM: posições, válidos, sem hora, `utc` mínimo e máximo, primeiro e último `boot_coord`, e se está *fechado* (arquivo com tamanho não múltiplo de 48, ex.: escrita parcial; não recebe mais registros para não desalinhar os seguintes).

**Capacidade e retenção:**

| Grandeza | Valor |
|---|---|
| Registros | 56 × 470 = **26 320** (entre 25 850 e 26 320, conforme o segmento atual) |
| Espaço ocupado cheio (medido com o registro v1) | **1356 KB de 1920 KB (70,6 %)**; o v2 usa os mesmos 6 blocos por segmento |
| Com 2 nós a cada 15 min (192 registros/dia) | **~135–137 dias** (v1: ~143–146) |
| Com 8 nós | ~34 dias (v1: ~36) |

A folga de ~29 % é deliberada: o LittleFS precisa de blocos livres para a cópia na escrita (*copy-on-write*) e para o seu nivelamento de desgaste.

**Desgaste da flash (pior caso):** na fonte do littlefs [LFS], depois de um `sync`/reabertura, a primeira escrita no arquivo (`lfs_ctz_extend`) apaga um bloco novo e copia para ele o último bloco parcial. Logo, cada registro gravado com abrir + anexar + fechar custa **um apagamento de bloco**. Com 480 blocos de 4 KB, o nivelamento dinâmico do LittleFS e 100 000 ciclos por setor:

```
2 nós: 480 × 100 000 / 192 apagamentos por dia ≈ 250 000 dias ≈ 680 anos
8 nós: 480 × 100 000 / 768 apagamentos por dia ≈ 62 500 dias ≈ 170 anos
```

O valor de 100 000 ciclos vem da ficha da flash da placa (Boya, ID 0x68/0x4016) no distribuidor; **falta confirmar no datasheet do fabricante** (pendência, seção 12). Mesmo com 10× menos, a flash dura décadas.

## 6. Gravação

**Medições das estratégias** (env `teste_gravacao`, comando `bench`, 500 registros cada, partição cheia com 1368 KB e vazia; [`gravacao_estrategias.txt`](../dados/etapa7/brutos/gravacao_estrategias.txt)):

| Estratégia | O que faz | Mediana (cheia / vazia) | p95 | Máx. | Perde numa queda |
|---|---|---|---|---|---|
| **E1 (adotada)** | abrir + anexar + fechar por registro | **59,8 / 57,7 ms** | 165 / 161 ms | 235 ms | nada já confirmado |
| E2 | arquivo aberto, `flush` (= `lfs_file_sync`) por registro | 50,9 / 47,0 ms | 66 / 63 ms | 145 ms | nada já confirmado |
| E3 | arquivo aberto, `flush` a cada 8 | média 7,0 / 6,6 ms por registro | — | 161 ms | até 7 registros |
| E4 | buffer de 8 em RAM, abrir + escrever + fechar | média 9,1 / 8,2 ms por registro | — | 210 ms | até 7 registros |

1. **Cheia × vazia quase não muda** o tempo (o LittleFS não degrada como o SPIFFS).
2. **E2 é só ~15 % mais rápida que E1** e exigiria manter um arquivo aberto (e tratar a troca de segmento e a consulta lendo o mesmo arquivo). Não compensa.
3. **O lote (E3/E4) é ~8× mais rápido**, mas uma queda de energia perderia até 7 leituras que já receberam ACK. A ~1 pacote por minuto (8 nós), o tempo de gravação não é gargalo.
4. **Estresse:** uma tarefa gravando sem parar no mesmo núcleo do `loop()` (4471 gravações em 300 s) e o NÓ 1 enviando a cada 10 s: **30/30 pacotes recebidos e gravados, 0 perdidos, fila do ESP-NOW sem descarte**.

**Decisão: G1 = E1**, sem código novo. O `close()` é o `sync`: o registro só está na flash depois dele.

**Na escala real** (28 mil registros gravados por `fs gravar`, [`escala_real.txt`](../dados/etapa7/brutos/escala_real.txt)), a média acumulada subiu de 62 ms (4 segmentos) para 93 ms (56 segmentos), com máximo de 854 ms: abrir um arquivo custa mais com mais arquivos no diretório. A rotação apagou o segmento 1 ao abrir o 57, e os registros continuaram contíguos pelo id global.

**Cuidado observado:** `fs gravar N` bloqueia o `loop()`; numa gravação longa a fila do ESP-NOW (16 itens) chegou a descartar 45 pacotes. Daí a regra do console de executar **uma linha por volta do `loop()`** e a exportação em páginas (seção 9).

## 7. Montagem, estados e queda de energia

**Política de montagem** (`historico::iniciar`, chamada no `setup()` **antes de ligar o rádio**, para a carga não disputar com a fila do ESP-NOW):

| Situação | Detecção | Ação | Estado |
|---|---|---|---|
| Partição nunca formatada | sem a string `littlefs` no offset 8 dos blocos 0 e 1 (superbloco, [LFS] `SPEC.md`) | formata sozinho (7 ms) | `formatado_no_boot` (informativo) |
| LittleFS existente que não monta | tem a assinatura, mas `begin()` falha | **não formata**; fica sem histórico; `fs formatar` formata à mão | `nao_montado` |
| Partição ausente | `esp_partition_find_first` = nulo | sem histórico | `nao_montado` |
| Espaço livre < 48 KB ao abrir segmento | `totalBytes − usedBytes` | **rotação de emergência**: apaga os mais antigos até ter a margem | `espaco_baixo` se nem assim |
| 3 gravações seguidas com erro | contador | continua tentando | `falhas_de_gravacao` |

Sem histórico (`nao_montado`) ou com a gravação falhando, a **última leitura em RAM continua sendo atualizada**: o app mostra o valor atual mesmo sem histórico.

Testes ([`dados/etapa7/brutos/`](../dados/etapa7/brutos/)):

| Teste | Resultado |
|---|---|
| Partição apagada (`montagem_virgem`) | formatou sozinho em 7 ms, `formatado_no_boot`, gravou normalmente |
| Superbloco corrompido (`montagem_corrompida`) | `nao_montado`, **não formatou**; `fs formatar` recuperou em 90 ms |
| Espaço baixo (`espaco_baixo`): histórico com 27 mil registros + arquivo de 560 KB | 2 rotações de emergência (segmentos 1 e 2), estado `ok`, sem falha de gravação |
| Espaço extremo (`espaco_baixo_extremo`): arquivo de 1856 KB | `espaco_baixo` com o histórico mínimo; voltou a `ok` depois de liberar o espaço |
| Rotação forçada (env `teste_rotacao`, K = 5, máx. 3) | rotação correta, reinício e carga sem perder a sequência |
| Reset durante gravação (passo 5) | recuperou sem perda e sem registros inválidos |

**Teste de queda de energia** (passo 9, [`queda.csv`](../dados/etapa7/brutos/queda.csv), [`ferramentas/teste_queda.py`](../ferramentas/teste_queda.py)): 20 cortes do cabo USB da COORD com o histórico cheio (56 segmentos, ~27,6 mil registros) e `fs gravar N v` imprimindo cada registro confirmado; 17 cortes durante a gravação e 3 durante o boot (ciclos 6, 7 e 9; no 7 o corte foi durante a carga, depois de montar). O script compara o último id confirmado antes do corte com o último presente depois de religar.

| Grandeza | Resultado |
|---|---|
| Montagem depois do corte | **montou em todos os boots** |
| Registros inválidos (CRC) | **0** |
| Registros confirmados e perdidos | **0** |
| Registros a mais | **1** (ciclo 21: gravado na flash, mas o corte veio antes de a linha de confirmação sair pela serial) |
| Carga no boot | 2,2–2,9 s (56 segmentos) |

O registro "a mais" é o comportamento esperado do LittleFS: a escrita é atômica no `sync`; um registro está inteiro ou não está. Para o nó, porém, isso significa que **um registro pode ter sido gravado sem o nó saber** — ponto que a Etapa 7.1 precisa tratar (duplicatas no reenvio).

## 8. Relógio provisório e hora dos registros

O DS3231 foi adiado (Etapa 6). [`src/relogio.cpp`](../coordenador/src/relogio.cpp) usa o relógio do sistema do ESP32, acertado pelo comando `hora` (epoch ou ISO 8601 UTC). Quando o DS3231 entrar, só essa implementação muda.

| Fonte | Quando | Hora válida |
|---|---|---|
| `nenhuma` | depois de um *power-on* (queda de energia, ou o EN pela abertura da serial) | não: `utc_s = 0` |
| `manual` | acertada pelo comando `hora` neste boot | sim |
| `herdada` | acertada antes de um reset por **software**: o temporizador RTC manteve a hora [IDF-TIME] | sim |

Decisões: firmware só em **UTC** (fuso no app); faixa plausível **2026–2100** (o `uint32` do registro vai até 2106); saltos de acerto são aceitos e registrados (`ajuste de +11 s`); o acerto trunca a fração de segundo (erro de até 1 s). Testado em [`relogio_bancada.txt`](../dados/etapa7/brutos/relogio_bancada.txt): faixa, formato inválido, acerto, ajuste, herança no `reiniciar` e perda no *power-on*.

**Hora reconstruída** ([`lib/filtro`](../coordenador/lib/filtro/src/filtro.h)): um registro sem hora de um boot em que o relógio foi acertado depois recebe `utc = desvio + desde_boot_s`, com o desvio (`utc − desde_boot`) do primeiro registro com hora do mesmo boot. A tabela de âncoras só guarda boots que têm registros sem hora (até 32). A consulta e a exportação marcam cada linha como `valida`, `reconstruida` ou `sem_hora`; boots sem nenhum acerto ficam `sem_hora` e não entram em consultas por período.

## 9. Consulta e exportação

**Consulta** ([`src/consulta.cpp`](../coordenador/src/consulta.cpp)): API interna que as rotas HTTP da Etapa 8 vão usar. Filtros por nó e por período (sobre a hora efetiva), ordem crescente ou decrescente, cursor (id global) e limite por página; leitura em partes (buffer de 16 registros) e pulo de segmentos fora do período pelo resumo. Roda na mesma tarefa que grava: consulta e gravação não se misturam no meio de um registro.

Medido com 27,5 mil registros sintéticos de 2 nós ([`consulta.txt`](../dados/etapa7/brutos/consulta.txt)):

| Grandeza | Valor |
|---|---|
| Página de 100, crescente | mediana **12,9 ms** (276 páginas) |
| Página de 100, decrescente | mediana **17,5 ms** |
| Varredura completa sem resultado (nó inexistente) | **1,45 s** (`maxlidos` limita) |
| Heap durante a consulta | −5,5 KB transitório, **sem vazamento** (heap igual antes e depois) |

**Exportação** ([`src/exportar.cpp`](../coordenador/src/exportar.cpp) + [`ferramentas/exportar_historico.py`](../ferramentas/exportar_historico.py)): o histórico sai em CSV pela serial, em páginas de 50 por volta do `loop()` (a recepção continua), com contagem de linhas e CRC-32 (zlib) no fim; o script acerta a hora da COORD, grava `.parcial` até conferir e só então renomeia. Medido: **27 610 linhas em 337 s (2,98 MB), CRC conferido**, com 34 pacotes do NÓ 1 gravados durante a exportação; a exportação interrompida (porta desanexada no meio) foi detectada e o arquivo ficou como `.parcial`.

## 10. Cadastro dos nós (NVS)

[`lib/cadastro`](../coordenador/lib/cadastro/src/cadastro.h) + [`src/configuracao.cpp`](../coordenador/src/configuracao.cpp): até 8 nós, cada um com MAC, nome (1–15 caracteres `[A-Za-z0-9_-]`) e calibração do solo (`mv_seco` > `mv_umido`), num único blob versionado de 212 B. Semente em `config::NOS`, usada só no primeiro boot ou no comando `no padrao`; depois vale a NVS, editada pelos comandos `no`.

Por que NVS e não um arquivo no LittleFS: a configuração fica independente do histórico (se o LittleFS não montar ou for formatado, nomes e calibrações continuam), e a NVS *"works best for storing many small values"* e não perde dados numa queda de energia, *"except for the new key-value pair if it was being written"* [IDF-NVS]. Testado em [`cadastro.txt`](../dados/etapa7/brutos/cadastro.txt): edição, erros, calibração pela última leitura, reinício, *power-on* e formatação do LittleFS.

## 11. Testes

`cd coordenador && pio test -e native`: **74 testes Unity no PC**, todos passando (71 na Etapa 7; +3 na 7.1).

| Suíte | Testes | O que cobre |
|---|---|---|
| `test_registro` | 13 | CRC (vetor 0x29B1), 52 bytes iguais à referência em Python (registro v2 com pacote v3), hora da leitura = recepção − idade, hora inválida ou fora do `uint32`, saturação, cada bit invertido detectado, tamanho e versão (registro v1 recusado) |
| `test_anel` | 16 | nomes de segmento, resumo (pela hora da leitura), id global, plano de rotação, rotação simulada, continuação depois do boot, segmento fechado |
| `test_filtro` | 15 | âncoras, hora reconstruída, hora efetiva descontando a idade, filtro, pulo de segmentos, percurso e paginação nas duas ordens |
| `test_cadastro` | 12 | nomes, MAC, calibração, umidade do solo, tabela cheia, blob (ida e volta, inválido) |
| `test_calendario` | 11 | epoch ↔ data, bissextos, interpretação de epoch e ISO 8601, rejeições, reconstrução |
| `test_exportacao` | 7 | CRC-32 (vetor 0xCBF43926, igual ao Python), linha de referência, colunas (27, com `idade_s`), células vazias |

Na placa: os testes de bancada das seções 6, 7 e 9, com os dados brutos em [`dados/etapa7/brutos/`](../dados/etapa7/brutos/).

## 12. Limitações e pendências

- ~~**O coordenador é ponto único de falha.**~~ **Resolvido na Etapa 7.1** para quedas de até ~2,7 dias: o nó guarda as leituras na memória RTC até o coordenador confirmar que estão gravadas ([`protocolo.md`](protocolo.md), seção 13). Continua valendo para o que só o coordenador tem: o histórico e a API.
- ~~**O rastreador avança antes da gravação.**~~ **Corrigido na Etapa 7.1:** a borda de cada nó só avança depois do `close()` (`classificar()` → gravar → `confirmar()`).
- **LittleFS sem montar ou gravação falhando:** o coordenador não confirma nada, e os nós acumulam até encher a fila (~2,7 dias), reenviando uma rajada por ciclo enquanto isso.
- **Resistência da flash** (100 000 ciclos) só pela ficha do distribuidor; confirmar no datasheet do fabricante.
- **Segmentos com registros sem hora nunca são pulados** numa consulta por período (a hora reconstruída pode cair fora do `utc` mínimo/máximo do resumo): no sintético pessimista, 24 de 56 segmentos lidos (0,6 s). Melhoria opcional: guardar no resumo o `boot`/`desde_boot` mínimo e máximo dos registros sem hora.
- **Carga no boot de 2–3 s** com o histórico cheio, antes de ligar o rádio: pacotes enviados nesse intervalo recebem `FAIL` (o rádio ainda não está ligado).
- Depois de `fs formatar`, as últimas leituras em RAM ficam desatualizadas até o próximo boot.
- **Hora:** provisória até o DS3231; registros de boots sem acerto ficam sem hora.
