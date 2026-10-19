#!/bin/bash
# Captura das seriais (COORD e NÓ 1) com o PC na rede do coordenador, enquanto
# ferramentas/testar_api.py gera as requisições (Etapa 8, passos 5 e 6).
#
# Ordem necessária, medida em 19/10/2026 (docs/comunicacao.md, seção 9):
#  1. trocar o Wi-Fi do Windows para a horta-coord e esperar 90 s. A troca de
#     rede faz o usbip desanexar as placas do WSL cerca de 1 min depois;
#     anexando DEPOIS disso, elas ficam;
#  2. anexar as placas e iniciar a captura. Abrir a serial reinicia as duas
#     placas: o SoftAP some por alguns segundos;
#  3. reconectar o Wi-Fi quando a COORD voltar (o perfil é manual);
#  4. rodar o testar_api.py;
#  5. SEMPRE voltar para a rede de casa no fim.
# A Internet do PC deve vir por outra interface (ancoragem USB do celular).
#
# Uso: ferramentas/medir_com_wifi.sh <prefixo> <duracao_captura_s> <rede_de_casa> <args do testar_api.py ...>
#  ex.: ferramentas/medir_com_wifi.sh dados/etapa8/brutos/p5_convivencia 840 Elton carga ocioso:150 csv:180
set -u
cd "$(dirname "$0")/.."
PREFIXO=$1; DURACAO=$2; CASA=$3; shift 3
BUSIDS=${BUSIDS:-"1-4 2-3"}  # NÓ 1 e COORD no usbipd (conferir com usbipd.exe list)

volta() { netsh.exe wlan connect name="$CASA" >/dev/null 2>&1; echo "[rede] Wi-Fi de volta para $CASA"; }
trap volta EXIT
estado() { usbipd.exe list 2>/dev/null | grep CH9102 | awk '{print $1, $NF}' | tr '\n' ' '; }
esperar_coord() { for i in $(seq 1 40); do curl -s -m 2 -o /dev/null http://192.168.4.1/ && return 0; sleep 1; done; return 1; }

netsh.exe wlan connect name=horta-coord >/dev/null 2>&1
esperar_coord || { echo "ABORTADO: COORD nao respondeu na horta-coord"; exit 1; }
echo "[rede] na horta-coord; esperando 90 s (o usbip desanexa depois da troca de rede)"
sleep 90
for b in $BUSIDS; do usbipd.exe attach --wsl --busid "$b" >/dev/null 2>&1; done
sleep 5
echo "[usb] $(estado)"
rm -f "${PREFIXO}_coord.txt" "${PREFIXO}_no1.txt"
.venv/bin/python ferramentas/capturar_seriais.py "$DURACAO" "$PREFIXO" > /dev/null 2>&1 &
CAP=$!
for i in $(seq 1 40); do grep -q "servidor na porta 80" "${PREFIXO}_coord.txt" 2>/dev/null && break; sleep 1; done
grep -q "servidor na porta 80" "${PREFIXO}_coord.txt" 2>/dev/null || { echo "ABORTADO: captura nao comecou"; kill $CAP; exit 1; }
netsh.exe wlan connect name=horta-coord >/dev/null 2>&1
esperar_coord || { echo "ABORTADO: Wi-Fi nao reconectou depois do reinicio da COORD"; kill $CAP; exit 1; }
echo "[captura] gravando; Wi-Fi reconectado; usb: $(estado)"
.venv/bin/python ferramentas/testar_api.py "${PREFIXO}_http.csv" "$@"
echo "[captura] esperando o fim"
wait $CAP
echo "[usb] no fim: $(estado)"
wc -l "${PREFIXO}_coord.txt" "${PREFIXO}_no1.txt"
