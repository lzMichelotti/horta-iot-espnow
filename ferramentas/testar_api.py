"""Testes da API HTTP do coordenador a partir do PC (Etapa 8, passos 5 e 6).

Só biblioteca padrão (urllib, threading). Cada requisição vira uma linha no
CSV de saída: instante do PC, cliente, rota, código, bytes e duração medida no
cliente (conexão TCP + resposta inteira).

O PC precisa estar na rede do coordenador. Com --wifi, o script conecta o Wi-Fi
do Windows à horta-coord no início e SEMPRE volta para a rede indicada no fim
(netsh; a Internet do PC deve vir por outra interface, ex.: ancoragem USB).

Modos:
  rotas N           N requisições por rota de leitura (tempo de resposta por rota)
  carga F:S [...]   fases de carga, cada uma S segundos: ocioso, csv, pag200, pag50, status
  clientes K S      K clientes ao mesmo tempo pedindo páginas de 50 por S segundos
  escrita           rotas de escrita sem token, com token errado e com token certo (códigos e cabeçalhos)
  status            confere no /status o id do coordenador (MAC) e a geração do histórico
  paginas N         N páginas do histórico para cada limite (10, 50, 100, 200)
  csv N             N exportações completas em CSV (tempo, bytes, linhas)
  bateria           rotas 100, paginas 30, csv 3, clientes 2 120 e escrita, em sequência
  heap S            S segundos de requisições contínuas e variadas (CSV completo a cada 10 min),
                    com o heap lido no /status a cada 60 s (<saida>_heap.csv)

Uso:
  .venv/bin/python ferramentas/testar_api.py <saida.csv> [--wifi CASA] <modo> [args]
  ex.: ... testar_api.py dados/etapa8/brutos/p5_carga.csv --wifi Elton carga ocioso:120 csv:180 pag200:180
"""
import argparse
import csv
import statistics
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

BASE = "http://192.168.4.1"
TOKEN_ARQ = Path(__file__).parent.parent / "dados" / "etapa8" / "rascunho" / "token.txt"
NO = "88:57:21:70:93:70"
ROTAS_LEITURA = ["/", "/api/v1/status", "/api/v1/nos", f"/api/v1/nos/{NO}", "/api/v1/leituras/atuais",
                 "/api/v1/leituras?limite=50", "/api/v1/leituras?limite=200", "/api/v1/alertas"]

trava = threading.Lock()


def pedir(metodo, rota, corpo=None, token=None, prazo=120):
    """(código, bytes, segundos, cabeçalhos) medidos no cliente."""
    dados = corpo.encode() if corpo is not None else None
    req = urllib.request.Request(BASE + rota, data=dados, method=metodo)
    if dados is not None:
        req.add_header("Content-Type", "application/json")
    if token is not None:
        req.add_header("Authorization", f"Bearer {token}")
    t0 = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=prazo) as r:
            n = len(r.read())
            return r.status, n, time.perf_counter() - t0, dict(r.headers)
    except urllib.error.HTTPError as e:
        n = len(e.read())
        return e.code, n, time.perf_counter() - t0, dict(e.headers)
    except Exception as e:  # noqa: BLE001 (tempo esgotado, conexão recusada...)
        return f"falha:{type(e).__name__}", 0, time.perf_counter() - t0, {}


class Registro:
    def __init__(self, arquivo):
        self.f = open(arquivo, "w", newline="", encoding="utf-8")
        self.w = csv.writer(self.f)
        self.w.writerow(["t_pc", "cliente", "fase", "metodo", "rota", "codigo", "bytes", "s"])
        self.linhas = []

    def anotar(self, cliente, fase, metodo, rota, res):
        codigo, n, s, _ = res
        linha = [f"{time.time():.3f}", cliente, fase, metodo, rota, codigo, n, f"{s:.4f}"]
        with trava:
            self.w.writerow(linha)
            self.f.flush()
            self.linhas.append(linha)


def resumo(valores):
    v = sorted(valores)
    if not v:
        return "—"
    p95 = v[min(len(v) - 1, int(round(0.95 * (len(v) - 1))))]
    return f"mediana {1000 * statistics.median(v):.1f} ms, p95 {1000 * p95:.1f} ms, máx {1000 * v[-1]:.1f} ms (n={len(v)})"


def modo_rotas(reg, n):
    for rota in ROTAS_LEITURA:
        tempos = []
        for _ in range(n):
            res = pedir("GET", rota)
            reg.anotar(0, "rotas", "GET", rota, res)
            if res[0] == 200:
                tempos.append(res[2])
        print(f"{rota:40s} {resumo(tempos)}")


def fase_requisicao(fase):
    if fase == "csv":
        return "GET", "/api/v1/exportar.csv"
    if fase == "pag200":
        return "GET", "/api/v1/leituras?limite=200"
    if fase == "pag50":
        return "GET", "/api/v1/leituras?limite=50"
    if fase == "status":
        return "GET", "/api/v1/status"
    return None


def modo_carga(reg, fases):
    for item in fases:
        fase, seg = item.split(":")
        fim = time.time() + float(seg)
        print(f"[fase] {fase} por {seg} s", flush=True)
        reg.anotar(0, fase, "-", "inicio_fase", ("-", 0, 0, {}))
        req = fase_requisicao(fase)
        tempos = []
        while time.time() < fim:
            if req is None:
                time.sleep(1)
                continue
            res = pedir(*req)
            reg.anotar(0, fase, req[0], req[1], res)
            if res[0] == 200:
                tempos.append(res[2])
        if req is not None:
            print(f"        {req[1]}: {resumo(tempos)}")


def modo_clientes(reg, k, seg):
    fim = time.time() + seg

    def cliente(i):
        while time.time() < fim:
            rota = "/api/v1/leituras?limite=50" if i % 2 == 0 else "/api/v1/status"
            reg.anotar(i, f"clientes{k}", "GET", rota, pedir("GET", rota))

    ths = [threading.Thread(target=cliente, args=(i,)) for i in range(k)]
    for t in ths:
        t.start()
    for t in ths:
        t.join()
    for i in range(k):
        minhas = [l for l in reg.linhas if l[1] == i and l[2] == f"clientes{k}"]
        tempos = [float(l[7]) for l in minhas if l[5] == 200]
        falhas = [l for l in minhas if l[5] != 200]
        print(f"cliente {i}: {resumo(tempos)}; falhas {len(falhas)}")


def modo_escrita(reg):
    token = TOKEN_ARQ.read_text().strip()
    errado = "0123456789abcdef0123456789abcdef"
    epoch = str(int(time.time()))
    casos = [
        ("sem token", "PUT", "/api/v1/hora", '{"epoch": %s}' % epoch, None),
        ("token errado", "PUT", "/api/v1/hora", '{"epoch": %s}' % epoch, errado),
        ("token certo", "PUT", "/api/v1/hora", '{"epoch": %s}' % epoch, token),
        ("sem token", "PATCH", f"/api/v1/nos/{NO}", '{"nome": "no1"}', None),
        ("token errado", "PATCH", f"/api/v1/nos/{NO}", '{"nome": "no1"}', errado),
        ("token certo", "PATCH", f"/api/v1/nos/{NO}", '{"nome": "no1"}', token),
        ("sem token", "POST", f"/api/v1/nos/{NO}/calibrar", '{"ponto": "seco"}', None),
        ("token errado", "POST", f"/api/v1/nos/{NO}/calibrar", '{"ponto": "seco"}', errado),
        ("método errado", "DELETE", "/api/v1/status", None, None),
    ]
    for nome, m, rota, corpo, tok in casos:
        res = pedir(m, rota, corpo, tok)
        reg.anotar(0, "escrita", m, rota, res)
        extra = {k: v for k, v in res[3].items() if k.lower() in ("www-authenticate", "allow")}
        print(f"{nome:14s} {m:6s} {rota:40s} -> {res[0]} {extra}")


def modo_status(reg):
    """Campos do /status que o app usa como chave: coordenador (MAC) e geração do histórico."""
    import json
    import re
    res = urllib.request.urlopen(BASE + "/api/v1/status", timeout=30)
    st = json.loads(res.read())
    reg.anotar(0, "status", "GET", "/api/v1/status", (res.status, 0, 0, {}))
    coord = st.get("coordenador", "")
    geracao = st.get("armazenamento", {}).get("geracao")
    ok_coord = re.fullmatch(r"([0-9a-f]{2}:){5}[0-9a-f]{2}", coord) is not None
    ok_ger = geracao is None or re.fullmatch(r"[0-9a-f]{8}", geracao) is not None
    print(f"firmware {st.get('firmware')}; coordenador {coord} ({'ok' if ok_coord else 'FORMATO ERRADO'}); "
          f"geracao {geracao} ({'ok' if ok_ger else 'FORMATO ERRADO'})")
    if not (ok_coord and ok_ger):
        sys.exit(1)


def modo_paginas(reg, n):
    for limite in (10, 50, 100, 200):
        rota = f"/api/v1/leituras?limite={limite}"
        tempos, tamanhos = [], []
        for _ in range(n):
            res = pedir("GET", rota)
            reg.anotar(0, "paginas", "GET", rota, res)
            if res[0] == 200:
                tempos.append(res[2])
                tamanhos.append(res[1])
        kb = statistics.median(tamanhos) / 1024 if tamanhos else 0
        print(f"limite={limite:3d}: {resumo(tempos)}; {kb:.1f} KB por página")


def modo_csv(reg, n):
    for _ in range(n):
        req = urllib.request.Request(BASE + "/api/v1/exportar.csv")
        t0 = time.perf_counter()
        try:
            with urllib.request.urlopen(req, timeout=600) as r:
                corpo = r.read()
                codigo = r.status
        except Exception as e:  # noqa: BLE001
            corpo, codigo = b"", f"falha:{type(e).__name__}"
        s = time.perf_counter() - t0
        reg.anotar(0, "csv", "GET", "/api/v1/exportar.csv", (codigo, len(corpo), s, {}))
        linhas = corpo.count(b"\n") - 1  # sem o cabeçalho
        print(f"csv: {codigo}, {linhas} linhas, {len(corpo) / 1024:.0f} KB em {s:.1f} s "
              f"({len(corpo) / 1024 / s:.0f} KB/s)", flush=True)


def modo_heap(reg, segundos, arquivo):
    import json
    rotas = ["/api/v1/status", "/api/v1/nos", "/api/v1/leituras/atuais", "/api/v1/leituras?limite=50",
             "/api/v1/leituras?limite=200", f"/api/v1/nos/{NO}", "/api/v1/alertas"]
    inicio = time.time()
    proxima_amostra = inicio
    proximo_csv = inicio + 600
    amostras = open(arquivo, "w", newline="", encoding="utf-8")
    w = csv.writer(amostras)
    w.writerow(["t_pc", "min", "heap_livre", "heap_minimo", "uptime_s", "requisicoes", "falhas"])
    n = falhas = i = 0
    while time.time() - inicio < segundos:
        agora = time.time()
        if agora >= proxima_amostra:
            proxima_amostra += 60
            res_status = urllib.request.urlopen(BASE + "/api/v1/status", timeout=30)
            st = json.loads(res_status.read())
            m = st["memoria"]
            w.writerow([f"{agora:.0f}", f"{(agora - inicio) / 60:.1f}", m["heap_livre"], m["heap_minimo"], st["uptime_s"], n,
                        falhas])
            amostras.flush()
            print(f"[{(agora - inicio) / 60:5.1f} min] heap livre {m['heap_livre']} B, mínimo {m['heap_minimo']} B, "
                  f"{n} requisições, {falhas} falhas", flush=True)
            continue
        if agora >= proximo_csv:
            proximo_csv += 600
            modo_csv(reg, 1)
            continue
        rota = rotas[i % len(rotas)]
        i += 1
        res = pedir("GET", rota)
        reg.anotar(0, "heap", "GET", rota, res)
        n += 1
        if res[0] != 200:
            falhas += 1
    amostras.close()


def wifi(rede):
    subprocess.run(["netsh.exe", "wlan", "connect", f"name={rede}"], capture_output=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("saida")
    ap.add_argument("--wifi", metavar="CASA", help="conectar à horta-coord e voltar para CASA no fim")
    ap.add_argument("modo", choices=["rotas", "carga", "clientes", "escrita", "paginas", "csv", "bateria", "heap", "status"])
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    reg = Registro(a.saida)
    try:
        if a.wifi:
            wifi("horta-coord")
            for i in range(30):
                if pedir("GET", "/", prazo=2)[0] == 200:
                    break
                time.sleep(1)
            print(f"[rede] COORD respondeu depois de ~{i + 1} s", flush=True)
        if a.modo == "rotas":
            modo_rotas(reg, int(a.args[0]) if a.args else 50)
        elif a.modo == "carga":
            modo_carga(reg, a.args)
        elif a.modo == "clientes":
            modo_clientes(reg, int(a.args[0]), float(a.args[1]))
        elif a.modo == "paginas":
            modo_paginas(reg, int(a.args[0]) if a.args else 30)
        elif a.modo == "heap":
            modo_heap(reg, float(a.args[0]) if a.args else 3600, a.saida.replace(".csv", "_heap.csv"))
        elif a.modo == "csv":
            modo_csv(reg, int(a.args[0]) if a.args else 1)
        elif a.modo == "status":
            modo_status(reg)
        elif a.modo == "bateria":
            for nome, f in (("rotas 100", lambda: modo_rotas(reg, 100)), ("paginas 30", lambda: modo_paginas(reg, 30)),
                            ("csv 3", lambda: modo_csv(reg, 3)), ("clientes 2 120", lambda: modo_clientes(reg, 2, 120)),
                            ("escrita", lambda: modo_escrita(reg))):
                print(f"\n## {nome}", flush=True)
                f()
        else:
            modo_escrita(reg)
    finally:
        if a.wifi:
            wifi(a.wifi)
            print(f"[rede] Wi-Fi de volta para {a.wifi}")


if __name__ == "__main__":
    sys.exit(main())
