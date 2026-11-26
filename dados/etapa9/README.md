# Dados da Etapa 9 (alertas, bancada)

Testes de 22/11/2026: COORD `5AC9001351` no env `teste_alertas` (períodos da bancada) e nó `horta1` (`5AC9009194`). Captura com `ferramentas/coord_log.py`. Resultados e interpretação: [`docs/alertas.md`](../../docs/alertas.md).

Em `brutos/`, as linhas `[SCAN]` (SSIDs das redes vizinhas) foram removidas; o log original fica em `rascunho/`, fora do Git.

| Arquivo | Conteúdo |
|---|---|
| `coord_log.txt` | serial da COORD durante todos os testes (~3 h 20 min): instante do PC, `<` linha recebida, `>` comando enviado |
| `t0_boot.txt` | primeiro boot com a Etapa 9 (reconstrução dos alertas na carga) |
| `tA_solo_ar.txt` | sensor de solo no ar (alerta `solo_seco`) |
| `tB_bateria_resumo.txt`, `tB_inicio.txt` | rampa de tensão simulada (env `teste_tensao` do nó): pacotes e transições de alerta |
| `coord_log_app.txt` | serial da COORD durante a conferência com o app (26/11/2026), sem as linhas `[SCAN]` |
| `app/`, `app2/` | respostas reais da API (`*.json`) e tempos (`tempos.csv`) pela ponte HTTP (`experimentos/ponte_http`, `ferramentas/ponte_http.py`): `app` com 1 alerta (solo seco), `app2` com 4 (solo seco, bateria baixa e crítica, sem comunicar) e 20 rodadas de tempos |
