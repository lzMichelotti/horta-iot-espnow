"""Análise dos experimentos de rádio da revisão do artigo (experimentos/radio).

Lê as capturas de ferramentas/capturar_seriais.py (<prefixo>_no1.txt e
<prefixo>_coord.txt) e, para cada experimento, calcula:
  - tempos por fase no nó (esp_timer): ligar o rádio, enviar, desligar e o
    total da aplicação (fim_us), em mediana e p95;
  - ROM + bootloader pelo PC: (t_PC[T0] − t_PC[rst]) − esp_timer[T0];
  - entrega: seq enviados pelo nó × seq recebidos pela COORD (o 1º ciclo é
    descartado: as duas placas reiniciam juntas ao abrir as portas);
  - ACK (ESP-NOW) e RSSI médio na COORD.

Uso:
  .venv/bin/python ferramentas/analisar_auditoria.py dados/auditoria_errc/brutos/e1_espnow [outros prefixos...]
"""
import argparse
import re
import statistics
from pathlib import Path

LINHA = re.compile(r"^\s*([\d.]+) (.*)$")
CAMPOS = ["modo", "seq", "ligado", "ok", "tentativas", "extra", "setup_us", "ligar_us", "enviar_us", "desligar_us",
          "fim_us"]


def ler(arquivo):
    for bruta in Path(arquivo).read_text(encoding="utf-8", errors="replace").splitlines():
        m = LINHA.match(bruta)
        if m:
            yield float(m.group(1)), m.group(2)


def ciclos_no(arquivo):
    ciclos, rst, t0 = [], None, None
    for t, texto in ler(arquivo):
        if texto.startswith("rst:"):
            rst, t0 = (t, "DEEPSLEEP" in texto), None
        elif texto.startswith("[T0] "):
            t0 = (t, int(texto.split()[1]))
        elif texto.startswith("[CICLO] "):
            v = texto[8:].split(",")
            c = dict(zip(CAMPOS, [v[0]] + [int(x) for x in v[1:]]))
            if rst and t0 and rst[1]:
                c["rom_us"] = ((t0[0] - rst[0]) - t0[1] / 1e6) * 1e6
            ciclos.append(c)
    return ciclos


def recebidos_coord(arquivo, origem):
    seqs, rssi, info = set(), [], []
    for _, texto in ler(arquivo):
        if texto.startswith(f"[RX] {origem},"):
            _, s, r = texto[5:].split(",")
            seqs.add(int(s))
            rssi.append(int(r))
        elif texto.startswith(("[BOOT]", "[BLE]", "[WIFI]", "[ESPNOW]", "[EST]")):
            info.append(texto)
    return seqs, rssi, info


def resumo(valores):
    if not valores:
        return "—"
    v = sorted(valores)
    p95 = v[min(len(v) - 1, int(round(0.95 * (len(v) - 1))))]
    return f"{statistics.median(v) / 1000:.1f} / {p95 / 1000:.1f}"


def analisar(prefixo):
    ciclos = ciclos_no(f"{prefixo}_no1.txt")[1:]  # 1º ciclo: COORD ainda iniciando
    if not ciclos:
        print(f"## {Path(prefixo).name}\n\nsem ciclos\n")
        return
    modo = ciclos[0]["modo"]
    origem = "ble" if modo.startswith("ble") else "espnow"
    seqs, rssi, info = recebidos_coord(f"{prefixo}_coord.txt", origem)
    enviados = {c["seq"] for c in ciclos if c["ligado"]}
    entregues = len(enviados & seqs)

    print(f"## {Path(prefixo).name} ({modo})\n")
    print(f"Ciclos analisados: {len(ciclos)}; rádio ligado em {len(enviados)}.  ")
    print(f"**Entrega na COORD: {entregues}/{len(enviados)} = {100 * entregues / max(1, len(enviados)):.1f} %**", end="")
    if origem == "espnow":
        acks = sum(c["ok"] for c in ciclos)
        tent = [c["tentativas"] for c in ciclos]
        print(f"; ACK no nó: {acks}/{len(ciclos)}; tentativas: "
              + ", ".join(f"{k}×{tent.count(k)}" for k in sorted(set(tent))), end="")
    if rssi:
        print(f"; RSSI médio {statistics.mean(rssi):.0f} dBm", end="")
    print("\n")
    print("| Fase | mediana / p95 (ms) |\n|---|---|")
    for nome, campo in [("ROM + bootloader (PC)", "rom_us"), ("Início até o setup()", "setup_us"),
                        ("Ligar o rádio", "ligar_us"), ("Enviar", "enviar_us"), ("Desligar o rádio", "desligar_us"),
                        ("Aplicação até o fim (esp_timer)", "fim_us")]:
        print(f"| {nome} | {resumo([c[campo] for c in ciclos if campo in c])} |")
    radio = [c["ligar_us"] + c["enviar_us"] + c["desligar_us"] for c in ciclos]
    total = [c["fim_us"] + c["rom_us"] for c in ciclos if "rom_us" in c]
    print(f"| **Rádio (ligar + enviar + desligar)** | **{resumo(radio)}** |")
    print(f"| **Acordado total (ROM + aplicação)** | **{resumo(total)}** |\n")
    for linha in info:
        if linha.startswith("[EST]"):
            ultimo_est = linha
        else:
            print(f"- `{linha}`")
    if any(l.startswith("[EST]") for l in info):
        print(f"- `{ultimo_est}`")
    print()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("prefixos", nargs="+")
    for p in ap.parse_args().prefixos:
        analisar(p)


if __name__ == "__main__":
    main()
