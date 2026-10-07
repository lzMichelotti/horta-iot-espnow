#include "nos.h"

#include <esp_mac.h>

#include "configuracao.h"

namespace nos {

namespace {

No tabela[config::MAX_NOS];

void copiarNome(No& n) {
  const cadastro::No* c = configuracao::tabela().buscar(n.mac);
  strncpy(n.nome, c ? c->nome : "desconhecido", sizeof(n.nome) - 1);
  n.nome[sizeof(n.nome) - 1] = '\0';
}

}  // namespace

No* buscar(const uint8_t mac[6]) {
  for (auto& n : tabela)
    if (n.usado && memcmp(n.mac, mac, 6) == 0) return &n;
  for (auto& n : tabela)
    if (!n.usado) {
      n.usado = true;
      memcpy(n.mac, mac, 6);
      copiarNome(n);
      return &n;
    }
  return nullptr;
}

void atualizarNomes() {
  for (auto& n : tabela)
    if (n.usado) copiarNome(n);
}

const No* encontrar(const uint8_t mac[6]) {
  for (const auto& n : tabela)
    if (n.usado && memcmp(n.mac, mac, 6) == 0) return &n;
  return nullptr;
}

const No& posicao(size_t i) { return tabela[i]; }

void atualizarUltima(const registro::Registro& r) {
  uint8_t mac[6];
  memcpy(mac, r.mac, 6);  // cópia: não passar ponteiro de campo de struct packed
  No* n = buscar(mac);
  if (n == nullptr) return;
  n->ultima = r;
  n->temUltima = true;
}

void contarRecepcao(No& n, int8_t rssi) {
  if (n.recebidos == 0 || rssi < n.rssiMin) n.rssiMin = rssi;
  if (n.recebidos == 0 || rssi > n.rssiMax) n.rssiMax = rssi;
  n.recebidos++;
  n.somaRssi += rssi;
}

void imprimirResumo() {
  for (const auto& n : tabela) {
    if (!n.usado) continue;
    const auto& s = n.sequencia;
    uint32_t esperados = s.aceitos() + s.perdidos();
    float entrega = esperados ? 100.0f * s.aceitos() / esperados : 0.0f;
    float rssiMedio = n.recebidos ? (float)n.somaRssi / n.recebidos : 0.0f;
    Serial.printf("[RESUMO] %lu," MACSTR ",%s,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%.2f,%.1f,%d,%d\n", millis(),
                  MAC2STR(n.mac), n.nome, n.recebidos, n.rejeitados, s.aceitos(), s.perdidos(), s.descartados(),
                  s.reinicios(), n.semAck, entrega, rssiMedio, n.rssiMin, n.rssiMax);
  }
}

}  // namespace nos
