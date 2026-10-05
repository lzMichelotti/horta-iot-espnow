"""Captura simultânea das seriais da COORD e do NÓ 1 (Etapa 4).

Cada linha é gravada com o instante do PC (s desde o início) num arquivo por
placa. Também comanda o pino EN de cada placa pela linha RTS da ponte USB
(circuito de auto-reset da DevKit), para os testes de bancada:

  --reset-no 30,60        reinicia o NÓ 1 aos 30 s e aos 60 s
  --coord-off 20:80       mantém a COORD em reset (EN em 0) de 20 s a 80 s

Uso:
  .venv/bin/python ferramentas/capturar_seriais.py <duracao_s> <prefixo_saida> [opções]
  ex.: ... capturar_seriais.py 600 dados/etapa4/brutos/entrega

Observação: abrir a porta reinicia as duas placas (o Linux ativa DTR/RTS ao
abrir o tty, antes de o pyserial aplicar dtr=False/rts=False).
"""
import argparse
import threading
import time

import serial

PORTAS = {
    "coord": "/dev/serial/by-id/usb-1a86_USB_Single_Serial_5AC9002039-if00",
    "no1": "/dev/serial/by-id/usb-1a86_USB_Single_Serial_5AC9001351-if00",
}


def lista_floats(texto):
    return [float(x) for x in texto.split(",") if x]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("duracao", type=float)
    ap.add_argument("prefixo")
    ap.add_argument("--reset-no", type=lista_floats, default=[], help="instantes (s) para reiniciar o NÓ 1")
    ap.add_argument("--coord-off", default="", help="janela INICIO:FIM (s) com a COORD em reset")
    ap.add_argument("--so", choices=list(PORTAS), help="capturar só uma placa")
    args = ap.parse_args()

    # Eventos de RTS por placa: (instante, valor). RTS ativo = EN em 0 = chip em reset.
    eventos = {"coord": [], "no1": []}
    for t in args.reset_no:
        eventos["no1"] += [(t, True), (t + 0.1, False)]
    if args.coord_off:
        ini, fim = (float(x) for x in args.coord_off.split(":"))
        eventos["coord"] += [(ini, True), (fim, False)]

    t0 = time.time()
    placas = [args.so] if args.so else list(PORTAS)

    def ler(nome):
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = PORTAS[nome], 115200, 0.05
        s.dtr = False
        s.rts = False
        s.open()
        pendentes = sorted(eventos[nome])
        with open(f"{args.prefixo}_{nome}.txt", "w", encoding="utf-8") as saida:
            while (agora := time.time() - t0) < args.duracao:
                while pendentes and pendentes[0][0] <= agora:
                    _, valor = pendentes.pop(0)
                    s.rts = valor
                    saida.write(f"{agora:9.3f} #RTS {'ativo (EN=0)' if valor else 'solto'}\n")
                linha = s.readline()
                if linha:
                    saida.write(f"{time.time() - t0:9.3f} {linha.decode(errors='replace').rstrip()}\n")
                    saida.flush()
        s.close()

    threads = [threading.Thread(target=ler, args=(n,)) for n in placas]
    for th in threads:
        th.start()
    for th in threads:
        th.join()
    print(f"captura concluída: {args.prefixo}_{{{','.join(placas)}}}.txt")


if __name__ == "__main__":
    main()
