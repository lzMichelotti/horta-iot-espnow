# Etapa 1 — Passo 3: pisca-LED + mensagem na serial (exemplo; será substituído).
# boot.py roda antes deste arquivo; depois que main.py termina (ou é
# interrompido com Ctrl-C), o interpretador abre o REPL.
import time
from machine import Pin

# LED azul onboard no GPIO2 (confirmado piscando nas duas placas).
PINO_LED = 2
INTERVALO_MS = 500

# ticks_ms(): contador de milissegundos do MicroPython; não inclui o tempo
# do ROM e do bootloader, só a partir do início do firmware.
ms_no_main = time.ticks_ms()

led = Pin(PINO_LED, Pin.OUT)
print("[BOOT] main.py iniciou {} ms apos o inicio do contador".format(ms_no_main))
print("[BOOT] LED no GPIO{}, piscando a cada {} ms".format(PINO_LED, INTERVALO_MS))

aceso = False
while True:  # Ctrl-C gera KeyboardInterrupt, sai do laço e volta ao REPL
    aceso = not aceso
    led.value(aceso)
    print("[LED] {}  t={} ms".format("aceso  " if aceso else "apagado", time.ticks_ms()))
    time.sleep_ms(INTERVALO_MS)
