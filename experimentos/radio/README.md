# Experimentos de rádio (auditoria do parecer da ERRC)

Firmwares de teste para comparar, na mesma base de código, ESP-NOW, ESP-NOW cifrado e BLE no nó, e a coexistência Wi-Fi/BLE no coordenador. Não substituem os firmwares de produção. Resultados e conclusões: [`docs/auditoria_errc.md`](../../docs/auditoria_errc.md).

Usam a mesma plataforma e as mesmas tabelas de partições da produção: a NVS do nó (contador de boots) e o LittleFS da COORD não são tocados. Depois dos testes, regravar a produção (`cd coordenador && pio run -t upload`; `cd no_sensor && pio run -e bancada -t upload`).

| Ambiente | Placa | O que faz |
|---|---|---|
| `no_espnow` | NÓ 1 | igual ao envio da produção (`no_sensor/src/envio.cpp`) |
| `no_espnow_cripto` / `no_espnow_chave_errada` | NÓ 1 | PMK + LMK correta / LMK errada |
| `no_ble_vhci` / `no_ble_vhci_30ms` | NÓ 1 | BLE só pelo controlador (HCI via VHCI), anunciando 100 / 30 ms |
| `no_ble_arduino` | NÓ 1 | BLE pela biblioteca do núcleo (Bluedroid) |
| `coord_espnow` / `coord_cripto` | COORD | SoftAP canal 1 + ESP-NOW (NÓ 1 como peer cifrado) |
| `coord_coex` | COORD | idem + varredura BLE contínua |

Os ambientes da COORD também servem em `http://192.168.4.1` (rede `horta-exp`) uma página que se recarrega a cada 2 s, para simular o app consultando a API com um celular conectado (E9a/E9b); o tempo de atendimento sai na linha `[EST]`.

Rodar (cada teste: gravar a COORD, gravar o nó, capturar):

```bash
cd experimentos/radio
pio run -e coord_espnow -t upload && pio run -e no_espnow -t upload
cd ../.. && .venv/bin/python ferramentas/capturar_seriais.py 300 dados/auditoria_errc/brutos/e1_espnow
.venv/bin/python ferramentas/analisar_auditoria.py dados/auditoria_errc/brutos/e1_espnow
.venv/bin/python ferramentas/estimar_energia.py
```
