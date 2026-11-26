"""Requisições à API do coordenador pela ponte HTTP (experimentos/ponte_http).

O PC não tem Wi-Fi: a placa da ponte entra no Wi-Fi da horta e faz as
requisições que o app faria. Grava cada resposta (JSON) e um CSV de tempos.

  .venv/bin/python ferramentas/ponte_http.py <pasta_saida> <repeticoes> /api/v1/status /api/v1/alertas ...
"""
import csv, json, os, re, sys, time, serial

PORTA = "/dev/serial/by-id/usb-1a86_USB_Single_Serial_5AC9009194-if00"
saida, reps, rotas = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
os.makedirs(saida, exist_ok=True)

s = serial.Serial()
s.port, s.baudrate, s.timeout = PORTA, 115200, 0.1
s.dtr = False; s.rts = False
s.open()
time.sleep(6)  # a placa pode reiniciar ao abrir a porta; espera o Wi-Fi conectar
s.reset_input_buffer()


def pedir(rota):
    s.write(f"GET {rota}\n".encode())
    buf, t0 = b"", time.time()
    while b"### fim" not in buf and time.time() - t0 < 30:
        buf += s.read(4096)
    txt = buf.decode("utf-8", "replace").replace("\r", "")
    m = re.search(r"### status=(-?\d+) ms=(\d+) bytes=(\d+)", txt)
    corpo = txt.split("\n", txt.count("\n"))  # linhas
    corpo = txt[txt.find("\n", m.start()) + 1: txt.rfind("### fim")].strip() if m else ""
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else (-1, -1, 0), corpo


with open(os.path.join(saida, "tempos.csv"), "a", newline="") as f:
    w = csv.writer(f)
    if f.tell() == 0:
        w.writerow(["em_utc", "rota", "status", "ms_cliente", "bytes"])
    for r in range(reps):
        for rota in rotas:
            (st, ms, nb), corpo = pedir(rota)
            w.writerow([time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), rota, st, ms, nb])
            nome = re.sub(r"[^a-z0-9]+", "_", rota.replace("/api/v1/", "")).strip("_")
            if r == 0:
                with open(os.path.join(saida, nome + ".json"), "w") as j:
                    try:
                        json.dump(json.loads(corpo), j, ensure_ascii=False, indent=2)
                    except ValueError:
                        j.write(corpo)
            print(f"{rota:40s} status={st} ms={ms} bytes={nb}", flush=True)
s.close()
