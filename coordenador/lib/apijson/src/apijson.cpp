#include "apijson.h"

#include <cstdio>

#include <calendario.h>

namespace apijson {

void mac(char (&saida)[TAM_MAC], const uint8_t m[6]) {
  snprintf(saida, TAM_MAC, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

const char* nomeEstado(protocolo::Estado e) {
  switch (e) {
    case protocolo::Estado::OK: return "ok";
    case protocolo::Estado::ERRO: return "erro";
    case protocolo::Estado::FORA_DE_FAIXA: return "fora_de_faixa";
    case protocolo::Estado::RESERVADO: return "reservado";
  }
  return "?";
}

void hora(json::Escritor& e, filtro::Hora h, uint32_t utc) {
  e.objeto().chave("utc");
  if (h == filtro::Hora::SEM_HORA) {
    e.nulo();
  } else {
    char iso[calendario::TAM_ISO];
    calendario::formatarIso(utc, iso);
    e.texto(iso);
  }
  e.campo("origem", filtro::nomeHora(h)).fimObjeto();
}

namespace {

bool temValor(protocolo::Estado s) { return s == protocolo::Estado::OK || s == protocolo::Estado::FORA_DE_FAIXA; }

}  // namespace

void leitura(json::Escritor& e, int64_t id, const registro::Registro& r, const char* nome, filtro::Hora h,
             uint32_t utcLeitura, bool temPct, int16_t pctDecimos) {
  protocolo::PacoteLeitura p = r.pacote;  // cópia: não ler campos packed por referência
  using protocolo::Grandeza;
  protocolo::Estado eT = protocolo::lerEstado(p.estados, Grandeza::TEMPERATURA);
  protocolo::Estado eU = protocolo::lerEstado(p.estados, Grandeza::UMIDADE_AR);
  protocolo::Estado eS = protocolo::lerEstado(p.estados, Grandeza::SOLO);
  protocolo::Estado eA = protocolo::lerEstado(p.estados, Grandeza::ALIMENTACAO);
  uint8_t m[6];
  for (int i = 0; i < 6; i++) m[i] = r.mac[i];
  char txtMac[TAM_MAC];
  mac(txtMac, m);

  e.objeto();
  if (id >= 0) e.campo("id", id);
  e.campo("mac", txtMac).campo("nome", nome);
  e.chave("hora");
  hora(e, h, utcLeitura);
  e.campoNatural("idade_s", p.idade_s);
  e.chave("temperatura_c");
  if (temValor(eT)) e.decimal(p.temperatura_c100, 2);
  else e.nulo();
  e.chave("umidade_ar_pct");
  if (temValor(eU)) e.decimal(p.umidade_ar_c100, 2);
  else e.nulo();
  e.chave("solo_mv");
  if (temValor(eS)) e.natural(p.solo_mv);
  else e.nulo();
  e.chave("solo_pct");
  if (temPct) e.decimal(pctDecimos, 1);
  else e.nulo();
  e.chave("alimentacao_mv");
  if (temValor(eA)) e.natural(p.alimentacao_mv);
  else e.nulo();
  e.chave("estados")
      .objeto()
      .campo("temperatura", nomeEstado(eT))
      .campo("umidade_ar", nomeEstado(eU))
      .campo("solo", nomeEstado(eS))
      .campo("alimentacao", nomeEstado(eA))
      .fimObjeto();
  e.campo("rssi_dbm", int64_t(r.rssi))
      .campoNatural("no_boot", p.boot)
      .campoNatural("no_seq", p.seq)
      .campoNatural("perdidos_antes", r.perdidos)
      .fimObjeto();
}

}  // namespace apijson
