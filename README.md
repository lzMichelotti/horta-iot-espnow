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
| `coordenador/` | Firmware do coordenador | C++ (Arduino-ESP32 3.x), PlatformIO |
| `comum/protocolo/` | Definição do pacote, compartilhada pelos dois firmwares | C++ |
| `docs/` | Documentação técnica (ambiente, placas, protocolo) | Markdown |
| `ferramentas/` | Scripts auxiliares que rodam no PC | Python |

## Preparar o ambiente

Detalhes e versões em [`docs/ambiente.md`](docs/ambiente.md).

```bash
# Firmware (dentro de no_sensor/ ou coordenador/)
pio run -t upload
pio device monitor

# Scripts do PC (ferramentas/)
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## Licença

A definir.
