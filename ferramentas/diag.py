# Manda comandos ao firmware diag_solo (no_sensor/diag/diag_solo.cpp, env
# diag_solo) e grava a resposta: CSV com cabeçalho + comentários '#'.
# Uso: .venv/bin/python ferramentas/diag.py <comandos> <segundos> [arquivo_saida]
#   ex.: .venv/bin/python ferramentas/diag.py 1 35 dados/etapa2/brutos/caracterizacao_22_11/t1_ar_sem_radio.csv
# Comandos: 1, 2, 3, p, f, r, s, h (ver o cabeçalho de diag_solo.cpp).
import sys, time, serial

PORTA = "/dev/serial/by-id/usb-1a86_USB_Single_Serial_5AC9009194-if00"  # nó horta1 desde 22/11/2026
cmds, dur = sys.argv[1], float(sys.argv[2])
saida = sys.argv[3] if len(sys.argv) > 3 else None

s = serial.Serial()
s.port, s.baudrate, s.timeout = PORTA, 115200, 0.2
s.dtr = False; s.rts = False   # não segura a placa no reset
s.open()
time.sleep(1.5)                # caso a placa reinicie ao abrir a porta
s.reset_input_buffer()
for c in cmds:
    s.write(c.encode() + b"\n")
    time.sleep(0.1)

buf = b""
t0 = time.time()
while time.time() - t0 < dur:
    buf += s.read(4096)
s.close()

texto = buf.decode("utf-8", "replace").replace("\r", "")
print(texto)
if saida:
    with open(saida, "w") as f:
        f.write(texto)
    print(f"# salvo em {saida}")
