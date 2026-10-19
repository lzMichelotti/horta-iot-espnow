#include "exportacao.h"

#include <cstdio>
#include <cstring>

#include <calendario.h>

namespace exportacao {

namespace {

const char* nomeEstado(protocolo::Estado e) {
  switch (e) {
    case protocolo::Estado::OK: return "ok";
    case protocolo::Estado::ERRO: return "erro";
    case protocolo::Estado::FORA_DE_FAIXA: return "fora_de_faixa";
    case protocolo::Estado::RESERVADO: return "reservado";
  }
  return "?";
}

bool temValor(protocolo::Estado e) { return e == protocolo::Estado::OK || e == protocolo::Estado::FORA_DE_FAIXA; }

// Centésimos → "23.66" / "-0.05" (sem float: exato e igual no ESP32 e no PC).
void centesimos(char (&s)[12], int32_t v) {
  uint32_t a = v < 0 ? static_cast<uint32_t>(-v) : static_cast<uint32_t>(v);
  snprintf(s, sizeof(s), "%s%lu.%02lu", v < 0 ? "-" : "", static_cast<unsigned long>(a / 100),
           static_cast<unsigned long>(a % 100));
}

}  // namespace

uint32_t crc32(uint32_t crc, const uint8_t* dados, size_t tamanho) {
  crc = ~crc;
  for (size_t i = 0; i < tamanho; i++) {
    crc ^= dados[i];
    for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
  }
  return ~crc;
}

const char* cabecalho() {
  return "id,mac,nome,hora,utc,boot_coord,desde_boot_s,classe,perdidos,rssi,ruido,"
         "boot_no,seq,temp_c,ur_pct,solo_mv,solo_pct,alim_mv,"
         "est_temp,est_ur,est_solo,est_alim,motivo_boot,acordado_ant_ms,tentativas_ant,flags,idade_s";
}

size_t formatarLinha(char (&saida)[TAM_LINHA], uint64_t id, const registro::Registro& r, const char* nome,
                     filtro::Hora hora, uint32_t utcEfetivo, bool temPct, int16_t pctDecimos) {
  protocolo::PacoteLeitura p = r.pacote;  // cópia: não ler campos packed por referência
  using protocolo::Grandeza;
  protocolo::Estado eT = protocolo::lerEstado(p.estados, Grandeza::TEMPERATURA);
  protocolo::Estado eU = protocolo::lerEstado(p.estados, Grandeza::UMIDADE_AR);
  protocolo::Estado eS = protocolo::lerEstado(p.estados, Grandeza::SOLO);
  protocolo::Estado eA = protocolo::lerEstado(p.estados, Grandeza::ALIMENTACAO);

  char utc[calendario::TAM_ISO] = "";
  if (hora != filtro::Hora::SEM_HORA) calendario::formatarIso(utcEfetivo, utc);
  char temp[12] = "", ur[12] = "", solo[8] = "", pct[12] = "", alim[8] = "";
  if (temValor(eT)) centesimos(temp, p.temperatura_c100);
  if (temValor(eU)) centesimos(ur, p.umidade_ar_c100);
  if (temValor(eS)) snprintf(solo, sizeof(solo), "%u", p.solo_mv);
  if (temPct) snprintf(pct, sizeof(pct), "%d.%d", pctDecimos / 10, pctDecimos % 10);
  if (temValor(eA)) snprintf(alim, sizeof(alim), "%u", p.alimentacao_mv);

  int n = snprintf(saida, TAM_LINHA,
                   "%llu,%02X:%02X:%02X:%02X:%02X:%02X,%s,%s,%s,%u,%lu,%s,%u,%d,%d,"
                   "%u,%lu,%s,%s,%s,%s,%s,"
                   "%s,%s,%s,%s,%u,%u,%u,%u,%lu",
                   static_cast<unsigned long long>(id), r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
                   nome, filtro::nomeHora(hora), utc, r.boot_coord, static_cast<unsigned long>(r.desde_boot_s),
                   protocolo::nomeClasse(static_cast<protocolo::Classe>(r.classe)), r.perdidos, r.rssi, r.ruido,
                   p.boot, static_cast<unsigned long>(p.seq), temp, ur, solo, pct, alim, nomeEstado(eT),
                   nomeEstado(eU), nomeEstado(eS), nomeEstado(eA), p.motivo_boot, p.acordado_ant_ms,
                   p.tentativas_ant, p.flags, static_cast<unsigned long>(p.idade_s));
  if (n < 0) {
    saida[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n) < TAM_LINHA ? static_cast<size_t>(n) : TAM_LINHA - 1;
}

}  // namespace exportacao
