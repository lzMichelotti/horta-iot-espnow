# ferramentas

Scripts que rodam no PC (Python, venv da raiz: `pip install -r requirements.txt`).

| Script | Uso |
|---|---|
| `medir_boot.py` | Mede, pelo PC, o tempo do reset até linhas-chave na serial: `.venv/bin/python ferramentas/medir_boot.py <porta> [repeticoes]` |
| `analisar_sensores.py` | Estatísticas e figuras da Etapa 2 (sensores) |
| `overhead_protocolo.py` | Tabela e figuras de overhead do pacote (Etapa 3) |
| `capturar_seriais.py` | Grava as seriais da COORD e do NÓ 1 ao mesmo tempo e comanda o pino EN pela linha RTS (reiniciar o nó, manter o coordenador desligado): `.venv/bin/python ferramentas/capturar_seriais.py 600 dados/etapa4/brutos/t1_entrega [--reset-no 30,60] [--coord-off 30:90]` |
| `sessao_coord.py` | Sessão roteirizada no console da COORD (Etapa 7): envia comandos e grava as respostas com o instante do PC: `.venv/bin/python ferramentas/sessao_coord.py <saida> abrir "ate:[RESUMO] t_ms:40" hora:1 "cmd:fs:2" fechar` |
| `analisar_comunicacao.py` | Resumo dos testes da Etapa 4 (entrega, tempos, RSSI/SNR, classes), cruzando nó × coordenador por (boot, seq): `.venv/bin/python ferramentas/analisar_comunicacao.py <prefixo> [...]`; `--figura` gera `docs/figuras/espnow_bancada.png` |
