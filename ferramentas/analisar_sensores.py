"""Análise dos dados da Etapa 2 (sensores do nó) e geração das figuras.

Uso (na raiz do projeto, com o .venv ativo):
    python ferramentas/analisar_sensores.py [--dados dados/etapa2/brutos] [--figuras docs/figuras]

Lê os CSVs gravados pela serial e:
  1. calcula média, desvio-padrão, mínimo e máximo de cada grandeza do teste
     de estabilidade (12_estabilidade_10min.csv) e conta os estados;
  2. calcula os pontos de calibração do solo a partir das capturas brutas;
  3. gera as figuras: calibração do solo, estabilidade, acomodação após
     inserir/regar, estabilização após ligar e dependência da alimentação.
"""

import argparse
import csv
import statistics as st
from collections import Counter
from pathlib import Path

import matplotlib

matplotlib.use("Agg")  # só gera arquivos, sem janela
import matplotlib.pyplot as plt

# Calibração de bancada do firmware (no_sensor/src/config.h).
SOLO_MV_SECO = 1400.0
SOLO_MV_UMIDO = 655.0

# Pontos de calibração: (rótulo, arquivo, início [s], fim [s]) da janela usada
# e deslocamento do rótulo na figura (pontos tipográficos).
PONTOS_SOLO = [
    ("Ar", "1_ar.csv", 30, 90, (-40, 12)),
    ("Solo seco ao toque\n(acomodado 14 h)", "7_solo_seco_acomodado.csv", 30, 600, (10, 6)),
    ("Água", "4_agua.csv", 30, 240, (10, -22)),
    ("Solo ~1 h após a rega", "9_pos_rega_divisor.csv", 2543, 2608, (10, 6)),
]

GRANDEZAS = [
    ("temp_c", "temp_estado", "Temperatura do ar", "°C"),
    ("ur_pct", "ur_estado", "Umidade do ar", "%"),
    ("solo_mv", "solo_mv_estado", "Solo (tensão)", "mV"),
    ("solo_pct", "solo_pct_estado", "Solo (índice)", "%"),
    ("alim_mv", "alim_estado", "Alimentação", "mV"),
]


def ler_csv(caminho):
    with open(caminho, newline="") as f:
        return list(csv.DictReader(f))


def janela(linhas, coluna, inicio, fim, col_tempo="t_s"):
    return [float(x[coluna]) for x in linhas if inicio <= float(x[col_tempo]) < fim]


def resumo(valores):
    return {
        "n": len(valores),
        "media": st.mean(valores),
        "desvio": st.pstdev(valores),
        "min": min(valores),
        "max": max(valores),
    }


def analisar_estabilidade(dados, figuras):
    linhas = ler_csv(dados / "12_estabilidade_10min.csv")
    duracao_min = (int(linhas[-1]["t_ms"]) - int(linhas[0]["t_ms"])) / 60000
    print(f"\n## Teste de estabilidade: {len(linhas)} leituras, {duracao_min:.1f} min\n")
    print("| Grandeza | n | Média | Desvio | Mín | Máx | Estados |")
    print("|---|---|---|---|---|---|---|")
    for col, col_estado, nome, unidade in GRANDEZAS:
        validos = [float(x[col]) for x in linhas if x[col_estado] != "erro"]
        estados = Counter(x[col_estado] for x in linhas)
        r = resumo(validos)
        txt_estados = ", ".join(f"{k}: {v}" for k, v in sorted(estados.items()))
        print(f"| {nome} ({unidade}) | {r['n']} | {r['media']:.2f} | {r['desvio']:.2f} | "
              f"{r['min']:.2f} | {r['max']:.2f} | {txt_estados} |")
    extras = Counter(x["aht20_extras"] for x in linhas)
    leitura = resumo([int(x["leitura_us"]) / 1000 for x in linhas])
    print(f"\nTentativas extras do AHT20: {dict(extras)}")
    print(f"Tempo de leitura das 4 grandezas: média {leitura['media']:.1f} ms "
          f"(mín {leitura['min']:.1f}, máx {leitura['max']:.1f})")

    t = [(int(x["t_ms"]) - int(linhas[0]["t_ms"])) / 60000 for x in linhas]
    fig, eixos = plt.subplots(4, 1, figsize=(8, 9), sharex=True)
    for eixo, (col, _, nome, unidade) in zip(eixos, [GRANDEZAS[i] for i in (0, 1, 2, 4)]):
        eixo.plot(t, [float(x[col]) for x in linhas], lw=0.8)
        eixo.set_ylabel(f"{nome}\n({unidade})")
        eixo.grid(alpha=0.3)
    eixos[-1].set_xlabel("Tempo (min)")
    fig.suptitle("Teste de estabilidade do nó (leitura a cada 2 s)")
    fig.tight_layout()
    fig.savefig(figuras / "estabilidade_10min.png", dpi=150)
    plt.close(fig)


def analisar_calibracao(dados, figuras):
    print("\n## Pontos de calibração do solo\n")
    print("| Ponto | Janela | n | Média (mV) | Desvio (mV) |")
    print("|---|---|---|---|---|")
    pontos = []
    for rotulo, arquivo, ini, fim, deslocamento in PONTOS_SOLO:
        r = resumo(janela(ler_csv(dados / arquivo), "mv_media", ini, fim))
        pontos.append((rotulo, r, deslocamento))
        print(f"| {rotulo.replace(chr(10), ' ')} | {arquivo} [{ini}–{fim} s] | {r['n']} | "
              f"{r['media']:.1f} | {r['desvio']:.2f} |")

    fig, eixo = plt.subplots(figsize=(8, 5))
    mv = [600, 2300]
    pct = [(SOLO_MV_SECO - v) / (SOLO_MV_SECO - SOLO_MV_UMIDO) * 100 for v in mv]
    eixo.plot(mv, pct, "--", color="gray", lw=1, label="Reta de calibração (extrapolada)")
    eixo.plot([SOLO_MV_UMIDO, SOLO_MV_SECO], [100, 0], "-", color="C0", lw=2,
              label="Faixa calibrada (0–100 %)")
    eixo.axhspan(0, 100, color="C0", alpha=0.06)
    for rotulo, r, deslocamento in pontos:
        y = (SOLO_MV_SECO - r["media"]) / (SOLO_MV_SECO - SOLO_MV_UMIDO) * 100
        eixo.errorbar(r["media"], y, xerr=r["desvio"], fmt="o", color="C3", capsize=3)
        eixo.annotate(f"{rotulo}\n{r['media']:.0f} mV", (r["media"], y), textcoords="offset points",
                      xytext=deslocamento, fontsize=8)
    eixo.set_xlabel("Tensão de saída do sensor (mV)")
    eixo.set_ylabel("Índice de umidade do solo (%)")
    eixo.set_title("Calibração de bancada do sensor capacitivo V1.2 (alimentação 3V3, ~3,38 V)")
    eixo.grid(alpha=0.3)
    eixo.legend(loc="upper right", fontsize=8)
    fig.tight_layout()
    fig.savefig(figuras / "calibracao_solo.png", dpi=150)
    plt.close(fig)


def figura_acomodacao(dados, figuras):
    fig, eixos = plt.subplots(1, 2, figsize=(11, 4))
    ins = ler_csv(dados / "5_solo_vaso_seco_60min.csv")
    eixos[0].plot([float(x["t_s"]) / 60 for x in ins], [float(x["mv_media"]) for x in ins], lw=0.8)
    eixos[0].set_title("Após inserir o sensor na terra")
    rega = ler_csv(dados / "8_rega_2h.csv")
    eixos[1].plot([float(x["t_s"]) / 60 for x in rega], [float(x["mv_media"]) for x in rega], lw=0.8)
    eixos[1].set_title("Antes e depois da rega")
    eixos[1].annotate("rega", (1.8, 600), xytext=(4, 1100), fontsize=8,
                      arrowprops={"arrowstyle": "->", "color": "gray"})
    eixos[1].annotate("picos: mão perto do sensor\n(montagem na protoboard)", (28.6, 2300),
                      xytext=(8, 1900), fontsize=8, arrowprops={"arrowstyle": "->", "color": "gray"})
    for eixo in eixos:
        eixo.set_xlabel("Tempo (min)")
        eixo.set_ylabel("Saída do sensor (mV)")
        eixo.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(figuras / "acomodacao_solo.png", dpi=150)
    plt.close(fig)


def figura_ligar_sensor(dados, figuras):
    linhas = ler_csv(dados / "10_estabilizacao_gpio.csv")
    fig, eixo = plt.subplots(figsize=(8, 4))
    for ciclo in sorted({x["ciclo"] for x in linhas}):
        pts = [x for x in linhas if x["ciclo"] == ciclo]
        eixo.plot([int(x["t_ms"]) for x in pts], [float(x["mv"]) for x in pts], lw=0.8,
                  label=f"ciclo {ciclo}")
    eixo.axvline(500, color="gray", ls="--", lw=1)
    eixo.annotate("espera recomendada: 500 ms", (500, 200), xytext=(510, 200), fontsize=8)
    eixo.set_xlabel("Tempo após ligar o sensor (ms)")
    eixo.set_ylabel("Saída do sensor (mV, média de 5 ms)")
    eixo.set_title("Estabilização da saída após ligar o sensor (alimentado por GPIO)")
    eixo.grid(alpha=0.3)
    eixo.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(figuras / "estabilizacao_ligar_solo.png", dpi=150)
    plt.close(fig)


def figura_alimentacao(dados, figuras):
    linhas = sorted(ler_csv(dados / "13_alimentacao_sensor_solo.csv"),
                    key=lambda x: float(x["alimentacao_mv"]))
    fig, eixo = plt.subplots(figsize=(7, 4))
    x = [float(l["alimentacao_mv"]) / 1000 for l in linhas]
    y = [float(l["solo_mv"]) for l in linhas]
    eixo.plot(x, y, "o-")
    for l, xi, yi in zip(linhas, x, y):
        eixo.annotate(l["condicao"], (xi, yi), textcoords="offset points", xytext=(5, -12), fontsize=7)
    eixo.set_xlabel("Tensão de alimentação do sensor (V)")
    eixo.set_ylabel("Saída do sensor no mesmo solo (mV)")
    eixo.set_title("Dependência da saída em relação à alimentação")
    eixo.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(figuras / "alimentacao_solo.png", dpi=150)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dados", type=Path, default=Path("dados/etapa2/brutos"))
    parser.add_argument("--figuras", type=Path, default=Path("docs/figuras"))
    args = parser.parse_args()
    args.figuras.mkdir(parents=True, exist_ok=True)

    analisar_estabilidade(args.dados, args.figuras)
    analisar_calibracao(args.dados, args.figuras)
    figura_acomodacao(args.dados, args.figuras)
    figura_ligar_sensor(args.dados, args.figuras)
    figura_alimentacao(args.dados, args.figuras)
    print(f"\nFiguras salvas em {args.figuras}/")


if __name__ == "__main__":
    main()
