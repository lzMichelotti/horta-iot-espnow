"""Mede, pelo PC, o tempo do reset até linhas-chave na serial do ESP32.

Roda no PC (CPython), com o pyserial do .venv:
    .venv/bin/python ferramentas/medir_boot.py <porta> [repeticoes]

Método: pulso no RTS (ligado ao EN pelo circuito de auto-reset da placa),
t0 = soltar o reset; registra quando chegam o primeiro byte (log do ROM),
a linha "[BOOT] primeira linha" e a linha "[WIFI]". O mesmo cronômetro vale
para C++ e MicroPython. Inclui a latência USB, igual para os dois.
"""
import statistics
import sys
import time

import serial

MARCAS = {
    "rom": None,  # primeiro byte recebido
    "primeira_linha": b"[BOOT] primeira linha",
    "wifi": b"[WIFI]",
}
TIMEOUT_S = 10.0


def medir_uma(p):
    p.reset_input_buffer()
    p.rts = True  # EN em nível baixo: chip em reset
    time.sleep(0.05)
    p.reset_input_buffer()
    p.rts = False  # solta o reset
    t0 = time.perf_counter()

    tempos = {}
    buf = b""
    while time.perf_counter() - t0 < TIMEOUT_S and len(tempos) < len(MARCAS):
        n = p.in_waiting
        if not n:
            time.sleep(0.0005)
            continue
        buf += p.read(n)
        agora = (time.perf_counter() - t0) * 1000
        tempos.setdefault("rom", agora)
        for nome, marca in MARCAS.items():
            if marca and nome not in tempos and marca in buf:
                tempos[nome] = agora
    time.sleep(0.3)
    buf += p.read(p.in_waiting)
    return tempos, buf


def main():
    porta = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 20

    p = serial.Serial()
    p.port, p.baudrate, p.timeout = porta, 115200, 0
    p.dtr = p.rts = False  # abrir sem disparar o auto-reset
    p.open()
    time.sleep(3)  # se a abertura reiniciou a placa, espera o boot

    resultados = {k: [] for k in MARCAS}
    ultimo = b""
    for i in range(n):
        tempos, ultimo = medir_uma(p)
        for k in MARCAS:
            if k in tempos:
                resultados[k].append(tempos[k])
        print("rep {:2d}: ".format(i + 1) + "  ".join("{}={:.1f} ms".format(k, tempos.get(k, float("nan"))) for k in MARCAS))
        time.sleep(1.0)
    p.close()

    print("\nResumo ({} repeticoes), ms desde soltar o reset:".format(n))
    for k, v in resultados.items():
        if v:
            print("  {:15s} mediana {:7.1f}  min {:7.1f}  max {:7.1f}  (n={})".format(k, statistics.median(v), min(v), max(v), len(v)))
    print("\nSaida da ultima repeticao:")
    texto = ultimo.decode(errors="replace")
    print(texto[texto.find("[BOOT]"):][:600])


if __name__ == "__main__":
    main()
