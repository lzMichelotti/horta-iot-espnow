"""Estimativa de carga por ciclo e de autonomia (revisão do artigo da ERRC).

Sem medição de corrente (decisão do projeto: sem multímetro/INA), a carga é
estimada como TEMPO MEDIDO × FAIXA DE CORRENTE DO DATASHEET. Cada fase recebe
um limite inferior e um superior; o resultado é uma faixa, não um valor.

Fontes das correntes:
  [ESP32]  ESP32 Series Datasheet v5.3 (Espressif, 2026), tab. 4-2 e 5-4:
           TX 802.11b 1 Mbps +19,5 dBm 240 mA; TX BLE 0 dBm 130 mA;
           RX 95–100 mA; modem-sleep 240 MHz 30–68 mA; light-sleep 0,8 mA;
           deep-sleep (RTC timer + memória RTC) 10 µA.
  [SX1276] Semtech SX1276/77/78/79 Datasheet Rev. 7 (2020), tab. 6:
           TX +20 dBm 120 mA, +17 dBm 87 mA, +14 dBm (RFO) ~29 mA a +13 dBm;
           RX 10,8 mA; sleep 0,2 µA; tempo no ar: seção 4.1.1.7.

Tempos medidos:
  - ciclo de produção do nó (Etapa 5, dados/etapa5/brutos/p4_4b_qio_80m,
    mediana de 11 ciclos, ferramentas/analisar_energia.py);
  - ESP-NOW × BLE na mesma base de firmware (experimentos/radio,
    dados/auditoria_errc/brutos, ferramentas/analisar_auditoria.py).

Uso:
  .venv/bin/python ferramentas/estimar_energia.py
"""
import math
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from analisar_auditoria import ciclos_no  # noqa: E402

BRUTOS = Path(__file__).parent.parent / "dados" / "auditoria_errc" / "brutos"

# Faixas de corrente (mA) por estado do ESP32 [ESP32]
CPU = (30.0, 68.0)        # ativo sem rádio (modem-sleep, 240 MHz, dual-core)
RADIO = (30.0, 100.0)     # fases de rádio ligado sem transmitir: entre CPU só e RX
TX_WIFI = 240.0           # 802.11b 1 Mbps
TX_BLE = 130.0            # BLE 0 dBm
LIGHT = 0.8
DEEP_CHIP = 0.010

# Ciclo de produção (Etapa 5, mediana, ms): fases fora do rádio, com e sem CPU
PROD = {
    "rom_boot": 36.0 + 11.2,          # ROM + bootloader + init até o setup()
    "cpu_resto": 0.9 + 0.9 + 0.4,     # serial/contadores + iniciar sensores + impressão
    "leitura_ativa": 97.5 - 90.1,     # leitura: parte com a CPU acordada
    "light_sleep": 90.1,              # esperas dos sensores em light sleep
    "radio_ligar": 19.8,
    "radio_enviar": 1.8,
    "radio_desligar": 1.0,
}
AIRTIME_ESPNOW_MS = 0.712             # 65 B a 1 Mbps (docs/protocolo.md, seção 9)


def faixa(t_ms, i):
    return (t_ms * i[0], t_ms * i[1])  # µC = mA × ms


def soma(*fx):
    return (sum(f[0] for f in fx), sum(f[1] for f in fx))


def carga_ciclo_producao():
    cpu_ms = PROD["rom_boot"] + PROD["cpu_resto"] + PROD["leitura_ativa"]
    radio_ms = PROD["radio_ligar"] + PROD["radio_enviar"] + PROD["radio_desligar"]
    radio = soma(faixa(radio_ms - AIRTIME_ESPNOW_MS, RADIO), faixa(AIRTIME_ESPNOW_MS, (TX_WIFI, TX_WIFI)))
    cpu = faixa(cpu_ms, CPU)
    light = faixa(PROD["light_sleep"], (LIGHT, LIGHT))
    return {"cpu": cpu, "radio": radio, "light": light, "total": soma(cpu, radio, light),
            "radio_ms": radio_ms, "acordado_ms": cpu_ms + radio_ms + PROD["light_sleep"]}


def mediana(prefixo, campo):
    return statistics.median(c[campo] for c in ciclos_no(BRUTOS / f"{prefixo}_no1.txt")[1:]) / 1000


def carga_radio_experimento(prefixo, airtime_ms, i_tx):
    """Fase de rádio (ligar + enviar + desligar) medida no experimento."""
    t = sum(mediana(prefixo, c) for c in ("ligar_us", "enviar_us", "desligar_us"))
    return t, soma(faixa(t - airtime_ms, RADIO), faixa(airtime_ms, (i_tx, i_tx)))


def toa_lora_ms(pl, sf, bw_hz=125e3, cr=1, preambulo=8, crc=1, ih=0):
    """Tempo no ar LoRa, Semtech SX1276 Rev. 7, seção 4.1.1.7."""
    tsym = (2 ** sf) / bw_hz * 1000
    de = 1 if tsym > 16 else 0  # LowDataRateOptimize obrigatório acima de 16 ms
    npay = 8 + max(math.ceil((8 * pl - 4 * sf + 28 + 16 * crc - 20 * ih) / (4 * (sf - 2 * de))) * (cr + 4), 0)
    return (preambulo + 4.25) * tsym + npay * tsym


def mah_dia(q_ciclo_uc, ciclos_dia, i_sono_ma):
    return q_ciclo_uc * ciclos_dia / 3.6e6 + i_sono_ma * 24  # µC → mAh


def main():
    ciclos_dia = 24 * 60 / 15  # período de produção: 15 min
    p = carga_ciclo_producao()
    print("## Ciclo de produção do nó (ESP-NOW, Etapa 5)\n")
    print(f"Acordado {p['acordado_ms']:.0f} ms, rádio ligado {p['radio_ms']:.1f} ms\n")
    print("| Parcela | Carga por ciclo (mC) | % do total (sup.) |\n|---|---|---|")
    for k in ("cpu", "radio", "light", "total"):
        a, b = p[k]
        print(f"| {k} | {a / 1000:.2f} – {b / 1000:.2f} | {100 * b / p['total'][1]:.0f} % |")
    print()

    print("## Fase de rádio: ESP-NOW × BLE (mesma base de firmware)\n")
    print("| Modo | Rádio ligado (ms) | Carga do rádio (mC) |\n|---|---|---|")
    exps = [("e1_espnow", "ESP-NOW", AIRTIME_ESPNOW_MS, TX_WIFI),
            ("e2_cripto", "ESP-NOW + CCMP", 0.840, TX_WIFI),
            ("e6_ble_vhci", "BLE (controlador, 100 ms)", 4 * 3 * 0.376, TX_BLE),
            ("e7_ble_vhci_30ms", "BLE (controlador, 30 ms)", 2 * 3 * 0.376, TX_BLE),
            ("e8_ble_arduino", "BLE (Bluedroid, 100 ms)", 4 * 3 * 0.376, TX_BLE)]
    for pref, nome, air, itx in exps:
        if not (BRUTOS / f"{pref}_no1.txt").exists():
            continue
        t, (a, b) = carga_radio_experimento(pref, air, itx)
        print(f"| {nome} | {t:.1f} | {a / 1000:.2f} – {b / 1000:.2f} |")
    print("\nBLE: anúncio legado de 31 B ≈ 376 µs no ar a 1 Mbps por canal (3 canais por evento);"
          " número de eventos estimado pelo intervalo de 20 ms + atraso aleatório de até 10 ms.\n")

    print("## LoRa (SX1276), 22 B de carga, BW 125 kHz, CR 4/5, CRC, preâmbulo 8\n")
    print("| SF | Tempo no ar (ms) | Carga TX a +17 dBm (mC) | a +20 dBm (mC) |\n|---|---|---|---|")
    for sf in (7, 9, 12):
        t = toa_lora_ms(22, sf)
        print(f"| {sf} | {t:.1f} | {t * 87 / 1000:.2f} | {t * 120 / 1000:.2f} |")
    print("\n(só o transceptor; o ESP32 continua acordado esperando o TxDone, somando 30–68 mA no mesmo tempo)\n")

    print(f"## Autonomia com 2000 mAh, {ciclos_dia:.0f} ciclos/dia (sem autodescarga, sem perdas do regulador)\n")
    print("| Corrente dormindo | mAh/dia | Dias |\n|---|---|---|")
    for i_sono, rot in [(DEEP_CHIP, "10 µA (só o chip, datasheet)"), (0.1, "0,1 mA (hipótese)"),
                        (1.0, "1 mA (hipótese)"), (5.0, "5 mA (hipótese)")]:
        lo = mah_dia(p["total"][0], ciclos_dia, i_sono)
        hi = mah_dia(p["total"][1], ciclos_dia, i_sono)
        print(f"| {rot} | {lo:.2f} – {hi:.2f} | {2000 / hi:.0f} – {2000 / lo:.0f} |")
    radio_dia = (p["radio"][0] * ciclos_dia / 3.6e6, p["radio"][1] * ciclos_dia / 3.6e6)
    print(f"\nParcela do rádio: {radio_dia[0]:.3f} – {radio_dia[1]:.3f} mAh/dia.")


if __name__ == "__main__":
    main()
