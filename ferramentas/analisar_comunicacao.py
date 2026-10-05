"""Análise dos testes de bancada da Etapa 4 (ESP-NOW nó → coordenador).

Lê as capturas de ferramentas/capturar_seriais.py (<prefixo>_coord.txt e
<prefixo>_no1.txt), cruza as linhas [ENVIO] do nó com as [CSV] do coordenador
pelo par (boot, seq) e imprime um resumo por teste em Markdown.

Uso:
  .venv/bin/python ferramentas/analisar_comunicacao.py dados/etapa4/brutos/t1_entrega [outros prefixos...]
  .venv/bin/python ferramentas/analisar_comunicacao.py --figura dados/etapa4/brutos/t1_entrega dados/etapa4/brutos/t2_coord_desligado

Formatos das linhas: docs/comunicacao.md (seção Instrumentação).
"""
import argparse
import re
import statistics
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
FIGURAS = RAIZ / "docs" / "figuras"

CAMPOS_ENVIO = ["boot", "seq", "ack", "tentativas", "ligar_us", "t1_us", "t2_us", "t3_us", "envio_us", "erro",
                "tent_ant", "flags", "leitura_us", "ciclo_us"]
CAMPOS_CSV = ["t_ms", "mac", "nome", "rssi", "ruido", "len", "validacao", "classe", "perdidos", "boot", "seq",
              "temp_c100", "ur_c100", "solo_mv", "alim_mv", "estados", "motivo_boot", "acordado_ant_ms",
              "tent_ant", "flags"]
LINHA = re.compile(r"^\s*([\d.]+) (.*)$")


def numero(texto):
    try:
        return int(texto)
    except ValueError:
        return texto


def ler(arquivo, prefixo, campos):
    """Linhas `prefixo` (sem o cabeçalho) como dicionários, com o instante do PC em 't_pc'."""
    linhas = []
    if not arquivo.exists():
        return linhas
    for bruta in arquivo.read_text(encoding="utf-8").splitlines():
        m = LINHA.match(bruta)
        if not m or not m.group(2).startswith(prefixo + " "):
            continue
        valores = m.group(2)[len(prefixo) + 1:].split(",")
        if valores[0] == campos[0] or len(valores) != len(campos):  # cabeçalho ou linha truncada
            continue
        d = {c: numero(v) for c, v in zip(campos, valores)}
        d["t_pc"] = float(m.group(1))
        linhas.append(d)
    return linhas


def instante(arquivo, padrao):
    """Instantes do PC das linhas que contêm `padrao`."""
    if not arquivo.exists():
        return []
    return [float(m.group(1)) for b in arquivo.read_text(encoding="utf-8").splitlines()
            if (m := LINHA.match(b)) and padrao in m.group(2)]


def pct(valores, p):
    v = sorted(valores)
    if not v:
        return float("nan")
    k = (len(v) - 1) * p / 100
    i = int(k)
    return v[i] if i + 1 >= len(v) else v[i] + (v[i + 1] - v[i]) * (k - i)


def fmt(valores, escala=1.0, casas=1):
    if not valores:
        return "—"
    v = [x * escala for x in valores]
    return (f"mediana {statistics.median(v):.{casas}f} · p5 {pct(v, 5):.{casas}f} · p95 {pct(v, 95):.{casas}f}"
            f" · mín {min(v):.{casas}f} · máx {max(v):.{casas}f} (n={len(v)})")


def tempos_por_resultado(envios):
    """Separa os tempos envio→callback: tentativas com ACK, com FAIL e sem callback no prazo."""
    sucesso, falha, sem_callback = [], [], 0
    for e in envios:
        for k in range(1, e["tentativas"] + 1):
            t = e[f"t{k}_us"]
            if t == -1:
                sem_callback += 1
            elif isinstance(t, int):
                ultima_com_ack = e["ack"] == 1 and k == e["tentativas"]
                (sucesso if ultima_com_ack else falha).append(t)
    return sucesso, falha, sem_callback


def analisar(prefixo):
    prefixo = Path(prefixo)
    arq_no, arq_coord = Path(f"{prefixo}_no1.txt"), Path(f"{prefixo}_coord.txt")
    envios = ler(arq_no, "[ENVIO]", CAMPOS_ENVIO)
    pacotes = ler(arq_coord, "[CSV]", CAMPOS_CSV)
    pronto = instante(arq_coord, "[ESPNOW] pronto")
    rts = instante(arq_coord, "#RTS")
    print(f"\n## {prefixo.name}\n")

    # ------------------------------------------------------------ nó
    if envios:
        acks = [e for e in envios if e["ack"] == 1]
        tentativas = {k: sum(1 for e in envios if e["tentativas"] == k) for k in (1, 2, 3)}
        sucesso, falha, sem_cb = tempos_por_resultado(envios)
        erros = {}
        for e in envios:
            if e["erro"]:
                erros[e["erro"]] = erros.get(e["erro"], 0) + 1
        primeiros = [e["ligar_us"] for i, e in enumerate(envios) if i == 0 or e["boot"] != envios[i - 1]["boot"]]
        demais = [e["ligar_us"] for i, e in enumerate(envios) if i > 0 and e["boot"] == envios[i - 1]["boot"]]
        print("**Nó (linhas [ENVIO])**\n")
        print("| Grandeza | Valor |\n|---|---|")
        print(f"| Ciclos (pacotes montados) | {len(envios)} |")
        print(f"| Com ACK da camada MAC | {len(acks)} ({100 * len(acks) / len(envios):.1f} %) |")
        print(f"| Tentativas por pacote (1/2/3) | {tentativas[1]} / {tentativas[2]} / {tentativas[3]} |")
        print(f"| Envio→callback, tentativa com ACK (ms) | {fmt(sucesso, 1e-3, 2)} |")
        print(f"| Envio→callback, tentativa com FAIL (ms) | {fmt(falha, 1e-3, 1)} |")
        print(f"| Tentativas sem callback no prazo | {sem_cb} |")
        print(f"| Erros de esp_now_send | {erros or 'nenhum'} |")
        print(f"| Ligar Wi-Fi + ESP-NOW, 1º ciclo do boot (ms) | {fmt(primeiros, 1e-3, 1)} |")
        print(f"| Ligar Wi-Fi + ESP-NOW, demais ciclos (ms) | {fmt(demais, 1e-3, 1)} |")
        print(f"| Tempo total de envio (ms) | {fmt([e['envio_us'] for e in envios], 1e-3, 1)} |")
        print(f"| Ciclo acordado (ms) | {fmt([e['ciclo_us'] for e in envios], 1e-3, 1)} |")
        print(f"| Boots do nó na captura | {sorted({e['boot'] for e in envios})} |")
        print()

    # ------------------------------------------------------------ coordenador
    if pacotes:
        classes = {}
        for p in pacotes:
            chave = p["classe"] or f"rejeitado:{p['validacao']}"
            classes[chave] = classes.get(chave, 0) + 1
        validos = [p for p in pacotes if p["validacao"] == "nenhuma"]
        rssi = [p["rssi"] for p in pacotes]
        snr = [p["rssi"] - p["ruido"] for p in pacotes]
        aceitos = sum(1 for p in validos if p["classe"] in ("primeiro", "novo", "reinicio"))
        perdidos = sum(p["perdidos"] for p in validos if isinstance(p["perdidos"], int))
        print("**Coordenador (linhas [CSV])**\n")
        print("| Grandeza | Valor |\n|---|---|")
        print(f"| Quadros recebidos | {len(pacotes)} |")
        print(f"| Classes | {classes} |")
        print(f"| Perdidos (lacunas de seq) | {perdidos} |")
        if aceitos + perdidos:
            print(f"| Taxa de entrega (aceitos / (aceitos + perdidos)) | {100 * aceitos / (aceitos + perdidos):.2f} % |")
        print(f"| RSSI (dBm) | {fmt(rssi, 1, 0)} |")
        print(f"| Ruído (dBm) | {fmt([p['ruido'] for p in pacotes], 1, 0)} |")
        print(f"| SNR = RSSI − ruído (dB) | {fmt(snr, 1, 0)} |")
        print(f"| tent_ant > 1 ou ANTERIOR_SEM_ACK recebidos | "
              f"{sum(1 for p in validos if p['tent_ant'] > 1 or p['flags'] & 2)} |")
        print()

    # ------------------------------------------------------------ cruzamento
    if envios and pacotes:
        inicio = pronto[0] if pronto else 0.0
        recebidos = {(p["boot"], p["seq"]) for p in pacotes if p["validacao"] == "nenhuma"}
        # Pacotes do nó enquanto o coordenador estava no ar (depois de "pronto" e fora da janela de reset).
        def no_ar(t):
            if t < inicio:
                return False
            if len(rts) >= 2 and rts[0] <= t <= rts[1] + 6:  # +6 s: boot e varredura da COORD
                return False
            return True
        janela = [e for e in envios if no_ar(e["t_pc"])]
        chegaram = [e for e in janela if (e["boot"], e["seq"]) in recebidos]
        ack_sem_chegada = [e for e in janela if e["ack"] == 1 and (e["boot"], e["seq"]) not in recebidos]
        chegou_sem_ack = [e for e in janela if e["ack"] == 0 and (e["boot"], e["seq"]) in recebidos]
        print("**Cruzamento nó × coordenador (pacotes com o coordenador no ar)**\n")
        print("| Grandeza | Valor |\n|---|---|")
        print(f"| Pacotes enviados na janela | {len(janela)} |")
        if janela:
            print(f"| Chegaram à aplicação do coordenador | {len(chegaram)} ({100 * len(chegaram) / len(janela):.2f} %) |")
        print(f"| ACK no nó, mas ausente no coordenador | {len(ack_sem_chegada)} |")
        print(f"| Sem ACK no nó, mas recebido no coordenador | {len(chegou_sem_ack)} |")
        print()
    return envios, pacotes


def figura(prefixos):
    """Figura da bancada: RSSI, envio→ACK e envio→FAIL (um painel por grandeza)."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    envios, pacotes = [], []
    for p in prefixos:
        e, c = analisar(p)
        envios += e
        pacotes += c
    sucesso, falha, _ = tempos_por_resultado(envios)
    azul, laranja = "#2a78d6", "#eb6834"  # paleta de referência (dataviz), slots 1 e 2

    fig, eixos = plt.subplots(1, 3, figsize=(13, 3.8))
    paineis = [
        (eixos[0], [p["rssi"] for p in pacotes], azul, "RSSI no coordenador (dBm)", 1, None),
        (eixos[1], [t / 1000 for t in sucesso], azul, "Envio → callback com ACK (ms)", 0.1, 10),
        (eixos[2], [t / 1000 for t in falha], laranja, "Envio → callback com FAIL (ms)", 1, None),
    ]
    for eixo, todos, cor, titulo, largura, limite in paineis:
        valores = [v for v in todos if limite is None or v <= limite]
        if len(valores) < len(todos):
            titulo += f" [{len(todos) - len(valores)} > {limite} omitido]"
        if valores:
            inicio = min(valores) - largura / 2
            caixas = [inicio + i * largura for i in range(int((max(valores) - inicio) / largura) + 2)]
            eixo.hist(valores, bins=caixas, color=cor, edgecolor="white", linewidth=1)
            med = statistics.median(todos)
            eixo.axvline(med, color="#444444", ls="--", lw=1)
            eixo.annotate(f"mediana {med:.1f}", xy=(med, eixo.get_ylim()[1] * 0.92), xytext=(4, 0),
                          textcoords="offset points", fontsize=8, color="#444444")
        eixo.set_title(f"{titulo}\n(n={len(todos)})", fontsize=10)
        eixo.set_ylabel("ocorrências")
        eixo.grid(axis="y", color="#e5e5e5", lw=0.8)
        eixo.set_axisbelow(True)
        for lado in ("top", "right"):
            eixo.spines[lado].set_visible(False)
    fig.suptitle("ESP-NOW na bancada (nó e coordenador na mesma protoboard, canal 1, 1 Mbps)", fontsize=11)
    fig.tight_layout()
    FIGURAS.mkdir(parents=True, exist_ok=True)
    destino = FIGURAS / "espnow_bancada.png"
    fig.savefig(destino, dpi=150)
    print(f"figura: {destino.relative_to(RAIZ)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("prefixos", nargs="+")
    ap.add_argument("--figura", action="store_true", help="gera docs/figuras/espnow_bancada.png com as capturas dadas")
    args = ap.parse_args()
    if args.figura:
        figura(args.prefixos)
    else:
        for p in args.prefixos:
            analisar(p)


if __name__ == "__main__":
    main()
