# Etapa 1 — Passo 5: tarefa de comparação C++ × MicroPython (exemplo; será
# substituído). Mesma sequência de no_sensor/src/main.cpp:
#   primeira linha → memória → liga Wi-Fi STA e lê o MAC → memória → pisca LED.

# O script ferramentas/medir_boot.py mede do reset até esta linha.
print("[BOOT] primeira linha")

import time
import gc
import network
import esp32
from machine import Pin

PINO_LED = 2  # LED azul (acende em nível alto)
INTERVALO_MS = 500


def heap_idf_livre():
    # Heap do ESP-IDF fora do heap Python; pode ser anexado ao heap Python
    # quando este enche. https://docs.micropython.org/en/latest/library/esp32.html
    return sum(h[1] for h in esp32.idf_heap_info(esp32.HEAP_DATA))


def mac_str(b):
    return ":".join("%02x" % x for x in b)


# Contador interno: não inclui ROM nem bootloader; só informativo.
print("[TEMPO] contador interno: {} ms".format(time.ticks_ms()))
gc.collect()
print("[MEM] livre antes do Wi-Fi: python {} bytes, idf {} bytes".format(gc.mem_free(), heap_idf_livre()))

t0 = time.ticks_us()
sta = network.WLAN(network.WLAN.IF_STA)
sta.active(True)  # a doc exige a interface ativa para ler o MAC
mac = sta.config("mac")
dt = time.ticks_diff(time.ticks_us(), t0)
print("[WIFI] STA ativa e MAC lido em {} us: {}".format(dt, mac_str(mac)))
gc.collect()
print("[MEM] livre depois do Wi-Fi: python {} bytes, idf {} bytes".format(gc.mem_free(), heap_idf_livre()))

led = Pin(PINO_LED, Pin.OUT)
aceso = False
while True:
    aceso = not aceso
    led.value(aceso)
    print("[LED] {}  t={} ms".format("aceso  " if aceso else "apagado", time.ticks_ms()))
    time.sleep_ms(INTERVALO_MS)
