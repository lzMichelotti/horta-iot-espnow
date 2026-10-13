"""Chaves do HMAC do protocolo v2 (docs/protocolo.md, seção 12).

  mestra  gera a chave mestra (32 B aleatórios, módulo secrets) e a grava em
          coordenador/src/segredos.h. Recusa se já houver uma (--substituir
          troca, e então TODOS os nós precisam de chave nova).
  no MAC  deriva a chave do nó, HMAC-SHA256(mestra, MAC STA), e grava
          no_sensor/src/segredos.h. Rodar antes de gravar cada nó (cada nó
          tem a sua chave; o firmware confere se o MAC bate).

Os dois segredos.h ficam fora do Git (.gitignore); os modelos versionados são
os segredos.exemplo.h.

Uso:
  .venv/bin/python ferramentas/chaves.py mestra
  .venv/bin/python ferramentas/chaves.py no 88:57:21:70:93:70
"""
import argparse
import hashlib
import hmac
import re
import secrets
import shutil
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
SEGREDOS_COORD = RAIZ / "coordenador" / "src" / "segredos.h"
EXEMPLO_COORD = RAIZ / "coordenador" / "src" / "segredos.exemplo.h"
SEGREDOS_NO = RAIZ / "no_sensor" / "src" / "segredos.h"
TAMANHO_CHAVE = 32
PADRAO_MESTRA = re.compile(r"(constexpr uint8_t CHAVE_MESTRA\[32\] = \{)([^}]*)(\};)", re.S)


def em_c(dados):
    linhas = []
    for i in range(0, len(dados), 8):
        linhas.append("    " + ", ".join(f"0x{b:02x}" for b in dados[i:i + 8]) + ",")
    return "\n" + "\n".join(linhas) + "\n"


def ler_mestra():
    if not SEGREDOS_COORD.exists():
        sys.exit(f"{SEGREDOS_COORD} não existe: rode primeiro 'chaves.py mestra'")
    m = PADRAO_MESTRA.search(SEGREDOS_COORD.read_text(encoding="utf-8"))
    if not m:
        sys.exit(f"CHAVE_MESTRA não encontrada em {SEGREDOS_COORD}")
    valores = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", m.group(2)))
    if len(valores) != TAMANHO_CHAVE or not any(valores):
        sys.exit("CHAVE_MESTRA inválida (32 bytes, não todos zero): rode 'chaves.py mestra'")
    return valores


def cmd_mestra(substituir):
    if not SEGREDOS_COORD.exists():
        shutil.copy(EXEMPLO_COORD, SEGREDOS_COORD)
        print(f"{SEGREDOS_COORD} criado a partir do modelo: troque AP_SSID e AP_SENHA")
    texto = SEGREDOS_COORD.read_text(encoding="utf-8")
    m = PADRAO_MESTRA.search(texto)
    nova = secrets.token_bytes(TAMANHO_CHAVE)
    if m:
        existente = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", m.group(2)))
        if any(existente) and not substituir:
            sys.exit("já existe uma CHAVE_MESTRA; use --substituir (todos os nós precisarão de chave nova)")
        texto = texto[:m.start(2)] + em_c(nova) + texto[m.end(2):]
    else:
        bloco = ("\n// Chave mestra do HMAC (protocolo v2). Gerada por ferramentas/chaves.py.\n"
                 f"constexpr uint8_t CHAVE_MESTRA[32] = {{{em_c(nova)}}};\n")
        fim = texto.rindex("}  // namespace segredos")
        texto = texto[:fim] + bloco + "\n" + texto[fim:]
        if "#include <cstdint>" not in texto:
            texto = texto.replace("#pragma once\n", "#pragma once\n\n#include <cstdint>\n", 1)
    SEGREDOS_COORD.write_text(texto, encoding="utf-8")
    print(f"CHAVE_MESTRA gravada em {SEGREDOS_COORD}")


def cmd_no(mac_texto):
    partes = re.fullmatch(r"([0-9a-fA-F]{2})(?::([0-9a-fA-F]{2})){5}", mac_texto)
    if not partes:
        sys.exit("MAC no formato aa:bb:cc:dd:ee:ff (o MAC STA do nó, docs/placas.md)")
    mac = bytes(int(x, 16) for x in mac_texto.split(":"))
    chave = hmac.new(ler_mestra(), mac, hashlib.sha256).digest()  # = protocolo::derivarChaveNo
    SEGREDOS_NO.write_text(
        "// Chave do HMAC deste nó (protocolo v2). Gerado por ferramentas/chaves.py; fora do Git.\n"
        f"// Nó: {mac_texto.lower()}. Para outro nó, gerar de novo antes de gravar.\n"
        "#pragma once\n\n#include <cstdint>\n\nnamespace segredos {\n\n"
        f"constexpr uint8_t MAC_DA_CHAVE[6] = {{{em_c(mac)}}};\n"
        f"constexpr uint8_t CHAVE_NO[32] = {{{em_c(chave)}}};\n\n"
        "}  // namespace segredos\n",
        encoding="utf-8")
    print(f"chave do nó {mac_texto.lower()} gravada em {SEGREDOS_NO}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("mestra")
    m.add_argument("--substituir", action="store_true")
    n = sub.add_parser("no")
    n.add_argument("mac")
    a = ap.parse_args()
    if a.cmd == "mestra":
        cmd_mestra(a.substituir)
    else:
        cmd_no(a.mac)


if __name__ == "__main__":
    main()
