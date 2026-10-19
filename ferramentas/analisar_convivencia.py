"""Convivência ESP-NOW × HTTP no coordenador (Etapa 8, passo 5).

Lê uma captura de ferramentas/capturar_seriais.py (<prefixo>_coord.txt e
<prefixo>_no1.txt, mesmo relógio) feita enquanto ferramentas/testar_api.py
gerava carga HTTP em fases. A fase de cada instante sai das próprias linhas
[HTTP] da COORD (a rota que estava sendo pedida); sem requisição por 5 s,
"ocioso".

Para cada fase:
  - nó: ciclos, confirmação recebida, tempo de resposta (ACK MAC → confirmação),
    fila e reenvios;
  - COORD: tempo de atendimento por rota ([HTTP] ... us) e descartes da fila do
    ESP-NOW ("[ESPNOW] fila cheia").

Uso:
  .venv/bin/python ferramentas/analisar_convivencia.py dados/etapa8/brutos/p5_convivencia
"""
import re
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from analisar_reenvio import ler  # noqa: E402  (mesmo leitor de cabeçalhos)

HTTP = re.compile(r"^\s*([\d.]+) \[HTTP\] ([A-Z?]+),([^,]+),(\d+),(\d+),(\d+)$")
FILA = re.compile(r"^\s*([\d.]+) \[ESPNOW\] fila cheia: (\d+)")


def fase_da_rota(uri, nbytes=0):
    if "exportar.csv" in uri:
        return "csv"
    # O log [HTTP] tem o caminho sem a query: a página de 200 (~80 KB) e a de 50
    # (~20 KB) se separam pelo tamanho da resposta.
    if uri.endswith("/leituras"):
        return "pag200" if nbytes > 50000 else "pag50"
    if uri.endswith("/status"):
        return "status"
    return "outra"


def resumo(v, fmt="{:.1f}", un=" ms"):
    v = sorted(v)
    if not v:
        return "—"
    p95 = v[min(len(v) - 1, int(round(0.95 * (len(v) - 1))))]
    return f"mediana {fmt.format(statistics.median(v))}{un}, p95 {fmt.format(p95)}{un}, máx {fmt.format(v[-1])}{un}"


def main(prefixo):
    linhas = Path(f"{prefixo}_coord.txt").read_text(encoding="utf-8", errors="replace").splitlines()
    reqs, filas = [], []
    for l in linhas:
        if m := HTTP.match(l):
            reqs.append((float(m.group(1)), m.group(3), int(m.group(4)), int(m.group(5)), int(m.group(6))))
        elif m := FILA.match(l):
            filas.append((float(m.group(1)), int(m.group(2))))

    def fase(t):
        # Requisição mais recente terminada até t (a linha [HTTP] sai no fim do atendimento),
        # ou em andamento: a próxima, se começou antes de t (fim − duração).
        ativa = "ocioso"
        for tr, uri, _, nb, us in reqs:
            inicio = tr - us / 1e6
            if inicio <= t <= tr + 5:
                ativa = fase_da_rota(uri, nb)
            if inicio > t:
                break
        return ativa

    no = [d for t, p, d in ler(f"{prefixo}_no1.txt") if p == "[ENVIO]" and "seq" in d]
    tempos_no = [t for t, p, d in ler(f"{prefixo}_no1.txt") if p == "[ENVIO]" and "seq" in d]
    ordem = ["ocioso", "csv", "pag200", "pag50", "status"]
    print(f"## {prefixo}\n")
    print("| Fase | Ciclos do nó | Com confirmação | Resposta (ACK → confirmação) | Fila máx. | Reenviadas | "
          "Atendimento HTTP na COORD | Requisições |")
    print("|---|---|---|---|---|---|---|---|")
    for f in ordem:
        ciclos = [d for t, d in zip(tempos_no, no) if fase(t) == f and d["seq"] != 0]
        if not ciclos:
            continue
        com = [d for d in ciclos if d["conf"] != ""]
        resp = [d["resp_us"] / 1000 for d in com if d["resp_us"] != ""]
        http = [us / 1000 for _, uri, _, nb, us in reqs if fase_da_rota(uri, nb) == f]
        print(f"| {f} | {len(ciclos)} | {len(com)}/{len(ciclos)} | {resumo(resp)} | "
              f"{max(d['fila_depois'] for d in ciclos)} | {sum(d['reenviadas'] for d in ciclos)} | "
              f"{resumo(http) if http else '—'} | {len(http)} |")
    print(f"\nDescartes na fila do ESP-NOW da COORD: {filas[-1][1] if filas else 0}")
    sem_conf = [(round(t), fase(t), d["seq"]) for t, d in zip(tempos_no, no) if d["ack"] == 1 and d["conf"] == ""]
    print(f"Ciclos com ACK e sem confirmação: {len(sem_conf)} {sem_conf[:20]}")


if __name__ == "__main__":
    main(sys.argv[1])
