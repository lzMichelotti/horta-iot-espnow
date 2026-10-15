"""Teste de queda de energia da COORD durante gravações (Etapa 7, passo 9).

A cada ciclo: espera a COORD aparecer (roda "usbipd.exe attach" sozinho depois
que o cabo é reconectado), abre a serial, lê a carga do histórico no boot,
compara com o que tinha sido confirmado antes do corte e manda gravar sem parar
("fs gravar 100000 v": uma linha [FS] gravado por registro confirmado, isto é,
depois do close()). O corte é feito à mão: desconectar o cabo USB da COORD.

  A conta usa o id global (segmento × 500 + posição), não a quantidade de
  registros, para não ser afetada pela rotação:
  perdidos = id do último confirmado antes do corte − id do último presente
             no boot seguinte (lido de "fs listar"); deve ser 0
  extras   = presente − confirmado (0 ou 1: gravado, mas o corte veio antes
             de a confirmação sair pela serial)

Uso:
  .venv/bin/python ferramentas/teste_queda.py <ciclos> <saida> [--busid 1-4]
  ex.: ... teste_queda.py 20 dados/etapa7/brutos/queda
  gera <saida>.txt (serial bruta) e <saida>.csv (um resumo por ciclo)
"""
import argparse
import os
import re
import subprocess
import threading
import time

import serial

PORTA = "/dev/serial/by-id/usb-1a86_USB_Single_Serial_5AC9002039-if00"
CARGA = re.compile(r"\[FS\] carga: (\d+) segmentos, (\d+) registros validos, (\d+) invalidos, em (\d+) ms")
ESTADO = re.compile(r"\[FS\] estado: (\w+)")
GRAVADO = re.compile(r"\[FS\] gravado,(\d+),(\d+),")
LINHA_LISTAR = re.compile(r"\[FS\] (\d+),(\d+),\d+,\d+,")


def abrir_com_prazo(prazo=5.0):
    """Abre a porta numa thread: num dispositivo "fantasma" do usbip (a porta
    existe no Linux, mas a conexão morreu), open() fica preso para sempre."""
    resultado = []

    def tentar():
        try:
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = PORTA, 115200, 0.2
            s.dtr, s.rts = False, False
            s.open()
            resultado.append(s)
        except (serial.SerialException, OSError):
            pass

    t = threading.Thread(target=tentar, daemon=True)
    t.start()
    t.join(prazo)
    return resultado[0] if resultado else None


def abrir(busid, ciclo):
    """Espera a COORD voltar (anexando pelo usbipd) e abre a porta."""
    avisou = False
    while True:
        if not os.path.exists(PORTA):
            if not avisou:
                print(f"[ciclo {ciclo}] aguardando a COORD (reconecte o cabo)...", flush=True)
                avisou = True
            subprocess.run(["usbipd.exe", "attach", "--wsl", "--busid", busid], capture_output=True)
            time.sleep(2)
            continue
        time.sleep(0.5)
        s = abrir_com_prazo()
        if s is not None:
            return s
        # Fantasma ou falha: refaz a conexão do usbip.
        subprocess.run(["usbipd.exe", "detach", "--busid", busid], capture_output=True)
        time.sleep(2)
        subprocess.run(["usbipd.exe", "attach", "--wsl", "--busid", busid], capture_output=True)
        time.sleep(3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ciclos", type=int)
    ap.add_argument("saida")
    ap.add_argument("--busid", default="1-4", help="busid da COORD no usbipd.exe list")
    ap.add_argument("--k", type=int, default=500, help="registros por segmento (config.h)")
    a = ap.parse_args()

    bruto = open(a.saida + ".txt", "w")
    resumo = open(a.saida + ".csv", "w")
    resumo.write("ciclo,montou,estado,segmentos,validos,invalidos,carga_ms,ultimo_confirmado,ultimo_presente,"
                 "perdidos,extras,confirmados_no_ciclo,segundos_gravando,erros_littlefs\n")

    def log(t):
        linha = f"{time.time():.3f} {t}"
        bruto.write(linha + "\n")
        bruto.flush()
        return linha

    def ler_linha(s):
        t = s.readline().decode(errors="replace").rstrip()
        if t and not t.startswith("[SCAN]"):
            log("< " + t)
            return t
        return ""

    # Ids globais (segmento × K + posição): a conta não depende da rotação.
    ultimo_confirmado = None  # id do último "[FS] gravado" antes do corte
    for ciclo in range(1, a.ciclos + 2):  # +1: o último boot só confere
        s = abrir(a.busid, ciclo)
        log(f"# ciclo {ciclo}: abrir")

        carga, estado, erros, confirmados, inicio_grav = None, None, 0, 0, None
        presente = None
        confirmado_antes = ultimo_confirmado  # do ciclo anterior: o que deve estar na flash
        perdidos = extras = ""
        cortou = False

        def contar_gravado(t):
            nonlocal confirmados, ultimo_confirmado
            if (m := GRAVADO.match(t)):
                confirmados += 1
                ultimo_confirmado = int(m.group(1)) * a.k + int(m.group(2))

        try:
            fim = time.time() + 60
            while time.time() < fim:  # boot até o console ficar pronto
                t = ler_linha(s)
                if (m := CARGA.search(t)):
                    carga = tuple(int(x) for x in m.groups())
                if (m := ESTADO.search(t)):
                    estado = m.group(1)
                if "esp_littlefs" in t and "E (" in t:
                    erros += 1
                if "[RESUMO] t_ms" in t:
                    break

            # Último registro presente: última linha de "fs listar" (segmento, posições).
            s.write(b"fs listar\n")
            log("> fs listar")
            fim, linhas = time.time() + 4, []
            while time.time() < fim:
                t = ler_linha(s)
                if (m := LINHA_LISTAR.match(t)):
                    linhas.append(m)
                    fim = time.time() + 0.6  # para logo depois da última linha
                contar_gravado(t)
            if linhas and int(linhas[-1].group(2)) > 0:
                presente = int(linhas[-1].group(1)) * a.k + int(linhas[-1].group(2)) - 1
            if confirmado_antes is not None and presente is not None:
                perdidos = max(0, confirmado_antes - presente)
                extras = max(0, presente - confirmado_antes)

            if ciclo <= a.ciclos:
                s.write(b"fs gravar 100000 v\n")
                log("> fs gravar 100000 v")
                inicio_grav = time.time()
                print(f"[ciclo {ciclo}] montou={carga is not None} estado={estado} "
                      f"invalidos={carga[2] if carga else None} confirmado_antes={confirmado_antes} presente={presente} "
                      f"perdidos={perdidos} extras={extras} -> GRAVANDO: pode desconectar o cabo", flush=True)
                while True:  # até o cabo sair
                    contar_gravado(ler_linha(s))
        except (serial.SerialException, OSError):
            cortou = True
        finally:
            try:
                s.close()
            except Exception:
                pass

        segundos = round(time.time() - inicio_grav, 1) if inicio_grav else 0
        log(f"# ciclo {ciclo}: {'corte' if cortou else 'fim'}; confirmados no ciclo {confirmados}; "
            f"ultimo confirmado {ultimo_confirmado}")
        resumo.write(f"{ciclo},{carga is not None},{estado},{carga[0] if carga else ''},{carga[1] if carga else ''},"
                     f"{carga[2] if carga else ''},{carga[3] if carga else ''},{confirmado_antes if confirmado_antes is not None else ''},"
                     f"{presente if presente is not None else ''},{perdidos},{extras},{confirmados},{segundos},{erros}\n")
        resumo.flush()
        if ciclo > a.ciclos:
            print(f"[fim] último boot: montou={carga is not None} presente={presente} perdidos={perdidos} "
                  f"extras={extras}", flush=True)
            break
        print(f"[ciclo {ciclo}] corte detectado: {confirmados} confirmados em {segundos} s; "
              f"último confirmado {ultimo_confirmado}", flush=True)

if __name__ == "__main__":
    main()
