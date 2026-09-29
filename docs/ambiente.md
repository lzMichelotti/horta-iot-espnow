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

Abrir o projeto por `horta.code-workspace` (multi-root): o PlatformIO só reconhece um projeto quando o `platformio.ini` está na raiz de uma pasta do workspace, por isso `no_sensor/` e `coordenador/` entram como pastas próprias. `.vscode/settings.json` aponta o Python para o `.venv` (scripts de `ferramentas/`).

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

## Firmware (nó sensor e coordenador) — PlatformIO

Os dois firmwares são projetos PlatformIO separados (`no_sensor/`, `coordenador/`) com a mesma plataforma fixada. Código compartilhado em `comum/`, incluído via `lib_deps = <nome>=symlink://../comum/<nome>`.

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

Validação: firmware "hello" gravado; a serial mostrou `boot:0x13 (SPI_FAST_FLASH_BOOT)` e `ESP-IDF v5.5.5`.

## Scripts do PC

Venv na raiz (`.venv/`, fora do Git), versões em `requirements.txt`: **pyserial 3.5** (usado por `ferramentas/medir_boot.py`). A gravação das placas é feita pelo PlatformIO, que traz o próprio esptool.

## Histórico: coordenador em MicroPython (Etapa 0 → substituído na Etapa 1)

Na Etapa 0 o coordenador foi montado em **MicroPython v1.29.0** (ESP32_GENERIC, `ESP32_GENERIC-20260824-v1.29.0.bin`, SHA-256 `e67ad6015a0a504c1fec9aa9bbf589d0432ed28e62546f4f8dd8a147f8bd95f6`, <https://micropython.org/download/ESP32_GENERIC/>), com esptool 5.4.0, mpremote 1.29.0 e micropython-esp32-stubs 1.29.0.post1. Validação pelo REPL: LittleFS v2 com 2048 KB (partição `vfs` em `0x200000`), ~163 KB de heap Python livre.

Na Etapa 1 a mesma tarefa (primeira linha → memória → Wi-Fi STA + MAC → LED) foi implementada nas duas linguagens e medida **na mesma placa** (COORD), 20 resets cada, com `ferramentas/medir_boot.py` (boot a frio):

| Medição | C++ | MicroPython |
|---|---|---|
| Reset → primeira linha (mediana) | 365 ms | 684 ms |
| Reset → Wi-Fi STA ativo e MAC lido | 454 ms | 811 ms |
| Ligar Wi-Fi + ler MAC (contador interno) | 104 ms | 107 ms |
| Imagem do firmware | 0,90 MB | 1,79 MB + script |
| Memória livre após o Wi-Fi | 189.904 B (heap IDF) | 142.080 B (heap Python) + 92.672 B (heap IDF) |
| Editar 1 linha → rodando na placa | 24–26 s | ~0,7 s (+2 s de espera do auto-reset) |

**Decisão: tudo em C++.** Boot 1,9× mais rápido e metade da flash nos nós (que rodam na bateria); pacote definido num único header compartilhado; servidor HTTP oficial no núcleo Arduino (`WebServer`), enquanto o `micropython-lib` não tem servidor HTTP. Os dois firmwares usam `CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP=y`, então ao acordar do deep sleep a diferença deve ser menor — medir na etapa de deep sleep. <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-guides/bootloader.html>

Código usado na comparação: commits `19eaed6` (C++), `90cb5e4` (MicroPython) e `1bc47fc` (script).

## Observações e problemas encontrados

1. **CH9102 em vez de CP2102.** As placas DOIT DevKit V1 recebidas usam a ponte WCH CH9102. Não há diferença funcional; muda apenas o driver.
2. **WSL não enxerga USB** sem o usbipd-win. Após `wsl --shutdown`, reiniciar o PC ou reconectar a placa, é preciso refazer o `usbipd attach`.
3. **Partição `vfs` criada em tempo de execução.** A tabela de partições gravada na flash (lida de volta em `0x8000`) tem apenas `nvs`, `phy_init` e `factory`, mas o MicroPython em execução informa uma partição `vfs` de 2 MB em `0x200000`. Conclusão empírica: o firmware cria a partição de FS no espaço restante. Isso não foi encontrado na documentação oficial.
4. **Sintaxe do esptool 5.** A página de download do MicroPython usa a sintaxe antiga (`esptool.py write_flash`); no esptool 5 os comandos são `esptool erase-flash` e `write-flash`.
5. **Ubuntu 24.04 bloqueia `pip install` global** (PEP 668). Por isso os scripts Python do PC usam um venv.
6. **Primeira compilação do nó levou ~4 min** por causa do download da plataforma, da toolchain e do framework; as seguintes levam segundos.
7. **Papéis das placas invertidos na Etapa 1.** Na Etapa 0 a placa `…1351` foi chamada de COORD e a `…2039` de NÓ 1. Como a `…1351` é a que já está montada com os sensores, os papéis foram trocados: COORD = `…2039`, NÓ 1 = `…1351`. As tabelas acima já refletem a atribuição nova; as medições do REPL foram feitas com o mesmo firmware e hardware idêntico.
8. **Abrir a porta serial reinicia a placa COORD (`…2039`).** No Linux, o pyserial ativa DTR/RTS ao abrir a porta e o circuito de auto-reset dessa placa gera um pulso no EN a cada abertura; a placa `…1351` só reiniciou na primeira abertura (teste: 3 aberturas em cada). Consequência (quando o COORD rodava MicroPython): o mpremote enviava Ctrl-C/Ctrl-A durante o boot e falhava com `could not enter raw repl`; contornado com `mpremote connect ... sleep 2`. Em C++, só significa que abrir o monitor reinicia o COORD.
