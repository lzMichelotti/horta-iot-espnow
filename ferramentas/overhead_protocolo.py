"""Análise de overhead do pacote (Etapa 3): binário × JSON no ESP-NOW.

Uso (na raiz do projeto, com o .venv ativo):
    python ferramentas/overhead_protocolo.py [--figuras docs/figuras]

Calcula, para o mesmo conteúdo em três formatos (binário de 22 bytes, JSON
compacto e JSON legível), o tamanho do quadro ESP-NOW completo, o tempo no ar
a 1 Mbps e a carga elétrica gasta pelo rádio para transmiti-lo. Gera a figura
do pacote byte a byte e a do comparativo.

Fontes dos números (detalhes em docs/protocolo.md):
- Quadro ESP-NOW v1.0: ESP-IDF v5.5, "ESP-NOW", Frame Format.
- 1 Mbps com preâmbulo longo: WIFI_PHY_RATE_1M_L (esp_wifi_types_generic.h).
- Preâmbulo + cabeçalho PLCP DSSS = 192 µs; ACK de 14 bytes; SIFS = 10 µs:
  IEEE Std 802.11 (camada física DSSS). Não constam da documentação da Espressif.
- Corrente de transmissão 802.11b 1 Mbps a +19,5 dBm = 240 mA (típica):
  ESP32 Series Datasheet v5.3, tabela 5-4.
"""

import argparse
import json
import struct
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ---------------------------------------------------------------- Constantes
TAXA_BPS = 1_000_000
PREAMBULO_PLCP_US = 192          # 144 bits de preâmbulo longo + 48 bits de cabeçalho PLCP
CAB_MAC = 24
CAB_ACAO = 1 + 3 + 4             # categoria, OUI, valores aleatórios
CAB_ELEMENTO = 7                 # ID, comprimento, OUI, tipo, versão
FCS = 4
OVERHEAD_QUADRO = CAB_MAC + CAB_ACAO + CAB_ELEMENTO + FCS  # 43 bytes
ACK_BYTES = 14
SIFS_US = 10
CORRENTE_TX_MA = 240
CARGA_MAX_ESPNOW = 250

# Pacote real capturado do NÓ 1 (05/10/2026), conferido byte a byte em docs/protocolo.md.
PACOTE_REAL = bytes.fromhex("01 01 04 00 00 00 00 00 26 08 C5 17 F9 03 35 0D 00 01 00 00 00 00")

# Layout do PacoteLeitura (protocolo.h): nome, formato struct, rótulo da figura.
CAMPOS = [
    ("versao", "B", "versão"), ("tipo", "B", "tipo"), ("boot", "H", "boot"), ("seq", "I", "seq"),
    ("temperatura_c100", "h", "temperatura\n×0,01 °C"), ("umidade_ar_c100", "H", "umidade ar\n×0,01 %"),
    ("solo_mv", "H", "solo\nmV"), ("alimentacao_mv", "H", "alimentação\nmV"), ("estados", "B", "estados"),
    ("motivo_boot", "B", "motivo\nboot"), ("acordado_ant_ms", "H", "acordado\nant. ms"),
    ("tentativas_ant", "B", "tent.\nant."), ("flags", "B", "flags"),
]
FORMATO = "<" + "".join(f for _, f, _ in CAMPOS)  # "<" = little-endian, sem padding
assert struct.calcsize(FORMATO) == 22


def decodificar(pacote):
    return dict(zip([n for n, _, _ in CAMPOS], struct.unpack(FORMATO, pacote)))


def estados_texto(estados):
    nomes = ["ok", "erro", "fora_de_faixa", "reservado"]
    return {g: nomes[(estados >> (2 * i)) & 3] for i, g in enumerate(["temperatura", "umidade_ar", "solo", "alimentacao"])}


def json_legivel(c):
    return json.dumps({
        "versao": c["versao"], "tipo": "leitura", "boot": c["boot"], "seq": c["seq"],
        "temperatura_c": c["temperatura_c100"] / 100, "umidade_ar_pct": c["umidade_ar_c100"] / 100,
        "solo_mv": c["solo_mv"], "alimentacao_mv": c["alimentacao_mv"], "estados": estados_texto(c["estados"]),
        "motivo_boot": c["motivo_boot"], "acordado_ant_ms": c["acordado_ant_ms"],
        "tentativas_ant": c["tentativas_ant"], "flags": c["flags"],
    }, ensure_ascii=False, separators=(",", ":"))


def json_compacto(c):
    return json.dumps({
        "v": c["versao"], "t": c["tipo"], "b": c["boot"], "s": c["seq"], "T": c["temperatura_c100"] / 100,
        "U": c["umidade_ar_c100"] / 100, "S": c["solo_mv"], "A": c["alimentacao_mv"], "e": c["estados"],
        "m": c["motivo_boot"], "w": c["acordado_ant_ms"], "r": c["tentativas_ant"], "f": c["flags"],
    }, separators=(",", ":"))


def tempo_no_ar_us(carga):
    return PREAMBULO_PLCP_US + (OVERHEAD_QUADRO + carga) * 8 * 1_000_000 / TAXA_BPS


def linha(nome, carga):
    quadro = OVERHEAD_QUADRO + carga
    t = tempo_no_ar_us(carga)
    carga_uc = CORRENTE_TX_MA * t / 1000  # mA × µs = nC; /1000 = µC
    return {"formato": nome, "carga": carga, "quadro": quadro, "tempo_us": t, "carga_uc": carga_uc,
            "eficiencia": carga / quadro}


def figura_pacote(figuras):
    cores = ["#c9d6ea", "#c9d6ea", "#f4d6a0", "#f4d6a0", "#b8dfc0", "#b8dfc0", "#b8dfc0", "#b8dfc0",
             "#e8c0c0", "#ddd", "#ddd", "#ddd", "#ddd"]
    fig, eixo = plt.subplots(figsize=(13, 2.6))
    offset = 0
    for (nome, fmt, rotulo), cor in zip(CAMPOS, cores):
        tam = struct.calcsize("<" + fmt)
        eixo.add_patch(plt.Rectangle((offset, 0), tam, 1, facecolor=cor, edgecolor="black"))
        eixo.text(offset + tam / 2, 0.5, rotulo, ha="center", va="center", fontsize=7)
        for i in range(tam):
            eixo.text(offset + i + 0.5, 1.12, f"{PACOTE_REAL[offset + i]:02X}", ha="center", fontsize=7,
                      family="monospace")
        eixo.text(offset, -0.18, str(offset), ha="center", fontsize=7, color="gray")
        offset += tam
    eixo.text(offset, -0.18, str(offset), ha="center", fontsize=7, color="gray")
    eixo.set_xlim(-0.2, offset + 0.2)
    eixo.set_ylim(-0.35, 1.45)
    eixo.axis("off")
    eixo.set_title("PacoteLeitura v1: 22 bytes, little-endian (bytes de um pacote real do NÓ 1 acima; offsets abaixo)",
                   fontsize=9)
    legendas = [("cabeçalho do protocolo", "#c9d6ea"), ("sequência e reinício", "#f4d6a0"),
                ("leituras", "#b8dfc0"), ("estados (2 bits × 4)", "#e8c0c0"), ("instrumentação", "#ddd")]
    eixo.legend(handles=[plt.Rectangle((0, 0), 1, 1, facecolor=c, edgecolor="black") for _, c in legendas],
                labels=[n for n, _ in legendas], loc="upper center", bbox_to_anchor=(0.5, -0.05), ncol=5,
                fontsize=7, frameon=False)
    fig.tight_layout()
    fig.savefig(figuras / "pacote_bytes.png", dpi=150)
    plt.close(fig)


def figura_comparativo(linhas, figuras):
    fig, eixo = plt.subplots(figsize=(9, 4))
    nomes = [l["formato"] for l in linhas]
    partes = [
        ("preâmbulo + PLCP (192 µs)", [PREAMBULO_PLCP_US] * len(linhas), "#bbbbbb"),
        ("cabeçalhos MAC + ESP-NOW (39 B)", [(CAB_MAC + CAB_ACAO + CAB_ELEMENTO) * 8] * len(linhas), "#8fa8c8"),
        ("carga útil", [l["carga"] * 8 for l in linhas], "#5aa469"),
        ("FCS (4 B)", [FCS * 8] * len(linhas), "#d9a05b"),
    ]
    esquerda = [0] * len(linhas)
    for rotulo, valores, cor in partes:
        eixo.barh(nomes, valores, left=esquerda, color=cor, label=rotulo)
        esquerda = [e + v for e, v in zip(esquerda, valores)]
    for i, l in enumerate(linhas):
        aviso = "\nnão cabe no ESP-NOW v1 (> 250 B)" if l["carga"] > CARGA_MAX_ESPNOW else ""
        eixo.text(esquerda[i] + 15, i, f"{l['tempo_us']:.0f} µs · {l['quadro']} B no quadro{aviso}", va="center",
                  fontsize=8)
    eixo.set_xlabel("Tempo no ar do quadro de dados a 1 Mbps (µs)")
    eixo.set_xlim(0, max(esquerda) * 1.45)
    eixo.invert_yaxis()
    eixo.legend(fontsize=7, loc="upper center", bbox_to_anchor=(0.5, -0.22), ncol=4, frameon=False)
    eixo.set_title("Mesmo conteúdo, três formatos: tempo de transmissão no ESP-NOW")
    fig.tight_layout()
    fig.savefig(figuras / "binario_vs_json.png", dpi=150)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--figuras", type=Path, default=Path("docs/figuras"))
    args = parser.parse_args()
    args.figuras.mkdir(parents=True, exist_ok=True)

    c = decodificar(PACOTE_REAL)
    jc, jl = json_compacto(c), json_legivel(c)
    pior = dict(versao=1, tipo=1, boot=65535, seq=4294967295, temperatura_c100=-4000, umidade_ar_c100=10000,
                solo_mv=3300, alimentacao_mv=6000, estados=0b10101010, motivo_boot=15, acordado_ant_ms=65535,
                tentativas_ant=255, flags=255)
    print("JSON compacto:", jc)
    print("JSON legível: ", jl)
    print(f"\nQuadro ESP-NOW = {OVERHEAD_QUADRO} B fixos + carga útil; tempo = {PREAMBULO_PLCP_US} µs + 8 µs/byte\n")
    print("| Formato | Carga útil (B) | Quadro (B) | Tempo no ar (µs) | Carga do rádio (µC) | Carga útil / quadro "
          "| Cabe no ESP-NOW v1 (≤ 250 B)? |")
    print("|---|---|---|---|---|---|---|")
    linhas = [linha("Binário (struct)", len(PACOTE_REAL)), linha("JSON compacto", len(jc.encode())),
              linha("JSON legível", len(jl.encode()))]
    for l in linhas + [linha("JSON compacto (pior caso)", len(json_compacto(pior).encode())),
                       linha("JSON legível (pior caso)", len(json_legivel(pior).encode()))]:
        cabe = "sim" if l["carga"] <= CARGA_MAX_ESPNOW else "**não**"
        print(f"| {l['formato']} | {l['carga']} | {l['quadro']} | {l['tempo_us']:.0f} | {l['carga_uc']:.0f} | "
              f"{l['eficiencia'] * 100:.0f} % | {cabe} |")
    ack_us = PREAMBULO_PLCP_US + ACK_BYTES * 8
    print(f"\nACK: {ack_us} µs; SIFS: {SIFS_US} µs. Troca dados + ACK (binário): "
          f"{linhas[0]['tempo_us'] + SIFS_US + ack_us:.0f} µs (sem DIFS/backoff)")

    figura_pacote(args.figuras)
    figura_comparativo(linhas, args.figuras)
    print(f"\nFiguras: {args.figuras}/pacote_bytes.png e {args.figuras}/binario_vs_json.png")


if __name__ == "__main__":
    main()
