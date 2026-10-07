"""Sessão roteirizada na serial da COORD (Etapa 7): manda comandos do console e
grava tudo o que a placa responde, com o instante do PC.

Uso:
  .venv/bin/python ferramentas/sessao_coord.py SAIDA PASSO [PASSO ...]
  ex.: ... sessao_coord.py dados/etapa7/brutos/x.txt abrir "ate:[RESUMO] t_ms:40" hora:1 "cmd:fs:2" fechar

Passos:
  abrir             abre a porta (reinicia a placa: power-on pelo pino EN)
  fechar            fecha a porta
  espera:S          só lê por S segundos
  cmd:TEXTO:S       envia TEXTO (uma linha do console) e lê por S segundos
  hora:S            acerta a hora com o relógio do PC ("hora <epoch>") e lê por S segundos
  ate:TEXTO:S       lê até aparecer TEXTO numa linha (no máximo S segundos)

As linhas [SCAN] (SSIDs das redes vizinhas) não são gravadas.
"""
import sys
import time

import serial

PORTA = "/dev/serial/by-id/usb-1a86_USB_Single_Serial_5AC9002039-if00"


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    saida = open(sys.argv[1], "w")
    porta = None

    def log(texto):
        linha = f"{time.time():.3f} {texto}"
        print(linha, flush=True)
        saida.write(linha + "\n")
        saida.flush()

    def ler(segundos, ate=None):
        fim = time.time() + segundos
        while time.time() < fim:
            b = porta.readline()
            if not b:
                continue
            t = b.decode(errors="replace").rstrip()
            if t.startswith("[SCAN]"):
                continue
            log("< " + t)
            if ate and ate in t:
                return True
        return False

    def enviar(texto):
        log("> " + texto)
        porta.write((texto + "\n").encode())

    for passo in sys.argv[2:]:
        tipo, _, resto = passo.partition(":")
        if tipo == "abrir":
            log("# abrir")
            porta = serial.Serial()
            porta.port, porta.baudrate, porta.timeout = PORTA, 115200, 0.2
            porta.dtr, porta.rts = False, False
            porta.open()
        elif tipo == "fechar":
            log("# fechar")
            porta.close()
        elif tipo == "espera":
            ler(float(resto))
        elif tipo == "hora":
            enviar(f"hora {int(time.time())}")
            ler(float(resto))
        elif tipo == "cmd":
            texto, _, seg = resto.rpartition(":")
            enviar(texto)
            ler(float(seg))
        elif tipo == "ate":
            texto, _, seg = resto.rpartition(":")
            if not ler(float(seg), texto):
                log(f"# AVISO: '{texto}' não apareceu em {seg} s")
        else:
            sys.exit(f"passo desconhecido: {passo}")
    if porta is not None and porta.is_open:
        porta.close()


if __name__ == "__main__":
    main()
