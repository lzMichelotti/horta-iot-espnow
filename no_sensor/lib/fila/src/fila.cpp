#include "fila.h"

#include <cstring>

namespace fila {

Entrada criar(const protocolo::PacoteLeitura& p, uint32_t instante_s) {
  Entrada e{};
  std::memcpy(e.pacote, &p, sizeof(e.pacote));
  e.instante_s = instante_s;
  return e;
}

uint16_t bootDe(const Entrada& e) {
  uint16_t b;
  std::memcpy(&b, e.pacote + offsetof(protocolo::PacoteLeitura, boot), sizeof(b));
  return b;
}

uint32_t seqDe(const Entrada& e) {
  uint32_t s;
  std::memcpy(&s, e.pacote + offsetof(protocolo::PacoteLeitura, seq), sizeof(s));
  return s;
}

protocolo::PacoteLeitura paraEnvio(const Entrada& e, uint32_t agora_s, bool semAnteriores) {
  protocolo::PacoteLeitura p{};
  std::memcpy(&p, e.pacote, sizeof(e.pacote));
  uint32_t instante = e.instante_s;
  p.idade_s = agora_s >= instante ? agora_s - instante : 0;
  if (semAnteriores) p.flags |= protocolo::flag::SEM_ANTERIORES;
  else p.flags &= static_cast<uint8_t>(~protocolo::flag::SEM_ANTERIORES);
  return p;
}

Fila::Fila(uint32_t& controle, Entrada* itens, uint16_t capacidade)
    : controle_(controle), itens_(itens), capacidade_(capacidade) {}

uint16_t Fila::quantidade() const { return static_cast<uint16_t>(controle_ >> 16); }

const Entrada& Fila::em(uint16_t i) const { return itens_[(inicio() + i) % capacidade_]; }

bool Fila::enfileirar(const Entrada& e) {
  uint16_t ini = inicio(), n = quantidade();
  bool descartou = false;
  if (n >= capacidade_) {
    // Primeiro tira a mais antiga (uma escrita), depois reaproveita a posição dela.
    ini = static_cast<uint16_t>((ini + 1) % capacidade_);
    n = static_cast<uint16_t>(capacidade_ - 1);
    controle_ = empacotar(ini, n);
    descartou = true;
  }
  itens_[(ini + n) % capacidade_] = e;  // a entrada inteira antes de contá-la
  controle_ = empacotar(ini, static_cast<uint16_t>(n + 1));
  return descartou;
}

uint16_t Fila::liberarAte(uint16_t boot, uint32_t seq) {
  uint16_t n = quantidade(), k = 0;
  while (k < n && protocolo::compararPar(bootDe(em(k)), seqDe(em(k)), boot, seq) <= 0) k++;
  if (k > 0) controle_ = empacotar(static_cast<uint16_t>((inicio() + k) % capacidade_), static_cast<uint16_t>(n - k));
  return k;
}

bool Fila::coerente() const { return capacidade_ > 0 && inicio() < capacidade_ && quantidade() <= capacidade_; }

}  // namespace fila
