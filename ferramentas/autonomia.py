"""Estimativa de autonomia do nó sensor (Etapa 5, arquitetura de 26/11/2026).

Arquitetura: 18650 → TP4056 com proteção (DW01A) → RT9080-33 → pino 3V3 da
DevKit (VIN desligado, LED retirado); sensor de solo pelo GPIO25 só na leitura;
AHT20 no 3V3; divisor 100k + 100k da célula ao GPIO35.

Cada valor é uma faixa (mínimo, máximo) com a origem no comentário. Gera as
tabelas em Markdown de docs/energia.md, seção 7.

uso: python ferramentas/autonomia.py [capacidade_mAh] [fracao_util]
     (padrão: 3000 mAh e 0,8, EXEMPLOS até a célula ser escolhida)
"""
import sys

# ---------------------------------------------------------------- Dormindo (µA)
DORMINDO_UA = {
    # ESP32 Series Datasheet v5.3: "Power consumption in Deep-sleep mode is 10 µA"
    'ESP32 em deep sleep': (10.0, 10.0),
    # Richtek DS9080-09, Electrical Characteristics: IQ 2 típ., 4 máx.
    'Regulador RT9080-33': (2.0, 4.0),
    # Lei de Ohm: célula de 3,3 a 4,2 V sobre 200 kΩ
    'Divisor 100k + 100k': (3.3 / 200e3 * 1e6, 4.2 / 200e3 * 1e6),
    # AHT20 Data Sheet V1.0: no máximo 250 nA em repouso (docs/sensores.md, 2)
    'AHT20 em repouso': (0.0, 0.25),
    # TP4056 (cópia do datasheet): BAT em sleep, VCC = 0 V: 0,3 típ., 2 máx.
    'TP4056 sem USB': (0.3, 2.0),
    # DW01 (Fortune, rev. 1.0): supply current 3,0 típ., 6,0 máx. a 3,9 V
    'Proteção DW01A': (3.0, 6.0),
    # CH9102 Datasheet 1C, 6.2: ISLP 0,085 típ./0,15 máx. mA SE o chip for
    # alimentado pelo 3V3 sem USB; 0 se ele só recebe energia do USB (hipótese)
    'Ponte USB CH9102X': (0.0, 150.0),
    # LED de alimentação: retirado. Sensor de solo: pino solto no deep sleep.
    # Regulador 1117 da placa alimentado "ao contrário": fuga não documentada,
    # FORA da conta (o teste de descarga mede tudo junto).
}

# ---------------------------------------------------------------- Acordado (mC por ciclo)
# Base: ciclo de produção com confirmação (docs/energia.md, seção 3): 3,10–6,82 mC.
BASE_MC = (3.10, 6.82)
# Espera de estabilização do solo: +~410 ms acordado (581–586 ms medidos em 26/11
# contra ~171–178 ms), em light sleep a 0,8 mA (ESP32 DS v5.3, tab. 4-2).
ESPERA_MC = (0.41 * 0.8, 0.41 * 0.8)
# Sensor de solo ligado pelo GPIO25: 4,38 ± 0,45 mA (sensores.md 4.5) por 0,5–0,6 s.
SOLO_MC = ((4.38 - 0.45) * 0.5, (4.38 + 0.45) * 0.6)
# Coordenador fora (regra adaptativa, 1 tentativa): rádio de 236 ms (mediana,
# energia.md 5). Escalado da medição de 3 tentativas (197 ms de rádio → 19–48 mC).
FORA_BASE_MC = (19.0 * 236 / 197, 48.0 * 236 / 197)


def soma(faixas):
    return (sum(f[0] for f in faixas), sum(f[1] for f in faixas))


def por_dia(ciclos, mc_ciclo, ua_dormindo):
    acordado = (ciclos * mc_ciclo[0] / 3600, ciclos * mc_ciclo[1] / 3600)  # mC → mAh
    dormindo = (ua_dormindo[0] * 24 / 1000, ua_dormindo[1] * 24 / 1000)
    return acordado, dormindo, (acordado[0] + dormindo[0], acordado[1] + dormindo[1])


def fmt(x, casas=2):
    return f'{x:.{casas}f}'.replace('.', ',')


def main():
    cap = float(sys.argv[1]) if len(sys.argv) > 1 else 3000.0
    frac = float(sys.argv[2]) if len(sys.argv) > 2 else 0.8
    util = cap * frac

    dorm = soma(DORMINDO_UA.values())
    print('Dormindo (µA):')
    print('| Parte | Mín. | Máx. |\n|---|---|---|')
    for nome, (a, b) in DORMINDO_UA.items():
        print(f'| {nome} | {fmt(a, 1)} | {fmt(b, 1)} |')
    print(f'| **Total** | **{fmt(dorm[0], 1)}** | **{fmt(dorm[1], 1)}** |\n')

    normal = soma([BASE_MC, ESPERA_MC, SOLO_MC])
    fora = soma([FORA_BASE_MC, ESPERA_MC, SOLO_MC])
    print('Acordado (mC por ciclo):')
    print(f'| Ciclo normal | {fmt(normal[0])} | {fmt(normal[1])} |')
    print(f'| Coordenador fora | {fmt(fora[0])} | {fmt(fora[1])} |\n')

    print(f'Autonomia (capacidade útil = {fmt(cap, 0)} mAh × {fmt(frac)} = {fmt(util, 0)} mAh):')
    print('| Cenário | Ciclos/dia | Acordado (mAh/dia) | Dormindo (mAh/dia) | Total (mAh/dia) | Gasto em 30 dias (mAh) | Autonomia (dias) |')
    print('|---|---|---|---|---|---|---|')
    for nome, periodo_min, mc in [('Normal, 10 min', 10, normal), ('Normal, 15 min', 15, normal),
                                  ('Normal, 30 min', 30, normal), ('Coordenador fora, 15 min', 15, fora)]:
        ciclos = 24 * 60 // periodo_min
        ac, dm, tot = por_dia(ciclos, mc, dorm)
        print(f'| {nome} | {ciclos} | {fmt(ac[0])}–{fmt(ac[1])} | {fmt(dm[0])}–{fmt(dm[1])} | '
              f'{fmt(tot[0])}–{fmt(tot[1])} | {fmt(30 * tot[0], 0)}–{fmt(30 * tot[1], 0)} | '
              f'{fmt(util / tot[1], 0)}–{fmt(util / tot[0], 0)} |')


if __name__ == '__main__':
    main()
