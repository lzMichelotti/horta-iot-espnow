"""Perfil de tempo acordado do nó por fase (Etapa 5).

Lê a captura do NÓ 1 feita por ferramentas/capturar_seriais.py (<prefixo>_no1.txt)
e, para cada despertar do deep sleep, junta:
  - a linha "rst:0x5 (DEEPSLEEP_RESET)" da ROM (instante do PC);
  - a linha [T0] <esp_timer> do início do setup();
  - a linha [FASES] com as durações medidas pelo firmware.

ROM + bootloader = (t_PC[T0] − t_PC[rst]) − esp_timer[T0]: a parte que o
firmware não enxerga. Incerteza: alguns ms (latência do USB e o tempo de
transmissão da própria linha "rst:" a 115200 baud).

Uso:
  .venv/bin/python ferramentas/analisar_energia.py dados/etapa5/brutos/p3_fases [outros prefixos...]
"""
import argparse
import re
import statistics
from pathlib import Path

FASES = ["setup_us", "serial_nvs_us", "sens_iniciar_us", "leitura_us", "ligar_us", "envio_us", "desligar_us",
         "impressao_us", "flush_ant_us", "fim_us", "light_sleep_us", "light_sleeps"]
NOMES = {
    "rom_boot_us": "ROM + bootloader (PC)",
    "setup_us": "Inicialização ESP-IDF/Arduino até o setup()",
    "serial_nvs_us": "Serial + contadores",
    "sens_iniciar_us": "Iniciar sensores (inclui a espera de 100 ms do AHT20)",
    "leitura_us": "Leitura (AHT20 + ADC)",
    "ligar_us": "Ligar Wi-Fi + ESP-NOW",
    "envio_us": "Envio (até o ACK)",
    "desligar_us": "Desligar Wi-Fi",
    "impressao_us": "Impressão da linha [ENVIO]",
    "flush_us": "Esvaziar a serial antes do sono",
}
LINHA = re.compile(r"^\s*([\d.]+) (.*)$")


def ciclos(arquivo):
    """Um dicionário por despertar do deep sleep (o boot inicial é ignorado)."""
    resultado, atual = [], None
    for bruta in Path(arquivo).read_text(encoding="utf-8").splitlines():
        m = LINHA.match(bruta)
        if not m:
            continue
        t, texto = float(m.group(1)), m.group(2)
        if texto.startswith("rst:"):
            atual = {"deep_sleep": "DEEPSLEEP" in texto, "t_rst": t}
            resultado.append(atual)
        elif atual is None:
            continue
        elif texto.startswith("[T0] "):
            atual["t_t0"], atual["t0_us"] = t, int(texto.split()[1])
        elif texto.startswith("[FASES] ") and texto[8].isdigit():
            valores = [int(v) for v in texto[8:].split(",")]
            atual.update(zip(FASES, valores))
    validos = [c for c in resultado if c["deep_sleep"] and "fim_us" in c and "t_t0" in c]
    # flush_ant_us chega no ciclo seguinte: desloca para o ciclo a que pertence.
    for anterior, seguinte in zip(validos, validos[1:]):
        anterior["flush_us"] = seguinte["flush_ant_us"]
    for c in validos:
        c["rom_boot_us"] = round((c["t_t0"] - c["t_rst"]) * 1e6 - c["t0_us"])
    return [c for c in validos if "flush_us" in c]


def perfil(lista):
    chaves = ["rom_boot_us", "setup_us", "serial_nvs_us", "sens_iniciar_us", "leitura_us", "ligar_us", "envio_us",
              "desligar_us", "impressao_us", "flush_us"]
    medianas = {k: statistics.median(c[k] for c in lista) for k in chaves}
    total = sum(medianas.values())
    print(f"Ciclos analisados (despertares do deep sleep): {len(lista)}\n")
    print("| Fase | Mediana (ms) | Mín–máx (ms) | % do ciclo |\n|---|---|---|---|")
    for k in chaves:
        v = [c[k] / 1000 for c in lista]
        print(f"| {NOMES[k]} | {medianas[k] / 1000:.1f} | {min(v):.1f}–{max(v):.1f} | {100 * medianas[k] / total:.0f} % |")
    print(f"| **Total acordado (soma das medianas)** | **{total / 1000:.1f}** | | 100 % |")
    fim = [c["fim_us"] / 1000 for c in lista]
    print(f"\nesp_timer no fim do ciclo (≈ acordado_ant_ms): mediana {statistics.median(fim):.1f} ms")
    if all("light_sleep_us" in c for c in lista):
        ls = statistics.median(c["light_sleep_us"] for c in lista)
        n = statistics.median(c["light_sleeps"] for c in lista)
        print(f"Em light sleep (dentro das fases dos sensores): mediana {ls / 1000:.1f} ms em {n:.0f} trechos")
        print(f"**Ativo (CPU ligada) = total − light sleep: {(total - ls) / 1000:.1f} ms**")
    return medianas


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("prefixos", nargs="+")
    args = ap.parse_args()
    for p in args.prefixos:
        print(f"\n## {Path(p).name}\n")
        perfil(ciclos(f"{p}_no1.txt"))


if __name__ == "__main__":
    main()
