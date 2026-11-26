# Mantém a serial do coordenador aberta, porque abrir a porta reinicia a placa
# (Etapa 9): grava cada linha com o instante do PC ("<" recebida, ">" enviada)
# e manda ao console do coordenador o que chegar num FIFO.
#
#   .venv/bin/python ferramentas/coord_log.py dados/etapa9/rascunho/coord_log.txt &
#   echo "alertas" > dados/etapa9/rascunho/coord_cmd
#
# O FIFO fica, por padrão, na pasta do log (coord_cmd). O log bruto traz as
# linhas [SCAN] com os SSIDs das redes vizinhas: gravar em dados/*/rascunho/
# (fora do Git) e copiar para brutos/ sem elas (grep -v "\[SCAN\]").
import os, select, sys, time, serial

PORTA = "/dev/serial/by-id/usb-1a86_USB_Single_Serial_5AC9001351-if00"  # COORD desde 22/11/2026

log = sys.argv[1]
fifo = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(log)), "coord_cmd")
if not os.path.exists(fifo):
    os.mkfifo(fifo)
fd = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)

s = serial.Serial()
s.port, s.baudrate, s.timeout = PORTA, 115200, 0.05
s.dtr = False; s.rts = False
s.open()
resto = b""
with open(log, "a", buffering=1) as f:
    f.write(f"{time.time():.3f} # abrir\n")
    while True:
        r, _, _ = select.select([fd], [], [], 0)
        if r:
            dados = os.read(fd, 4096)
            if dados:
                for cmd in dados.decode().splitlines():
                    f.write(f"{time.time():.3f} > {cmd}\n")
                    s.write(cmd.encode() + b"\n")
            else:  # escritor fechou: reabre para o próximo echo
                os.close(fd)
                fd = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
        resto += s.read(4096)
        while b"\n" in resto:
            linha, resto = resto.split(b"\n", 1)
            f.write(f"{time.time():.3f} < {linha.decode('utf-8', 'replace').rstrip()}\n")
