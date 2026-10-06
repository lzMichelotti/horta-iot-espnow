#include "anel.h"

#include <cstdio>
#include <cstring>

namespace anel {

void nomeSegmento(uint32_t numero, char (&saida)[TAM_NOME]) {
  snprintf(saida, TAM_NOME, "%08lu.seg", static_cast<unsigned long>(numero % 100000000UL));
}

bool interpretarNome(const char* nome, uint32_t& numero) {
  if (nome == nullptr) return false;
  uint32_t v = 0;
  for (int i = 0; i < 8; i++) {
    if (nome[i] < '0' || nome[i] > '9') return false;
    v = v * 10 + static_cast<uint32_t>(nome[i] - '0');
  }
  if (strcmp(nome + 8, ".seg") != 0) return false;
  numero = v;
  return true;
}

void acumular(Resumo& s, const registro::Registro& r, bool ok) {
  s.posicoes++;
  if (!ok) return;
  if (s.validos == 0) s.bootPrimeiro = r.boot_coord;
  s.bootUltimo = r.boot_coord;
  s.validos++;
  if (!registro::horaValida(r)) {
    s.semHora++;
    return;
  }
  if (s.comHora() == 1) {
    s.utcMin = s.utcMax = r.utc_s;
  } else {
    if (r.utc_s < s.utcMin) s.utcMin = r.utc_s;
    if (r.utc_s > s.utcMax) s.utcMax = r.utc_s;
  }
}

bool cruza(const Resumo& s, uint32_t de, uint32_t ate) {
  return s.comHora() > 0 && de <= ate && s.utcMin <= ate && s.utcMax >= de;
}

uint64_t idGlobal(uint32_t segmento, uint32_t posicao, uint32_t registrosPorSegmento) {
  return static_cast<uint64_t>(segmento) * registrosPorSegmento + posicao;
}

void deIdGlobal(uint64_t id, uint32_t registrosPorSegmento, uint32_t& segmento, uint32_t& posicao) {
  segmento = static_cast<uint32_t>(id / registrosPorSegmento);
  posicao = static_cast<uint32_t>(id % registrosPorSegmento);
}

Indice::Indice(Politica p) : politica_(p) {
  if (politica_.maxSegmentos > CAPACIDADE) politica_.maxSegmentos = CAPACIDADE;
  if (politica_.maxSegmentos < 1) politica_.maxSegmentos = 1;
  if (politica_.registrosPorSegmento < 1) politica_.registrosPorSegmento = 1;
}

void Indice::limpar() { n_ = 0; }

bool Indice::adicionar(const Resumo& s) {
  if (n_ >= CAPACIDADE) return false;
  if (n_ > 0 && s.numero <= tabela_[n_ - 1].numero) return false;
  tabela_[n_++] = s;
  return true;
}

uint32_t Indice::totalValidos() const {
  uint32_t t = 0;
  for (size_t i = 0; i < n_; i++) t += tabela_[i].validos;
  return t;
}

Plano Indice::planejar() const {
  Plano p;
  const Resumo* a = atual();
  if (a != nullptr && !a->fechado && a->posicoes < politica_.registrosPorSegmento) {
    // Ainda cabe no atual. Se o limite tiver sido reduzido (outra configuração),
    // apaga o excesso, mas nunca o segmento atual.
    if (n_ > politica_.maxSegmentos) p.apagar = static_cast<uint32_t>(n_ - politica_.maxSegmentos);
    return p;
  }
  p.abrirNovo = true;
  p.numeroNovo = a ? a->numero + 1 : 1;  // primeiro segmento depois de formatar: 1
  // Com o novo, a tabela terá n_ + 1 segmentos: abre espaço antes de criar.
  if (n_ + 1 > politica_.maxSegmentos) p.apagar = static_cast<uint32_t>(n_ + 1 - politica_.maxSegmentos);
  return p;
}

void Indice::removerMaisAntigo() {
  if (n_ == 0) return;
  memmove(&tabela_[0], &tabela_[1], (n_ - 1) * sizeof(Resumo));
  n_--;
}

void Indice::abrirNovo(uint32_t numero) {
  Resumo s;
  s.numero = numero;
  if (!adicionar(s) && n_ == CAPACIDADE) {
    // Não deveria acontecer (planejar() apaga antes); por segurança, descarta o mais antigo da tabela.
    removerMaisAntigo();
    adicionar(s);
  }
}

void Indice::registrarGravacao(const registro::Registro& r) {
  if (n_ > 0) acumular(tabela_[n_ - 1], r, true);
}

void Indice::fecharAtual() {
  if (n_ > 0) tabela_[n_ - 1].fechado = true;
}

}  // namespace anel
