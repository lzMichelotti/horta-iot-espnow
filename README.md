# horta-iot-espnow

Sistema de comunicação IoT para monitoramento de hortas urbanas, sem Internet: nós sensores ESP32 enviam leituras por ESP-NOW a um coordenador ESP32, que registra os dados e os disponibiliza por HTTP/JSON na própria rede Wi-Fi (SoftAP).

```
[Nó sensor 1] ─┐
               ├─ ESP-NOW (estrela) ─> [Coordenador] ─ SoftAP + HTTP/JSON ─> [App]
[Nó sensor 2] ─┘
```

## Estrutura

| Pasta | Conteúdo | Linguagem / ferramenta |
|---|---|---|
| `no_sensor/` | Firmware dos nós sensores | C++ (Arduino-ESP32 3.x), PlatformIO |
| `coordenador/src/` | Código do coordenador | MicroPython, mpremote |
| `docs/` | Documentação técnica (ambiente, protocolo) | Markdown |
| `ferramentas/` | Firmware MicroPython e utilitários do PC | — |

## Preparar o ambiente

Detalhes e versões em [`docs/ambiente.md`](docs/ambiente.md).

```bash
# Ferramentas do coordenador
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

# Nó sensor: compilar e gravar
cd no_sensor
pio run -t upload
pio device monitor
```

## Licença

A definir.
