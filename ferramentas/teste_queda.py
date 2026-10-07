"""Teste de queda de energia da COORD durante gravações (Etapa 7, passo 9).

A cada ciclo: espera a COORD aparecer (roda "usbipd.exe attach" sozinho depois
que o cabo é reconectado), abre a serial, lê a carga do histórico no boot,
compara com o que tinha sido confirmado antes do corte e manda gravar sem parar
("fs gravar 100000 v": uma linha [FS] gravado por registro confirmado, isto é,
depois do close()). O corte é feito à mão: desconectar o cabo USB da COORD.

  perdidos = confirmados antes do corte − válidos depois (deve ser 0)
  extras   = válidos depois − confirmados (0 ou 1: gravado, mas o corte veio
             antes de a confirmação sair pela serial)

Uso:
  .venv/bin/python ferramentas/teste_queda.py <ciclos> <saida> [--busid 1-1]
  ex.: ... teste_queda.py 20 dados/etapa7/brutos/queda
  gera <saida>.txt (serial bruta) e <saida>.csv (um resumo por ciclo)
"""
import argparse
import os
import re
import subprocess
import time

import serial

PORTA = "/dev/serial/by-id/usb-1a86_USB_Single_Serial_5AC9002039-if00"
CARGA = re.compile(r"\[FS\] carga: (\d+) segmentos, (\d+) registros validos, (\d+) invalidos, em (\d+) ms")
ESTADO = re.compile(r"\[FS\] estado: (\w+)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ciclos", type=int)
    ap.add_argument("saida")
    ap.add_argument("--busid", default="1-1")
    a = ap.parse_args()

    bruto = open(a.saida + ".txt", "w")
    resumo = open(a.saida + ".csv", "w")
    resumo.write("ciclo,montou,estado,segmentos,validos,invalidos,carga_ms,esperado,perdidos,extras,"
                 "confirmados_no_ciclo,segundos_gravando,erros_littlefs\n")

    def log(t):
        linha = f"{time.time():.3f} {t}"
        bruto.write(linha + "\n")
        bruto.flush()
        return linha

    esperado = None  # válidos que devem existir no próximo boot
    for ciclo in range(1, a.ciclos + 2):  # +1: o último boot só confere
        avisou = False
        while not os.path.exists(PORTA):
            if not avisou:
                print(f"[ciclo {ciclo}] aguardando a COORD (reconecte o cabo)...", flush=True)
                avisou = True
            subprocess.run(["usbipd.exe", "attach", "--wsl", "--busid", a.busid], capture_output=True)
            time.sleep(2)
        time.sleep(0.5)

        try:
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = PORTA, 115200, 0.2
            s.dtr, s.rts = False, False
            s.open()
        except (serial.SerialException, OSError):
            time.sleep(1)
            continue
        log(f"# ciclo {ciclo}: abrir")

        carga, estado, erros, confirmados, inicio_grav = None, None, 0, 0, None
        cortou = False
        try:
            fim = time.time() + 60
            while time.time() < fim:  # boot até o console ficar pronto
                t = s.readline().decode(errors="replace").rstrip()
                if not t or t.startswith("[SCAN]"):
                    continue
                log("< " + t)
                if (m := CARGA.search(t)):
                    carga = tuple(int(x) for x in m.groups())
                if (m := ESTADO.search(t)):
                    estado = m.group(1)
                if "esp_littlefs" in t and "E (" in t:
                    erros += 1
                if t.startswith("[FS] gravado"):
                    confirmados += 1
                if "[RESUMO] t_ms" in t:
                    break

            validos = carga[1] if carga else None
            perdidos = extras = ""
            if esperado is not None and validos is not None:
                perdidos = max(0, esperado - validos)
                extras = max(0, validos - esperado)
            base = validos if validos is not None else 0

            if ciclo <= a.ciclos:
                s.write(b"fs gravar 100000 v\n")
                log("> fs gravar 100000 v")
                inicio_grav = time.time()
                print(f"[ciclo {ciclo}] montou={carga is not None} estado={estado} validos={validos} "
                      f"invalidos={carga[2] if carga else None} perdidos={perdidos} extras={extras} "
                      f"-> GRAVANDO: pode desconectar o cabo", flush=True)
                while True:  # até o cabo sair
                    t = s.readline().decode(errors="replace").rstrip()
                    if t:
                        log("< " + t)
                        if t.startswith("[FS] gravado"):
                            confirmados += 1
        except (serial.SerialException, OSError):
            cortou = True
        finally:
            try:
                s.close()
            except Exception:
                pass

        segundos = round(time.time() - inicio_grav, 1) if inicio_grav else 0
        log(f"# ciclo {ciclo}: {'corte' if cortou else 'fim'}; confirmados no ciclo {confirmados}")
        resumo.write(f"{ciclo},{carga is not None},{estado},{carga[0] if carga else ''},{validos},"
                     f"{carga[2] if carga else ''},{carga[3] if carga else ''},{esperado if esperado is not None else ''},"
                     f"{perdidos},{extras},{confirmados},{segundos},{erros}\n")
        resumo.flush()
        if ciclo > a.ciclos:
            print(f"[fim] último boot: montou={carga is not None} validos={validos} perdidos={perdidos} "
                  f"extras={extras}", flush=True)
            break
        # Sem a carga deste boot (corte durante o boot), não dá para saber o esperado.
        esperado = base + confirmados if validos is not None else None
        print(f"[ciclo {ciclo}] corte detectado: {confirmados} confirmados em {segundos} s; "
              f"esperado no próximo boot: {esperado}", flush=True)


if __name__ == "__main__":
    main()
