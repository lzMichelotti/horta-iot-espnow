# Dados da Etapa 2 (sensores do nó, bancada)

Capturas brutas gravadas pela serial da placa NÓ 1 entre 29/09 e 05/10/2026, em bancada (vaso com planta, em casa). Análise e figuras: `python ferramentas/analisar_sensores.py`. Resultados e interpretação: [`docs/sensores.md`](../../docs/sensores.md).

Salvo indicação, cada linha é uma média de 64 leituras de `analogReadMilliVolts()` (ADC1, 11 dB) por segundo, com o sensor de solo alimentado pelo 3V3 da placa.

| Arquivo | Conteúdo |
|---|---|
| `adc_ruido.csv` | 2000 leituras individuais do GPIO34 (bruto e mV), sensor de solo no ar: ruído do ADC |
| `1_ar.csv` | sensor de solo no ar (ponto de calibração) |
| `2_solo_levemente_umido.csv`, `2b_…`, `2c_…` | primeira inserção na terra: deriva de acomodação (90 s + 5 min + 15 min) |
| `3_ar_apos_solo.csv` | de volta ao ar após 25 min na terra (teste de absorção pela placa) |
| `4_agua.csv` | sensor na água (ponto de referência) |
| `5_solo_vaso_seco_60min.csv` | reinserção na terra seca: 60 min de acomodação |
| `6_degrau_ur.csv` | início do experimento de umidade do ar (interrompido; inclui o AHT20) |
| `7_solo_seco_acomodado.csv` | solo seco ao toque, ~14 h após a inserção (ponto de calibração 0 %) |
| `8_rega_2h.csv` | antes e depois da rega (35 min; interrompido para montar o divisor) |
| `9_pos_rega_divisor.csv` | continuação após a rega + divisor (GPIO35) + teste de troca dos resistores; t = 2543–2608 s é o ponto de calibração 100 % |
| `10_estabilizacao_gpio.csv` | saída do sensor nos 2 s após ligá-lo por GPIO (janelas de 5 ms, 3 ciclos) |
| `11_teste_falhas.csv` | saída bruta da serial do firmware do nó (tentativa de teste de falhas sem as falhas) |
| `12_estabilidade_10min.csv` | teste de estabilidade do firmware final: 303 linhas CSV, uma a cada 2 s |
| `13_alimentacao_sensor_solo.csv` | saída do sensor em função da alimentação (transcrito da serial dos testes de 30/09) |

Colunas usuais: `t_s` (tempo desde o início da captura), `mv_media` e `mv_desvio_bloco` (média e desvio das 64 leituras do solo), `temp_c`, `ur_pct`, `estado_aht20`, `div_mv` (ponto médio do divisor).

## Caracterização de 22/11/2026 (`brutos/caracterizacao_22_11/`)

Placa `5AC9009194`, firmware `diag_solo` (`no_sensor/diag/diag_solo.cpp`), capturado com `ferramentas/diag.py`. CSV: `tempo_ms,teste,pino,n,bruto,mv,umidade_pct,alim_mv,radio,dur_us` (`umidade_pct` sem o limite de 0–100 %; `alim_mv` sem sentido: divisor desmontado).

| Arquivo | Conteúdo |
|---|---|
| `t1_ar_sem_radio.csv` | teste 1: sensor no ar, 2 Hz por 30 s, leitura única (ruído do ADC) |
| `t2_estabilizacao_1.csv` | teste 2: 10 ms por 2 s depois de encaixar o VCC à mão (`t_rel_ms` = 0 no gatilho de 300 mV) |
| `t3_medias.csv` | teste 3: leitura única × média de 16 × média de 64, 60 rodadas |
| `t4_ar.csv`, `t4_agua.csv` | teste 4: patamares de calibração (média de 64, 1 Hz, 60 s) |
| `t5_dose0_seca.csv` | teste 5 interrompido: "terra seca" com a haste ainda molhada do teste 4 (709 mV), **não usar** |
| `aht_passo1_solo_coluna.csv` | conferência do solo com o 3V3 pela fileira central da protoboard (montagem do AHT20) |
