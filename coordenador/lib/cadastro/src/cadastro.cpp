#include "cadastro.h"

#include <cstdio>
#include <cstring>

namespace cadastro {

namespace {

bool caractereValido(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

bool mvValido(uint16_t mv) { return mv >= 1 && mv <= MV_MAX; }

// Seco > úmido quando os dois estão definidos (0 = não definido).
bool ordemValida(uint16_t seco, uint16_t umido) { return seco == 0 || umido == 0 || seco > umido; }

void gravar16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>(v >> 8);
}

uint16_t ler16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

}  // namespace

const char* nomeResultado(Resultado r) {
  switch (r) {
    case Resultado::OK: return "ok";
    case Resultado::NOME_INVALIDO: return "nome invalido (1 a 15 caracteres: letras, digitos, _ ou -)";
    case Resultado::TABELA_CHEIA: return "tabela cheia";
    case Resultado::NAO_ENCONTRADO: return "no nao cadastrado";
    case Resultado::MV_INVALIDO: return "mV fora de 1-3300";
    case Resultado::ORDEM_INVALIDA: return "o ponto seco precisa ser maior que o umido";
  }
  return "?";
}

bool nomeValido(const char* nome) {
  if (nome == nullptr) return false;
  size_t n = strlen(nome);
  if (n == 0 || n >= TAM_NOME) return false;
  for (size_t i = 0; i < n; i++)
    if (!caractereValido(nome[i])) return false;
  return true;
}

bool interpretarMac(const char* texto, uint8_t mac[6]) {
  if (texto == nullptr || strlen(texto) != 17) return false;
  for (int i = 0; i < 6; i++) {
    int v = 0;
    for (int j = 0; j < 2; j++) {
      char c = texto[i * 3 + j];
      int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
      if (d < 0) return false;
      v = v * 16 + d;
    }
    if (i < 5 && texto[i * 3 + 2] != ':') return false;
    mac[i] = static_cast<uint8_t>(v);
  }
  return true;
}

bool temCalibracao(const No& n) { return n.mvSeco != 0 && n.mvUmido != 0 && n.mvSeco > n.mvUmido; }

bool umidadeSoloDecimos(const No& n, uint16_t mv, int16_t& decimos) {
  if (!temCalibracao(n)) return false;
  // % = (seco − mv) / (seco − úmido) × 100, em décimos e arredondado.
  int32_t num = (static_cast<int32_t>(n.mvSeco) - mv) * 1000;
  int32_t den = static_cast<int32_t>(n.mvSeco) - n.mvUmido;
  int32_t v = (num >= 0 ? num + den / 2 : num - den / 2) / den;
  if (v < 0) v = 0;
  if (v > 1000) v = 1000;
  decimos = static_cast<int16_t>(v);
  return true;
}

const No* Tabela::buscar(const uint8_t mac[6]) const {
  for (size_t i = 0; i < n_; i++)
    if (memcmp(nos_[i].mac, mac, 6) == 0) return &nos_[i];
  return nullptr;
}

No* Tabela::buscarMut(const uint8_t mac[6]) { return const_cast<No*>(buscar(mac)); }

Resultado Tabela::definirNome(const uint8_t mac[6], const char* nome) {
  if (!nomeValido(nome)) return Resultado::NOME_INVALIDO;
  No* n = buscarMut(mac);
  if (n == nullptr) {
    if (n_ == MAX_NOS) return Resultado::TABELA_CHEIA;
    n = &nos_[n_++];
    *n = No{};
    memcpy(n->mac, mac, 6);
  }
  memset(n->nome, 0, TAM_NOME);
  strncpy(n->nome, nome, TAM_NOME - 1);
  return Resultado::OK;
}

Resultado Tabela::definirCalibracao(const uint8_t mac[6], uint16_t mvSeco, uint16_t mvUmido) {
  No* n = buscarMut(mac);
  if (n == nullptr) return Resultado::NAO_ENCONTRADO;
  if (!mvValido(mvSeco) || !mvValido(mvUmido)) return Resultado::MV_INVALIDO;
  if (mvSeco <= mvUmido) return Resultado::ORDEM_INVALIDA;
  n->mvSeco = mvSeco;
  n->mvUmido = mvUmido;
  return Resultado::OK;
}

Resultado Tabela::definirPonto(const uint8_t mac[6], Ponto p, uint16_t mv) {
  No* n = buscarMut(mac);
  if (n == nullptr) return Resultado::NAO_ENCONTRADO;
  if (!mvValido(mv)) return Resultado::MV_INVALIDO;
  uint16_t seco = p == Ponto::SECO ? mv : n->mvSeco;
  uint16_t umido = p == Ponto::UMIDO ? mv : n->mvUmido;
  if (!ordemValida(seco, umido)) return Resultado::ORDEM_INVALIDA;
  n->mvSeco = seco;
  n->mvUmido = umido;
  return Resultado::OK;
}

Resultado Tabela::removerCalibracao(const uint8_t mac[6]) {
  No* n = buscarMut(mac);
  if (n == nullptr) return Resultado::NAO_ENCONTRADO;
  n->mvSeco = n->mvUmido = 0;
  return Resultado::OK;
}

Resultado Tabela::remover(const uint8_t mac[6]) {
  No* n = buscarMut(mac);
  if (n == nullptr) return Resultado::NAO_ENCONTRADO;
  size_t i = static_cast<size_t>(n - nos_);
  memmove(&nos_[i], &nos_[i + 1], (n_ - i - 1) * sizeof(No));
  n_--;
  return Resultado::OK;
}

void Tabela::serializar(uint8_t (&saida)[TAM_BLOB]) const {
  memset(saida, 0, TAM_BLOB);
  saida[0] = VERSAO_BLOB;
  saida[1] = static_cast<uint8_t>(n_);
  for (size_t i = 0; i < n_; i++) {
    uint8_t* e = saida + 4 + i * TAM_ENTRADA;
    memcpy(e, nos_[i].mac, 6);
    memcpy(e + 6, nos_[i].nome, TAM_NOME);
    gravar16(e + 6 + TAM_NOME, nos_[i].mvSeco);
    gravar16(e + 8 + TAM_NOME, nos_[i].mvUmido);
  }
}

bool Tabela::desserializar(const uint8_t* dados, size_t tamanho) {
  if (dados == nullptr || tamanho != TAM_BLOB || dados[0] != VERSAO_BLOB || dados[1] > MAX_NOS) return false;
  Tabela t;
  for (size_t i = 0; i < dados[1]; i++) {
    const uint8_t* e = dados + 4 + i * TAM_ENTRADA;
    No n{};
    memcpy(n.mac, e, 6);
    memcpy(n.nome, e + 6, TAM_NOME);
    if (n.nome[TAM_NOME - 1] != '\0' || !nomeValido(n.nome)) return false;
    n.mvSeco = ler16(e + 6 + TAM_NOME);
    n.mvUmido = ler16(e + 8 + TAM_NOME);
    if ((n.mvSeco != 0 && !mvValido(n.mvSeco)) || (n.mvUmido != 0 && !mvValido(n.mvUmido)) ||
        !ordemValida(n.mvSeco, n.mvUmido))
      return false;
    if (t.buscar(n.mac) != nullptr) return false;  // MAC repetido
    t.nos_[t.n_++] = n;
  }
  *this = t;
  return true;
}

}  // namespace cadastro
