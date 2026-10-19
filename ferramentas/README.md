# ferramentas

Scripts que rodam no PC (Python, venv da raiz: `pip install -r requirements.txt`).

| Script | Uso |
|---|---|
| `medir_boot.py` | Mede, pelo PC, o tempo do reset até linhas-chave na serial: `.venv/bin/python ferramentas/medir_boot.py <porta> [repeticoes]` |
| `analisar_sensores.py` | Estatísticas e figuras da Etapa 2 (sensores) |
| `overhead_protocolo.py` | Tabela e figuras de overhead do pacote (Etapa 3) |
| `capturar_seriais.py` | Grava as seriais da COORD e do NÓ 1 ao mesmo tempo e comanda o pino EN pela linha RTS (reiniciar o nó, manter o coordenador desligado): `.venv/bin/python ferramentas/capturar_seriais.py 600 dados/etapa4/brutos/t1_entrega [--reset-no 30,60] [--coord-off 30:90] [--reset-coord-gravacao 3]`; `--reset-coord-gravacao N` (Etapa 7.1) reinicia a COORD logo depois da N-ésima gravação após o `--coord-off` |
| `sessao_coord.py` | Sessão roteirizada no console da COORD (Etapa 7): envia comandos e grava as respostas com o instante do PC: `.venv/bin/python ferramentas/sessao_coord.py <saida> abrir "ate:[RESUMO] t_ms:40" hora:1 "cmd:fs:2" fechar` |
| `teste_queda.py` | Teste de queda de energia da COORD (Etapa 7): manda gravar sem parar, detecta o corte (cabo USB desconectado à mão), reanexa pelo usbipd e confere o histórico no boot seguinte: `.venv/bin/python ferramentas/teste_queda.py 20 dados/etapa7/brutos/queda` |
| `exportar_historico.py` | Exporta o histórico da COORD em CSV pela serial (Etapa 7), acerta a hora e confere linhas e CRC-32: `.venv/bin/python ferramentas/exportar_historico.py dados/etapa7/exportacao.csv [--de T] [--ate T] [--no MAC] [--sem-hora]` |
| `analisar_comunicacao.py` | Resumo dos testes da Etapa 4 (entrega, tempos, RSSI/SNR, classes), cruzando nó × coordenador por (boot, seq): `.venv/bin/python ferramentas/analisar_comunicacao.py <prefixo> [...]`; `--figura` gera `docs/figuras/espnow_bancada.png` |
| `analisar_reenvio.py` | Testes da Etapa 7.1 (confirmação e reenvio): cada leitura gravada uma vez e em ordem, erro da hora (recepção − idade × instante da leitura, pelo relógio do PC), tempo de resposta e energia por ciclo (normal, com reenvio, coordenador fora); lê os nomes das colunas dos cabeçalhos do boot, então serve para o firmware antigo e o novo: `.venv/bin/python ferramentas/analisar_reenvio.py dados/etapa7_1/brutos/t2_coord_desligado [...]` |
| `analisar_auditoria.py` | Tempos, entrega e retransmissões dos testes do parecer da ERRC (E1–E9, `experimentos/radio`): `.venv/bin/python ferramentas/analisar_auditoria.py` |
| `estimar_energia.py` | Carga por ciclo e autonomia a partir dos tempos medidos e das correntes do datasheet (faixa, sem medição de corrente) |
| `chaves.py` | Chaves do HMAC (protocolo v2), só biblioteca padrão. `.venv/bin/python ferramentas/chaves.py mestra` gera a chave mestra em `coordenador/src/segredos.h` (uma vez; `--substituir` troca e invalida todos os nós). `.venv/bin/python ferramentas/chaves.py no 88:57:21:70:93:70` deriva a chave do nó pelo MAC STA e grava `no_sensor/src/segredos.h` (antes de gravar cada nó) |
