#include "configuracao.h"

#include <Preferences.h>
#include <cstring>
#include <esp_mac.h>

#include "config.h"
#include "nos.h"

namespace configuracao {

namespace {

// Mesmo namespace do contador de boots do coordenador (src/relogio.cpp).
// https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html
constexpr const char* NVS_NAMESPACE = "coordenador";
constexpr const char* NVS_CHAVE_NOS = "nos";

cadastro::Tabela cad;

void carregarSemente() {
  cad.limpar();
  for (const auto& s : config::NOS) {
    cad.definirNome(s.mac, s.nome);
    if (s.mvSeco != 0 && s.mvUmido != 0) cad.definirCalibracao(s.mac, s.mvSeco, s.mvUmido);
  }
}

// Grava a tabela inteira (um blob: atualização atômica na NVS).
bool salvar() {
  uint8_t blob[cadastro::Tabela::TAM_BLOB];
  cad.serializar(blob);
  Preferences nvs;
  nvs.begin(NVS_NAMESPACE, false);
  size_t n = nvs.putBytes(NVS_CHAVE_NOS, blob, sizeof(blob));
  nvs.end();
  if (n != sizeof(blob)) {
    Serial.printf("[CFG] ERRO ao gravar o cadastro na NVS (%u de %u bytes)\n", (unsigned)n, (unsigned)sizeof(blob));
    return false;
  }
  nos::atualizarNomes();
  return true;
}

void imprimir() {
  Serial.printf("[CFG] cadastro: %u de %u nos\n", (unsigned)cad.quantidade(), (unsigned)cadastro::MAX_NOS);
  Serial.println("[CFG] mac,nome,mv_seco,mv_umido,calibrado,ultimo_solo_mv,solo_pct");
  for (size_t i = 0; i < cad.quantidade(); i++) {
    const cadastro::No& c = cad.em(i);
    char mv[8] = "-", pct[8] = "-";
    const nos::No* n = nos::encontrar(c.mac);
    if (n != nullptr && n->temUltima) {
      protocolo::PacoteLeitura p = n->ultima.pacote;
      snprintf(mv, sizeof(mv), "%u", p.solo_mv);
      int16_t d = 0;
      if (umidadeSolo(n->ultima, d)) snprintf(pct, sizeof(pct), "%d.%d", d / 10, d % 10);
    }
    Serial.printf("[CFG] " MACSTR ",%s,%u,%u,%d,%s,%s\n", MAC2STR(c.mac), c.nome, c.mvSeco, c.mvUmido,
                  cadastro::temCalibracao(c), mv, pct);
  }
}

void resultado(cadastro::Resultado r) {
  if (r != cadastro::Resultado::OK) {
    Serial.printf("[CFG] nao alterado: %s\n", cadastro::nomeResultado(r));
    return;
  }
  if (salvar()) {
    Serial.println("[CFG] cadastro gravado na NVS");
    imprimir();
  }
}

// Ponto de calibração a partir da ÚLTIMA leitura do nó (calibração em campo).
void pontoDaUltima(const uint8_t mac[6], cadastro::Ponto ponto) {
  const nos::No* n = nos::encontrar(mac);
  if (n == nullptr || !n->temUltima) {
    Serial.println("[CFG] nao alterado: ainda nao ha leitura deste no");
    return;
  }
  protocolo::PacoteLeitura p = n->ultima.pacote;
  if (protocolo::lerEstado(p.estados, protocolo::Grandeza::SOLO) != protocolo::Estado::OK) {
    Serial.println("[CFG] nao alterado: a ultima leitura do solo nao esta OK");
    return;
  }
  Serial.printf("[CFG] ponto %s = %u mV (ultima leitura, seq %lu)\n", ponto == cadastro::Ponto::SECO ? "seco" : "umido",
                p.solo_mv, (unsigned long)p.seq);
  resultado(cad.definirPonto(mac, ponto, p.solo_mv));
}

void uso() {
  Serial.println("[CFG] uso: no | no nome MAC NOME | no cal MAC SECO UMIDO | no seco MAC | no umido MAC");
  Serial.println("[CFG]      no semcal MAC | no apagar MAC | no padrao");
}

}  // namespace

void iniciar() {
  Preferences nvs;
  nvs.begin(NVS_NAMESPACE, true);  // só leitura
  uint8_t blob[cadastro::Tabela::TAM_BLOB];
  size_t tam = nvs.getBytesLength(NVS_CHAVE_NOS);
  size_t lidos = (tam == sizeof(blob)) ? nvs.getBytes(NVS_CHAVE_NOS, blob, sizeof(blob)) : 0;
  nvs.end();

  if (lidos == sizeof(blob) && cad.desserializar(blob, lidos)) {
    Serial.printf("[CFG] cadastro lido da NVS: %u nos\n", (unsigned)cad.quantidade());
    return;
  }
  if (tam != 0) Serial.printf("[CFG] cadastro na NVS invalido (%u bytes); usando a semente\n", (unsigned)tam);
  else Serial.println("[CFG] sem cadastro na NVS (primeiro boot); gravando a semente de config.h");
  carregarSemente();
  salvar();
}

const cadastro::Tabela& tabela() { return cad; }

bool umidadeSolo(const registro::Registro& r, int16_t& decimos) {
  uint8_t mac[6];
  memcpy(mac, r.mac, 6);
  const cadastro::No* c = cad.buscar(mac);
  protocolo::PacoteLeitura p = r.pacote;
  if (c == nullptr || protocolo::lerEstado(p.estados, protocolo::Grandeza::SOLO) != protocolo::Estado::OK) return false;
  return cadastro::umidadeSoloDecimos(*c, p.solo_mv, decimos);
}

void comando(const char* argumento) {
  char copia[config::CONSOLE_LINHA_MAX + 1];
  strncpy(copia, argumento ? argumento : "", sizeof(copia) - 1);
  copia[sizeof(copia) - 1] = '\0';
  const char* tok[5] = {};
  size_t n = 0;
  for (char* t = strtok(copia, " "); t != nullptr && n < 5; t = strtok(nullptr, " ")) tok[n++] = t;

  if (n == 0) {
    imprimir();
    return;
  }
  if (strcmp(tok[0], "padrao") == 0 && n == 1) {
    carregarSemente();
    resultado(cadastro::Resultado::OK);
    return;
  }
  uint8_t mac[6];
  if (n < 2 || !cadastro::interpretarMac(tok[1], mac)) {
    uso();
    return;
  }
  if (strcmp(tok[0], "nome") == 0 && n == 3) {
    resultado(cad.definirNome(mac, tok[2]));
  } else if (strcmp(tok[0], "cal") == 0 && n == 4) {
    long seco = atol(tok[2]), umido = atol(tok[3]);
    if (seco < 0 || seco > 65535 || umido < 0 || umido > 65535) {
      resultado(cadastro::Resultado::MV_INVALIDO);
      return;
    }
    resultado(cad.definirCalibracao(mac, static_cast<uint16_t>(seco), static_cast<uint16_t>(umido)));
  } else if (strcmp(tok[0], "seco") == 0 && n == 2) {
    pontoDaUltima(mac, cadastro::Ponto::SECO);
  } else if (strcmp(tok[0], "umido") == 0 && n == 2) {
    pontoDaUltima(mac, cadastro::Ponto::UMIDO);
  } else if (strcmp(tok[0], "semcal") == 0 && n == 2) {
    resultado(cad.removerCalibracao(mac));
  } else if (strcmp(tok[0], "apagar") == 0 && n == 2) {
    resultado(cad.remover(mac));
  } else {
    uso();
  }
}

}  // namespace configuracao
