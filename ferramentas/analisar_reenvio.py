"""Análise dos testes de bancada da Etapa 7.1 (confirmação e reenvio).

Lê as capturas de ferramentas/capturar_seriais.py (<prefixo>_coord.txt e
<prefixo>_no1.txt). As colunas de [ENVIO], [FASES] e [CSV] vêm das linhas de
cabeçalho que as placas imprimem no boot, então serve para o firmware antigo
(antes da etapa) e para o novo.

Confere, para o nó:
  - entrega: cada (boot, seq) gerado pelo nó gravado UMA vez no coordenador
    (linha [CSV] aceita seguida de "[FS] gravado"), em ordem, sem buracos;
  - hora: (instante do PC da recepção − idade_s) contra o instante do PC da
    linha [ENVIO] do ciclo em que a leitura foi feita (o relógio do PC é a
    referência; não depende da hora do coordenador);
  - energia: tempo acordado e de rádio por ciclo, normal × com reenvio.

Uso:
  .venv/bin/python ferramentas/analisar_reenvio.py <prefixo> [<prefixo> ...]
"""
import re
import statistics
import sys
from pathlib import Path

LINHA = re.compile(r"^\s*([\d.]+) (\[[A-Z]+\]) (.*)$")
ACEITAS = {"primeiro", "novo", "reinicio"}


def numero(t):
    try:
        return int(t)
    except ValueError:
        return t


# Colunas conhecidas (do código dos firmwares), para quando o cabeçalho do boot
# chega truncado pela serial: escolhe a lista com o mesmo número de campos.
_ENVIO_V2 = ("boot,seq,ack,tentativas,ligar_us,t1_us,t2_us,t3_us,envio_us,erro,tent_ant,flags,leitura_us,"
             "ciclo_us,acordado_ant_ms").split(",")
_CSV_V2 = ("t_ms,mac,nome,rssi,ruido,len,validacao,classe,perdidos,boot,seq,temp_c100,ur_c100,solo_mv,alim_mv,"
           "estados,motivo_boot,acordado_ant_ms,tent_ant,flags").split(",")
CONHECIDOS = {
    "[ENVIO]": [_ENVIO_V2, _ENVIO_V2 + ("conf,resp_us,conf_recusadas,fila_antes,liberadas,reenviadas,"
                                        "reenviadas_ack,fila_depois,descartadas").split(",")],
    "[FASES]": [("setup_us,serial_nvs_us,sens_iniciar_us,leitura_us,ligar_us,envio_us,desligar_us,impressao_us,"
                 "flush_ant_us,fim_us,light_sleep_us,light_sleeps,modo").split(",")],
    "[CSV]": [_CSV_V2, _CSV_V2 + ["idade_s", "conf"]],
}


def ler(arquivo):
    """Lista de (t_pc, prefixo, dict) usando o último cabeçalho visto de cada prefixo.
    Linhas que não são registros viram dict {'texto': ...}."""
    cabecalhos, saida = {}, []
    for bruta in Path(arquivo).read_text(encoding="utf-8", errors="replace").splitlines():
        m = LINHA.match(bruta)
        if not m:
            continue
        t, pref, resto = float(m.group(1)), m.group(2), m.group(3)
        valores = resto.split(",")
        if pref in ("[ENVIO]", "[FASES]", "[CSV]", "[RESUMO]") and valores[0] in ("boot", "setup_us", "t_ms"):
            cabecalhos[pref] = valores
            continue
        campos = cabecalhos.get(pref)
        if not campos or len(campos) != len(valores):
            campos = next((c for c in CONHECIDOS.get(pref, []) if len(c) == len(valores)), None)
        if campos and len(valores) == len(campos):
            saida.append((t, pref, {c: numero(v) for c, v in zip(campos, valores)}))
        else:
            saida.append((t, pref, {"texto": resto}))
    return saida


def resumo(valores, unidade="", fmt="{:.1f}"):
    if not valores:
        return "—"
    v = sorted(valores)
    p95 = v[min(len(v) - 1, int(round(0.95 * (len(v) - 1))))]
    return (f"mediana {fmt.format(statistics.median(v))}{unidade}, p95 {fmt.format(p95)}{unidade}, "
            f"mín–máx {fmt.format(v[0])}–{fmt.format(v[-1])}{unidade} (n={len(v)})")


def analisar(prefixo):
    print(f"\n## {prefixo}\n")
    no = ler(f"{prefixo}_no1.txt") if Path(f"{prefixo}_no1.txt").exists() else []
    coord = ler(f"{prefixo}_coord.txt") if Path(f"{prefixo}_coord.txt").exists() else []

    # ------------------------------------------------------------ nó
    envios = [(t, d) for t, p, d in no if p == "[ENVIO]" and "seq" in d]
    fases = [d for t, p, d in no if p == "[FASES]" and "ligar_us" in d]
    gerado = {(d["boot"], d["seq"]): t for t, d in envios}  # instante (PC) do ciclo em que foi lida
    novo_fw = envios and "fila_antes" in envios[0][1]
    print(f"- Nó: {len(envios)} ciclos, boots {sorted({d['boot'] for _, d in envios})}, "
          f"firmware {'7.1 (fila)' if novo_fw else 'anterior'}")
    if novo_fw:
        conf = [d for _, d in envios if d["conf"] != ""]
        recusadas = sum(d["conf_recusadas"] for _, d in envios)
        print(f"- Confirmações válidas: {len(conf)}/{len(envios)} ciclos; recusadas pelo nó: {recusadas}; "
              f"liberadas: {sum(d['liberadas'] for _, d in envios)}; "
              f"reenviadas: {sum(d['reenviadas'] for _, d in envios)} "
              f"({sum(d['reenviadas_ack'] for _, d in envios)} com ACK); "
              f"fila máx.: {max(d['fila_depois'] for _, d in envios)}; "
              f"descartadas (fila cheia): {max(d['descartadas'] for _, d in envios)}")
        resp = [d["resp_us"] / 1000 for _, d in envios if d["resp_us"] != ""]
        print(f"- Tempo de resposta (ACK MAC da nova → confirmação): {resumo(resp, ' ms', '{:.2f}')}")

    # ------------------------------------------------------------ energia (por ciclo, do [FASES])
    if fases and envios:
        # [FASES] vem logo depois do [ENVIO] do mesmo ciclo: parear pela ordem.
        pares = list(zip([d for _, d in envios], fases[-len(envios):] if len(fases) >= len(envios) else fases))
        pares = [(e, f) for e, f in pares if e["seq"] != 0]  # 1º ciclo do boot: calibração de RF (96 ms)
        def radio_ms(f):
            return (f["ligar_us"] + f["envio_us"] + f["desligar_us"]) / 1000
        grupos = {"normal (com ACK, sem reenvio)": [p for p in pares if p[0]["ack"] == 1 and
                                                    (not novo_fw or p[0]["reenviadas"] == 0)],
                  "com reenvio": [p for p in pares if novo_fw and p[0]["reenviadas"] > 0],
                  "coordenador fora (sem ACK)": [p for p in pares if p[0]["ack"] == 0]}
        for nome, g in grupos.items():
            if not g:
                continue
            print(f"- Energia, {nome}: acordado {resumo([f['fim_us'] / 1000 for _, f in g], ' ms')}; "
                  f"rádio {resumo([radio_ms(f) for _, f in g], ' ms', '{:.2f}')}; "
                  f"envio {resumo([f['envio_us'] / 1000 for _, f in g], ' ms', '{:.2f}')}")
            if nome == "com reenvio":
                print(f"  reenviadas por ciclo: {[e['reenviadas'] for e, _ in g]}")

    # ------------------------------------------------------------ coordenador
    if not coord:
        return
    linhas_csv, gravados = [], {}
    i = 0
    while i < len(coord):
        t, p, d = coord[i]
        if p == "[CSV]" and "classe" in d and d["mac"] == "88:57:21:70:93:70" and d["validacao"] == "nenhuma":
            gravou = False
            j = i + 1
            while j < len(coord) and coord[j][1] != "[CSV]":
                if coord[j][1] == "[FS]" and coord[j][2].get("texto", "").startswith("gravado"):
                    gravou = True
                    break
                j += 1
            linhas_csv.append((t, d, gravou))
            if gravou:
                gravados.setdefault((d["boot"], d["seq"]), []).append((t, d))
        i += 1
    reinicios = [t for t, p, d in coord if p == "[BOOT]" and "coordenador" in d.get("texto", "")]
    classes = {}
    for _, d, _ in linhas_csv:
        classes[d["classe"]] = classes.get(d["classe"], 0) + 1
    rejeitados = [d for t, p, d in coord if p == "[CSV]" and d.get("validacao") not in (None, "nenhuma")]
    print(f"- Coordenador: boots em t = {[round(t) for t in reinicios]} s; pacotes autênticos do NÓ 1: "
          f"{len(linhas_csv)} {classes}; rejeitados: {len(rejeitados)}")

    duplicados = {k: v for k, v in gravados.items() if len(v) > 1}
    aceitos_sem_gravar = [(d["boot"], d["seq"]) for _, d, g in linhas_csv if d["classe"] in ACEITAS and not g]
    ordem = [k for _, d, g in linhas_csv if g for k in [(d["boot"], d["seq"])]]
    fora = [ordem[k] for k in range(1, len(ordem)) if ordem[k] <= ordem[k - 1]]
    # Esperados: tudo o que o nó gerou a partir do primeiro gravado (antes disso, o coordenador ainda
    # não existia nesta captura) até o último ciclo cuja leitura teve tempo de ser confirmada.
    if gravados:
        primeiro = min(gravados)
        esperados = [k for k in sorted(gerado) if k >= primeiro]
        faltam = [k for k in esperados if k not in gravados]
        # Os últimos ainda podem estar na fila do nó quando a captura terminou.
        fila_final = envios[-1][1].get("fila_depois", 0) if novo_fw else 0
        print(f"- Gravados: {len(gravados)} pares distintos; duplicatas gravadas: {len(duplicados)}; "
              f"fora de ordem: {len(fora)}; aceitos e não gravados: {len(aceitos_sem_gravar)}")
        print(f"- Esperados desde {primeiro}: {len(esperados)}; faltam {len(faltam)} "
              f"(fila do nó no fim da captura: {fila_final}) {faltam[:20]}")

        # Hora: erro da hora calculada (recepção − idade) contra o instante em que o nó leu.
        erros, erros_reenvio = [], []
        for k, lista in gravados.items():
            if k not in gerado:
                continue
            t_rec, d = lista[0]
            idade = d.get("idade_s", 0) or 0
            e = (t_rec - idade) - gerado[k]
            (erros_reenvio if idade > 0 else erros).append((idade, e))
        if erros:
            print(f"- Hora, enviadas no próprio ciclo: erro {resumo([e for _, e in erros], ' s', '{:+.2f}')}")
        if erros_reenvio:
            print(f"- Hora, reenviadas: idade {resumo([i for i, _ in erros_reenvio], ' s', '{:.0f}')}; "
                  f"erro {resumo([e for _, e in erros_reenvio], ' s', '{:+.2f}')}")
            longas = [(i, e) for i, e in erros_reenvio if i >= 60]  # abaixo disso domina a resolução de 1 s
            print(f"  erro relativo à idade (idade ≥ 60 s): {resumo([100 * e / i for i, e in longas], ' %', '{:+.2f}')}")
            print(f"  sem o campo de idade, o erro seria a própria idade: até {max(i for i, _ in erros_reenvio)} s")


def main():
    for prefixo in sys.argv[1:]:
        analisar(prefixo)


if __name__ == "__main__":
    main()
