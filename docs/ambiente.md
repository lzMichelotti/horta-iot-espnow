# Ambiente de desenvolvimento

Registro do ambiente montado na Etapa 0 (29/09/2026). Todas as versões abaixo foram lidas das próprias ferramentas, não copiadas de documentação.

## Sistema operacional

| Item | Versão |
|---|---|
| Host | Windows 11 Pro, build 10.0.26200 |
| WSL | 2.6.3.0, kernel 6.6.87.2-microsoft-standard-WSL2 |
| Distribuição | Ubuntu 24.04.4 LTS (systemd e udev ativos) |
| Python (sistema) | 3.12.3 |
| Git | 2.43.0 |

## Editor

| Item | Versão |
|---|---|
| VS Code (conectado ao WSL) | 1.139.1 |
| PlatformIO IDE | 3.3.4 |
| C/C++ (ms-vscode.cpptools) | 1.34.4 |
| Python (ms-python.python) | 2026.4.0 |
| Pylance | 2026.4.1 |

Configuração do Pylance com os stubs do MicroPython: `.vscode/settings.json`, baseada em <https://micropython-stubs.readthedocs.io/en/main/22_vscode.html> (caminho adaptado para Linux: `.venv/lib/python3.12/site-packages`).

## USB e portas seriais

O WSL2 não acessa USB diretamente. As placas são repassadas do Windows ao Linux com o **usbipd-win 5.3.0** (<https://learn.microsoft.com/windows/wsl/connect-usb>).

```
ESP32 ─ CH9102 ─USB─> Windows (driver WCH) ─ usbipd (USB/IP) ─> WSL2 (driver cdc_acm) ─> /dev/ttyACM*
```

- Ponte USB-serial das placas: **WCH CH9102** (VID:PID `1a86:55d4`), e não CP2102. No Windows: driver WCH 2.1.2025.7. No Linux: driver `cdc_acm` do kernel.
- `usbipd bind` feito uma vez por placa (PowerShell como administrador). `usbipd attach --wsl --busid <id>` é necessário a cada reconexão da placa ou reinício do WSL.
- Usuário adicionado ao grupo `dialout` (acesso a `/dev/ttyACM*`).

| Placa | Serial CH9102 | Windows | Linux (nome estável) | MAC (STA) |
|---|---|---|---|---|
| COORD | `5AC9002039` | COM5 | `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5AC9002039-if00` | `88:57:21:70:91:fc` |
| NÓ 1 | `5AC9001351` | COM3 | `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5AC9001351-if00` | `88:57:21:70:93:70` |

Chip das duas placas (lido pelo esptool): ESP32-D0WD-V3 rev v3.1, dual core 240 MHz, cristal 40 MHz, calibração de Vref do ADC em eFuse. Flash: 4 MB (fabricante `0x68`, dispositivo `0x4016`), 3,3 V.

A numeração `ttyACM0`/`ttyACM1` depende da ordem de conexão (na primeira vez, a placa `…1351` virou `ttyACM1`, e não `ttyACM0`), por isso usamos sempre `/dev/serial/by-id/` ou `mpremote connect id:<serial>`.

## Nó sensor — PlatformIO

| Item | Versão |
|---|---|
| PlatformIO Core | 6.2.0 (instalado pela extensão em `~/.platformio`, atalhos em `~/.local/bin`) |
| Plataforma | pioarduino `platform-espressif32` 55.3.312 (tag `55.03.312-1`) |
| **Arduino-ESP32** | **3.3.12** |
| ESP-IDF (bibliotecas do framework) | 5.5.5 |
| Toolchain | xtensa-esp-elf GCC 14.2.0 |
| esptool (do PlatformIO) | 5.4.0 |

Por que pioarduino: a plataforma oficial `platformio/platform-espressif32` (até a v7.1.3) ainda traz o Arduino-ESP32 2.0.17 (ESP-IDF 4.4.7), enquanto a documentação atual da Espressif descreve a 3.x. O pioarduino é um fork comunitário que empacota a 3.x; fixamos uma release exata no `platformio.ini`.

- <https://github.com/platformio/platform-espressif32/releases>
- <https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.312-1>

Tabela de partições usada pelo build (`default.csv` do Arduino): `nvs` 20 KB, `otadata` 8 KB, `app0` e `app1` 1280 KB cada, `spiffs` 1408 KB, `coredump` 64 KB.

Validação: firmware "hello" gravado no NÓ 1; a serial mostrou `boot:0x13 (SPI_FAST_FLASH_BOOT)` e `ESP-IDF v5.5.5`.

## Coordenador — MicroPython

Ferramentas no venv do projeto (`.venv/`, versões em `requirements.txt`):

| Item | Versão |
|---|---|
| esptool | 5.4.0 |
| mpremote | 1.29.0 |
| micropython-esp32-stubs | 1.29.0.post1 |
| micropython-stdlib-stubs (dependência) | 1.29.0.post2 |

Firmware:

| Item | Valor |
|---|---|
| Variante | ESP32_GENERIC (sem SPIRAM) |
| Versão | **MicroPython v1.29.0** (2026-08-24) |
| Arquivo | `ferramentas/firmware/ESP32_GENERIC-20260824-v1.29.0.bin` (1.790.544 bytes, fora do Git) |
| Origem | <https://micropython.org/download/ESP32_GENERIC/> |
| SHA-256 (calculado localmente) | `e67ad6015a0a504c1fec9aa9bbf589d0432ed28e62546f4f8dd8a147f8bd95f6` |

Gravação:

```bash
source .venv/bin/activate
PORTA=/dev/serial/by-id/usb-1a86_USB_Single_Serial_5AC9002039-if00
esptool --port $PORTA erase-flash
esptool --port $PORTA --baud 460800 write-flash 0x1000 ferramentas/firmware/ESP32_GENERIC-20260824-v1.29.0.bin
```

Validação pelo REPL (`mpremote connect id:5AC9002039`):

| Verificação | Resultado |
|---|---|
| `sys.implementation` | micropython 1.29.0, build `ESP32_GENERIC` |
| `os.statvfs('/')` | blocos de 4096 B × 512 = 2048 KB; 2036 KB livres |
| Tipo de FS | LittleFS v2 (assinatura `littlefs` no bloco 0 da partição `vfs`) |
| Partições (em execução) | `factory` 1984 KB em `0x10000`; `nvs` 24 KB; `phy_init` 4 KB; `vfs` 2048 KB em `0x200000` |
| Heap livre no boot | ~163 KB |

## Observações e problemas encontrados

1. **CH9102 em vez de CP2102.** As placas DOIT DevKit V1 recebidas usam a ponte WCH CH9102. Não há diferença funcional; muda apenas o driver.
2. **WSL não enxerga USB** sem o usbipd-win. Após `wsl --shutdown`, reiniciar o PC ou reconectar a placa, é preciso refazer o `usbipd attach`.
3. **Partição `vfs` criada em tempo de execução.** A tabela de partições gravada na flash (lida de volta em `0x8000`) tem apenas `nvs`, `phy_init` e `factory`, mas o MicroPython em execução informa uma partição `vfs` de 2 MB em `0x200000`. Conclusão empírica: o firmware cria a partição de FS no espaço restante. Isso não foi encontrado na documentação oficial.
4. **Sintaxe do esptool 5.** A página de download do MicroPython usa a sintaxe antiga (`esptool.py write_flash`); no esptool 5 os comandos são `esptool erase-flash` e `write-flash`.
5. **Ubuntu 24.04 bloqueia `pip install` global** (PEP 668). Por isso as ferramentas Python ficam num venv.
6. **Primeira compilação do nó levou ~4 min** por causa do download da plataforma, da toolchain e do framework; as seguintes levam segundos.
7. **Papéis das placas invertidos na Etapa 1.** Na Etapa 0 a placa `…1351` foi chamada de COORD e a `…2039` de NÓ 1. Como a `…1351` é a que já está montada com os sensores, os papéis foram trocados: COORD = `…2039`, NÓ 1 = `…1351`. As tabelas acima já refletem a atribuição nova; as medições do REPL foram feitas com o mesmo firmware e hardware idêntico.
8. **Abrir a porta serial reinicia a placa COORD (`…2039`).** No Linux, o pyserial ativa DTR/RTS ao abrir a porta e o circuito de auto-reset dessa placa gera um pulso no EN a cada abertura; a placa `…1351` só reiniciou na primeira abertura (teste: 3 aberturas em cada). Consequência: o mpremote envia Ctrl-C/Ctrl-A durante o boot e falha com `could not enter raw repl`. Solução: o comando `sleep` do mpremote logo após o `connect` (`mpremote connect id:5AC9002039 sleep 2 fs cp ...`), que espera o boot terminar. <https://docs.micropython.org/en/latest/reference/mpremote.html>
