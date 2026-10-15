"""Exporta o histórico da COORD em CSV pela serial (Etapa 7, passo 10).

Abre a serial (a COORD reinicia: pino EN), espera o boot, acerta a hora com o
relógio do PC (abrir a porta zera o relógio provisório; --sem-hora desliga),
manda "exportar" com os filtros e grava as linhas [EXP] num arquivo. No fim,
confere a quantidade de linhas e o CRC-32 (zlib) informados pela COORD; só
então renomeia o arquivo .parcial para o nome final.

Uso:
  .venv/bin/python ferramentas/exportar_historico.py <saida.csv> [--de T] [--ate T] [--no MAC] [--sem-hora]
  T em epoch ou AAAA-MM-DDTHH:MM:SSZ (UTC). Ex.:
  ... exportar_historico.py dados/etapa7/exportacao.csv --de 2026-10-15T00:00:00Z --no 88:57:21:70:93:70

As demais linhas da serial vão para <saida.csv>.log.
Sai com código 0 se a transferência foi conferida, 1 se veio incompleta ou corrompida.
"""
import argparse
import os
import re
import sys
import time
import zlib

import serial

PORTA = "/dev/serial/by-id/usb-1a86_USB_Single_Serial_5AC9002039-if00"
FIM = re.compile(r"\[EXPORT\] fim linhas=(\d+) crc32=([0-9A-Fa-f]{8}) ms=(\d+)")
SEM_DADOS_S = 60  # sem nenhuma linha [EXP] por este tempo: transferência parada


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("saida")
    ap.add_argument("--de")
    ap.add_argument("--ate")
    ap.add_argument("--no")
    ap.add_argument("--sem-hora", action="store_true", help="não acertar a hora da COORD")
    ap.add_argument("--porta", default=PORTA)
    a = ap.parse_args()

    filtros = " ".join(f"{k}={v}" for k, v in (("de", a.de), ("ate", a.ate), ("no", a.no)) if v)
    parcial = a.saida + ".parcial"
    arq = open(parcial, "w", newline="\n")
    log = open(a.saida + ".log", "w")  # demais linhas da serial ([CSV], [FS], fila cheia...)

    s = serial.Serial()
    s.port, s.baudrate, s.timeout = a.porta, 115200, 0.5
    s.dtr, s.rts = False, False  # a placa reinicia mesmo assim (DTR/RTS sobem ao abrir o tty)
    s.open()

    def linha():
        t = s.readline().decode(errors="replace").rstrip("\r\n")
        if t and not t.startswith("[EXP] ") and not t.startswith("[SCAN]"):
            log.write(f"{time.time():.3f} {t}\n")
        return t

    print("aguardando o boot da COORD...", flush=True)
    fim = time.time() + 60
    while time.time() < fim and "[RESUMO] t_ms" not in linha():
        pass
    if not a.sem_hora:
        s.write(f"hora {int(time.time())}\n".encode())
        print("hora acertada com o relógio do PC", flush=True)
    s.write(f"exportar {filtros}\n".encode())
    print(f"exportando ({filtros or 'histórico inteiro'})...", flush=True)

    crc, linhas, cabecalho, resultado = 0, 0, False, None
    t0 = ultimo_dado = time.time()
    try:
        while True:
            t = linha()
            if t.startswith("[EXP] "):
                conteudo = t[len("[EXP] "):]
                arq.write(conteudo + "\n")
                crc = zlib.crc32((conteudo + "\n").encode(), crc)
                if cabecalho:
                    linhas += 1
                    if linhas % 1000 == 0:
                        print(f"  {linhas} linhas ({time.time() - t0:.0f} s)", flush=True)
                cabecalho = True
                ultimo_dado = time.time()
            elif (m := FIM.search(t)):
                resultado = (int(m.group(1)), int(m.group(2), 16), int(m.group(3)))
                break
            elif t.startswith("[EXPORT] ERRO") or t.startswith("[EXPORT] interrompida"):
                print(t, flush=True)
                break
            elif time.time() - ultimo_dado > SEM_DADOS_S:
                print(f"nenhuma linha em {SEM_DADOS_S} s: transferência parada", flush=True)
                break
    except (serial.SerialException, OSError) as e:
        print(f"a porta caiu: {e}", flush=True)
    finally:
        arq.close()
        log.close()
        try:
            s.close()
        except Exception:
            pass

    duracao = time.time() - t0
    if resultado is None:
        print(f"INCOMPLETA: {linhas} linhas recebidas em {duracao:.0f} s; arquivo mantido em {parcial}")
        sys.exit(1)
    n_coord, crc_coord, ms = resultado
    if n_coord != linhas or crc_coord != crc:
        print(f"CORROMPIDA: COORD informou {n_coord} linhas e crc32 {crc_coord:08X}; "
              f"recebidas {linhas} linhas e crc32 {crc:08X}; arquivo mantido em {parcial}")
        sys.exit(1)
    os.replace(parcial, a.saida)
    print(f"OK: {linhas} linhas, crc32 {crc:08X} conferido, {duracao:.0f} s ({ms} ms na COORD) -> {a.saida}")


if __name__ == "__main__":
    main()
