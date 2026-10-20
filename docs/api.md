# API HTTP do coordenador (v1)

> **Situação: especificação aprovada (Etapa 8, passos 2 e 3); implementação em andamento.**

API para o app (e para um notebook) consultar o sistema pela rede Wi-Fi do próprio coordenador, sem Internet.

## 1. Como acessar

| Item | Valor |
|---|---|
| Rede | Wi-Fi do coordenador (SSID e senha WPA2 configurados no firmware), canal 1 |
| Endereço | **`http://192.168.4.1`** (IP fixo; não há DNS) |
| Base das rotas | **`/api/v1`** |
| Formato | JSON em UTF-8 (`Content-Type: application/json; charset=utf-8`); a exportação é CSV (`text/csv`) |
| Clientes ao mesmo tempo | até 4 conectados na rede; o servidor atende **uma requisição por vez** |
| Conexão | sem keep-alive: cada resposta termina com `Connection: close` |

**Android:** a rede não tem Internet, então o sistema pode avisar "sem Internet" e mandar o tráfego pelos dados móveis. O app deve pedir explicitamente a rede Wi-Fi do coordenador (por exemplo, `WifiNetworkSpecifier` com `removeCapability(NET_CAPABILITY_INTERNET)` e um socket ligado a essa rede) e usar sempre o IP. Detalhes em [`comunicacao.md`](comunicacao.md).

## 2. Convenções

### 2.1 Unidades e nomes

| Grandeza | Campo JSON | Unidade | Observação |
|---|---|---|---|
| Temperatura do ar | `temperatura_c` | °C, 2 casas | `null` se o sensor deu erro |
| Umidade relativa do ar | `umidade_ar_pct` | %, 2 casas | `null` se erro |
| Umidade do solo | `solo_pct` | %, 1 casa, 0–100 | calculada com a calibração **atual** do nó; `null` sem calibração ou com erro |
| Saída do sensor de solo | `solo_mv` | mV | o dado bruto, sempre presente quando o sensor leu |
| Alimentação (bateria) | `alimentacao_mv` | mV | entrada do divisor |
| Estado de cada grandeza | `estados.temperatura` etc. | `ok`, `erro`, `fora_de_faixa` | com `fora_de_faixa`, o valor vem preenchido, mas fora da faixa válida do sensor |
| Sinal | `rssi_dbm` | dBm | |
| Durações | `*_s`, `*_ms` | s, ms | |

Nomes em português, sem acento, `snake_case`, como no resto do projeto.

### 2.2 Hora

O coordenador ainda não tem RTC com bateria (o DS3231 fica para a Etapa 6). A hora vem do relógio do sistema, acertado pela API ou pela serial, e **se perde quando falta energia**. Por isso toda hora vem com a sua origem:

```json
"hora": { "utc": "2026-10-19T14:30:00Z", "origem": "valida" }
```

| `origem` | Significado | `utc` |
|---|---|---|
| `valida` | o relógio estava acertado quando o pacote chegou | hora da **leitura** (recepção − idade) |
| `reconstruida` | chegou antes do acerto, no mesmo boot do coordenador; hora calculada pelo primeiro registro com hora daquele boot | hora da leitura, estimada |
| `sem_hora` | chegou num boot em que o relógio nunca foi acertado | `null` |

- Formato: **ISO 8601 em UTC** (`AAAA-MM-DDTHH:MM:SSZ`), sem fuso; o app converte para a hora local.
- A hora é **da leitura**, não da recepção: para leituras que esperaram na fila do nó (coordenador fora do ar), o coordenador desconta a `idade_s` ([`protocolo.md`](protocolo.md), seção 13.6). Erro típico: ~1,1 % da idade.
- Filtros por período (`de`, `ate`) usam essa mesma hora; registros `sem_hora` nunca entram num filtro por período.

### 2.3 Identificação dos nós

Pelo **MAC STA** do nó, em minúsculas com dois-pontos: `88:57:21:70:93:70`. Nas rotas: `/api/v1/nos/88:57:21:70:93:70`.

### 2.4 Paginação (histórico)

O histórico pode ter ~26 mil registros: nunca vem inteiro. Cada página traz um **cursor** para pedir a seguinte.

| Parâmetro | Padrão | Faixa |
|---|---|---|
| `limite` | 50 | 1–200 (máximo provisório, a confirmar com a medição de convivência com o ESP-NOW) |
| `ordem` | `desc` (mais recentes primeiro) | `asc`, `desc` |
| `cursor` | início (ou fim, em `desc`) | o `proximo_cursor` da página anterior |

```json
{ "leituras": [ ... ], "proximo_cursor": 26105, "fim": false, "cursor_rotacionado": false }
```

- Uma página pode vir com **menos itens que o `limite` e `fim: false`**: cada página lê no máximo 2000 registros da flash, para não segurar o coordenador num filtro que descarta quase tudo. Basta pedir a próxima com o `proximo_cursor`.
- `fim: true`: não há mais registros no filtro, **por enquanto**. Em `asc`, o mesmo cursor depois traz só as leituras novas: é assim que o app sincroniza sem baixar tudo de novo.
- `cursor_rotacionado: true`: o cursor apontava para registros que já foram apagados pela rotação do histórico; a página começa no mais antigo que ainda existe.
- O cursor é um número inteiro (id global do registro, sempre < 2⁵³, então seguro em JSON).

### 2.5 Erros

Sempre JSON, com o código HTTP correspondente:

```json
{ "erro": { "codigo": "parametro_invalido", "campo": "limite", "mensagem": "limite deve estar entre 1 e 200" } }
```

| HTTP | `codigo` | Quando |
|---|---|---|
| 400 | `parametro_invalido`, `json_invalido` | parâmetro fora da faixa, MAC mal formado, corpo inválido |
| 401 | `nao_autorizado` | rota de escrita sem token ou com token errado (com `WWW-Authenticate: Bearer`) |
| 404 | `nao_encontrado` | rota inexistente ou nó desconhecido |
| 405 | `metodo_nao_permitido` | método errado (com o cabeçalho `Allow`) |
| 409 | `conflito` | ex.: hora rejeitada pela política (seção 4.6) |
| 413 | `corpo_grande` | corpo maior que o limite (1 KB) |
| 503 | `armazenamento_indisponivel` | histórico não montado (LittleFS com problema) |

## 3. Autorização

Leituras (GET) são livres para quem está na rede WPA2. As rotas que **alteram** dados exigem um token:

```
Authorization: Bearer <token>
```

| Item | Decisão |
|---|---|
| Token | 128 bits aleatórios em 32 caracteres hexadecimais, gerados na COORD pelo RNG de hardware com o rádio ligado ([Random Number Generation, ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/random.html)) |
| Onde fica | NVS da COORD (namespace `coordenador`, chave `token`): sobrevive à formatação do histórico e à regravação do firmware |
| Configuração | só pelo console serial (acesso físico): `token gerar` cria e mostra uma vez; `token` diz se existe; `token apagar` |
| Sem token configurado | toda rota de escrita responde `401` (fechado por padrão) |
| Escopo | um token único para todas as rotas de escrita |
| Comparação | em tempo constante |
| Resposta sem token ou com token errado | `401` com `WWW-Authenticate: Bearer` e o JSON de erro |
| No app | cadastrado uma vez na instalação (configurações ou QR code, Etapa 10); o usuário final não digita senha |

**Sem HTTPS.** A rede é WPA2-PSK: de fora, ninguém lê nem altera o tráfego. De dentro, quem souber a senha do Wi-Fi e capturar a associação do celular do administrador consegue derivar as chaves da sessão (IEEE 802.11i) e ver o token. O token impede escrita por engano ou curiosidade de quem está na rede, mas não um atacante com a senha e um analisador 802.11 (limitação documentada). O HTTPS exigiria trocar de servidor (`esp_https_server` do ESP-IDF), *"one socket uses ~ 40kB RAM with SSL"* e *"the initial session setup can take about two seconds"* ([HTTPS Server, ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/protocols/esp_https_server.html)), além de um certificado autoassinado embutido no app, que dependeria de datas num aparelho sem relógio confiável. Trabalho futuro.

## 4. Rotas

| Método | Rota | O que faz | Token |
|---|---|---|---|
| GET | `/api/v1/status` | estado do sistema | não |
| GET | `/api/v1/nos` | todos os nós: cadastro, última leitura e estatísticas | não |
| GET | `/api/v1/nos/{mac}` | um nó | não |
| GET | `/api/v1/leituras/atuais` | só a última leitura de cada nó (tela inicial do app) | não |
| GET | `/api/v1/leituras` | histórico paginado, por nó e período | não |
| GET | `/api/v1/exportar.csv` | histórico em CSV (download) | não |
| GET | `/api/v1/alertas` | esqueleto (regras na Etapa 9) | não |
| PUT | `/api/v1/hora` | acertar o relógio | **sim** |
| PATCH | `/api/v1/nos/{mac}` | nome e calibração do solo | **sim** |
| POST | `/api/v1/nos/{mac}/calibrar` | ponto seco ou úmido = solo da última leitura (calibração em campo) | **sim** |

### 4.1 `GET /api/v1/status`

```json
{
  "firmware": "coordenador 8.0", "protocolo": 3, "api": 1,
  "boot": 112, "uptime_s": 86400,
  "hora": { "utc": "2026-10-19T14:30:00Z", "valida": true, "fonte": "manual" },
  "armazenamento": {
    "estado": "ok", "total_kb": 1920, "usado_kb": 612,
    "registros": 11984, "segmentos": 26, "registros_por_segmento": 470, "max_segmentos": 56,
    "mais_antigo_utc": "2026-08-01T10:00:00Z"
  },
  "rede": { "canal": 1, "clientes": 1, "espnow_descartes_fila": 0 },
  "memoria": { "heap_livre": 168468, "heap_minimo": 160220 },
  "escrita_habilitada": true
}
```

- `hora.fonte`: `nenhuma` (relógio não acertado desde o último corte de energia), `manual` (acertado neste boot) ou `herdada` (acertado antes de um reset por software).
- `armazenamento.mais_antigo_utc`: hora da leitura mais antiga com hora válida ainda no histórico (`null` se não houver).
- `escrita_habilitada`: `false` enquanto o token não for configurado na COORD (toda escrita responde `401`).
- `armazenamento.estado`: `ok`, `formatado_no_boot`, `espaco_baixo`, `falhas_de_gravacao` ou `nao_montado` ([`persistencia.md`](persistencia.md), seção 7); base para o alerta de armazenamento.

### 4.2 `GET /api/v1/nos` e `GET /api/v1/nos/{mac}`

```json
{
  "nos": [
    {
      "mac": "88:57:21:70:93:70", "nome": "no1", "cadastrado": true,
      "calibracao": { "solo_mv_seco": 1400, "solo_mv_umido": 655 },
      "visto": { "ha_s": 9 },
      "bateria_mv": 3386,
      "ultima_leitura": { "...": "mesmo formato da seção 4.4" },
      "estatisticas": {
        "desde_boot_do_coordenador": true,
        "recebidos": 96, "rejeitados": 0, "aceitos": 95, "perdidos": 1, "entrega_pct": 98.96,
        "duplicados_ou_antigos": 3, "fora_de_ordem": 1, "sem_ack_no_no": 0,
        "rssi_dbm": { "medio": -61.2, "min": -70, "max": -55 }
      }
    }
  ]
}
```

- Inclui nós **não cadastrados** que mandaram pacotes (`cadastrado: false`, `nome: "desconhecido"`).
- `visto.ha_s`: segundos desde a recepção da última leitura gravada (pelo relógio interno, vale mesmo sem hora válida; `visto: null` se não dá para saber, por exemplo, de um boot anterior sem hora). A hora da leitura está em `ultima_leitura.hora`.
- `ultima_leitura` não tem `id` (vem da RAM); o histórico tem.
- `estatisticas` são contadas **desde o último boot do coordenador** (ficam em RAM).
- Sem nenhuma leitura ainda: `ultima_leitura: null`, `visto: null`.

### 4.3 `GET /api/v1/leituras/atuais`

A versão compacta da 4.2, para a tela inicial:

```json
{ "leituras": [ { "mac": "88:57:21:70:93:70", "nome": "no1", "...": "leitura (4.4)" } ] }
```

### 4.4 `GET /api/v1/leituras`

Parâmetros: `no` (MAC, opcional), `de` e `ate` (ISO 8601 UTC ou epoch, opcionais, inclusive), `ordem`, `cursor`, `limite`.

```json
{
  "leituras": [
    {
      "id": 26104, "mac": "88:57:21:70:93:70", "nome": "no1",
      "hora": { "utc": "2026-10-19T14:29:51Z", "origem": "valida" },
      "idade_s": 0,
      "temperatura_c": 21.66, "umidade_ar_pct": 75.14,
      "solo_mv": 1066, "solo_pct": 44.8, "alimentacao_mv": 3386,
      "estados": { "temperatura": "ok", "umidade_ar": "ok", "solo": "ok", "alimentacao": "ok" },
      "rssi_dbm": -61,
      "no_boot": 38, "no_seq": 3, "perdidos_antes": 0
    }
  ],
  "proximo_cursor": 26103, "fim": false, "cursor_rotacionado": false
}
```

- `idade_s > 0`: a leitura esperou na fila do nó (coordenador fora do ar) e chegou depois.
- `perdidos_antes`: leituras que o próprio nó descartou antes desta (fila cheia).
- Exemplos: `/api/v1/leituras?no=88:57:21:70:93:70&de=2026-10-19T00:00:00Z&limite=100`; sincronização incremental: `?ordem=asc&cursor=<proximo_cursor guardado>`.

### 4.5 `GET /api/v1/exportar.csv`

Mesmos filtros (`no`, `de`, `ate`), sempre em ordem crescente, **todas** as linhas do filtro, enviadas em partes (`Transfer-Encoding: chunked`), com `Content-Disposition: attachment; filename="historico.csv"`. Colunas: as mesmas da exportação pela serial (Etapa 7, 27 colunas, [`persistencia.md`](persistencia.md), seção 9), com a hora da leitura na coluna `utc`. Na serial, a exportação completa (~27 mil linhas, 3 MB) levou 337 s; pelo Wi-Fi deve ser bem mais rápida (a medir).

### 4.6 `PUT /api/v1/hora` (token)

```json
{ "utc": "2026-10-19T14:30:00Z" }
```

ou `{ "epoch": 1792420200 }`. Resposta: o mesmo bloco `hora` do status e o salto aplicado (`"ajuste_s": -3`).

Política de aceitação: **sempre aceita**, desde que com token e dentro da faixa plausível 2026–2100, também com o relógio já válido (corrige a deriva do relógio do ESP32). Fora da faixa: `400`. O salto aplicado volta na resposta e fica no log `[HTTP]`, como no acerto pela serial (Etapa 7: aceitar saltos e registrar).

Uso previsto pelo app: ao conectar, ler `/status`; se `hora.valida` for `false` ou a diferença para o relógio do celular passar de alguns segundos, enviar a hora do celular (que vem da rede celular/NTP).

### 4.7 `PATCH /api/v1/nos/{mac}` (token)

```json
{ "nome": "canteiro-2", "calibracao": { "solo_mv_seco": 1420, "solo_mv_umido": 640 } }
```

- Campos opcionais; `"calibracao": null` remove a calibração.
- Regras do cadastro atual: nome de 1–15 caracteres `[A-Za-z0-9_-]`; mV de 1 a 3300; seco > úmido; até 8 nós.
- Nó novo (MAC ainda não cadastrado): o PATCH cadastra (`201`).
- Resposta: o nó no formato da 4.2.

### 4.8 `POST /api/v1/nos/{mac}/calibrar` (token)

```json
{ "ponto": "seco" }
```

Usa o `solo_mv` da **última leitura** do nó como ponto seco ou úmido (procedimento de calibração em campo, [`sensores.md`](sensores.md), 4.6). `409` se o nó não tem leitura com o solo em estado `ok`.

### 4.9 `GET /api/v1/alertas`

Só o formato, para o app já ter onde mostrar; as regras são da Etapa 9:

```json
{ "alertas": [], "regras": "nao_implementadas" }
```

Formato previsto de cada alerta: `{ "tipo": "bateria_baixa", "mac": "...", "desde": { "utc": ..., "origem": ... }, "valor": 3150, "limiar": 3300 }`.

## 5. Desempenho e limites

Medido em 20/10/2026 na bancada: PC conectado à `horta-coord` a ~1 m, histórico cheio (25,9 mil registros, 56 segmentos), NÓ 1 enviando a cada 10 s durante todo o teste ([`ferramentas/testar_api.py`](../ferramentas/testar_api.py) `bateria`; dados em [`dados/etapa8/brutos/p6_bateria_*`](../dados/etapa8/brutos/)).

**Tempo de resposta por rota** (100 requisições cada; no cliente = conexão TCP + resposta inteira; na COORD = atendimento, do início da rota ao fim da resposta):

| Rota | No cliente: mediana / p95 | Na COORD: mediana / p95 | Tamanho |
|---|---|---|---|
| `GET /api/v1/status` | 28,6 / 43,0 ms | 6,8 / 9,9 ms | 0,45 KB |
| `GET /api/v1/nos` | 41,5 / 61,6 ms | 16,3 / 19,7 ms | 0,75 KB (1 nó) |
| `GET /api/v1/nos/{mac}` | 28,6 / 39,4 ms | 5,4 / 5,6 ms | 0,74 KB |
| `GET /api/v1/leituras/atuais` | 27,4 / 57,1 ms | 5,5 / 6,0 ms | 0,35 KB |
| `GET /api/v1/alertas` | 23,4 / 35,3 ms | 3,7 / 3,8 ms | 43 B |
| `GET /api/v1/leituras?limite=10` | 68,6 / 105,7 ms | 44,7 / 57,8 ms | 3,4 KB |
| `GET /api/v1/leituras?limite=50` | 246 / 314 ms | 186 / 252 ms | 17 KB |
| `GET /api/v1/leituras?limite=100` | 415 / 476 ms | 390 / 455 ms | 35 KB |
| `GET /api/v1/leituras?limite=200` | 738–839 / 1234–1318 ms | 808 / 1203 ms | 71 KB |
| `GET /api/v1/exportar.csv` (25,97 mil linhas) | **~50 s** (71 KB/s) | ~50 s | 3,55 MB |
| `PUT`, `PATCH`, `POST` (com e sem token) | — | 4–8 ms | — |

- As rotas leves levam ~5 ms na COORD; o resto do tempo no cliente (~20–25 ms) é a conexão TCP nova a cada requisição (o servidor fecha a conexão depois de cada resposta).
- Picos de ~1 s nas rotas leves (máximo de 1,02–1,05 s em 100 requisições) coincidem com respostas longas ou gravações em andamento: a conexão nova espera o atendimento anterior terminar.
- O histórico custa ~4 ms por leitura em JSON na COORD (montagem em blocos de 10, com o ESP-NOW atendido entre os blocos). Para sincronizar, páginas de 50 a 100 são o melhor equilíbrio.
- O CSV completo saiu em ~50 s (contra 337 s pela serial na Etapa 7), três vezes, com as linhas conferidas.

**Dois clientes ao mesmo tempo** (120 s, um pedindo páginas de 50 e o outro `/status`): 524 e 651 respostas, **0 falhas**, mediana de 216 e 212 ms. O servidor atende uma requisição por vez; o outro cliente espera na fila do TCP.

**Rotas de escrita:** sem token e com token errado, `401` com `WWW-Authenticate: Bearer realm="horta"`; com o token certo, `200`; método errado, `405` com `Allow: GET`.

**Convivência com o ESP-NOW** durante toda a bateria: 91/91 leituras do NÓ 1 gravadas, 0 duplicatas, 0 descartes na fila de recepção, confirmação recebida em 74/91 ciclos (mediana de 5,7 ms). Detalhes em [`comunicacao.md`](comunicacao.md), seção 8.

**Heap em 1 h de requisições contínuas** (20/10/2026; `testar_api.py heap 3600`; dados em [`dados/etapa8/brutos/p6_heap_*`](../dados/etapa8/brutos/)): 16 630 requisições variadas (status, nós, leituras atuais, páginas de 50 e 200) e um CSV completo a cada 10 min (5 × ~3,6 MB, 48–56 s cada), com o heap lido no `/status` a cada minuto.

| Grandeza | Resultado |
|---|---|
| Heap livre do minuto 3 ao 59 | **200 848–200 880 B** (constante, ±20 B): **sem vazamento** |
| Menor heap livre registrado pelo ESP-IDF (`heap_minimo`) | 151 888 B (durante as respostas grandes) |
| Reinícios da COORD | 0 (`uptime` contínuo) |
| Falhas | **2 de 16 630 (0,012 %)**: uma página de 200 que chegou incompleta (aos 2,7 min) e uma conexão que não respondeu em 120 s (aos 9,6 min). A COORD continuou no ar nos dois casos; provavelmente uma queda curta do Wi-Fi do PC (sem a serial da COORD neste teste para confirmar) |

**Celular real** (20/10/2026, Samsung Galaxy A05s, Android, navegador Chrome):

| Situação | Resultado |
|---|---|
| Conectar à `horta-coord` | o sistema mostrou **"Conectado, sem Internet"** e manteve a conexão |
| `http://192.168.4.1/api/v1/status` com os **dados móveis ligados** | **abriu** (JSON completo) |
| O mesmo com os dados móveis desligados | abriu |
| `/api/v1/leituras/atuais` e `/api/v1/leituras?limite=10` | abriram; o Chrome oferece "Estilos de formatação" para ler o JSON indentado |
| Barra de endereço | aviso de site não seguro (HTTP sem TLS, decisão do passo 3) |

No navegador deste aparelho, o endereço da rede local foi alcançado pelo Wi-Fi mesmo com os dados móveis ligados: a rede `192.168.4.0/24` é a do Wi-Fi conectado. **Isso não garante o mesmo comportamento em todo app e aparelho.** Um app nativo deve continuar pedindo a rede Wi-Fi explicitamente (seção 1), e a Etapa 10 testa isso no app.

Observações do teste: a COORD tinha reiniciado e estava **sem hora** (`"valida": false`), então as leituras do NÓ 1 apareceram como `sem_hora`. É o caso que o app resolve enviando a hora do celular ao conectar (`PUT /api/v1/hora`, seção 4.6). As leituras dos nós `02:00:00:00:00:01` e `:02` são os registros sintéticos criados para o teste de exportação completa.

## 6. Decisões (passo 2)

| Decisão | Escolha | Por quê |
|---|---|---|
| Rotas | as da seção 4, sob `/api/v1` | cobrem o roteiro; `/leituras/atuais` serve à tela inicial e `/calibrar` à calibração em campo |
| Página máxima | 200 (padrão 50), provisório | ~80 KB de JSON; confirmar com a medição de convivência com o ESP-NOW |
| CSV | sem token | só leitura, e o resto da API já é legível na rede WPA2 |
| Acerto da hora | sempre aceita, com token e faixa plausível; salto registrado | o relógio do ESP32 sem DS3231 deriva; é a mesma política da serial |
| CORS | desligado | depende da tecnologia do app (não confirmada); liga com `enableCORS` se for uma página web servida de outro endereço |
