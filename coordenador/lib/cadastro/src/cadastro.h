// Cadastro dos nós no coordenador: MAC → nome e calibração do solo.
// C++ puro: compila no ESP32 e no PC (testes nativos). Quem grava na NVS é
// src/configuracao.cpp (um blob com a tabela inteira). Detalhes:
// docs/persistencia.md.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cadastro {

constexpr size_t MAX_NOS = 8;
constexpr size_t TAM_NOME = 16;  // até 15 caracteres + '\0' (limite das chaves da NVS, por coerência)
constexpr uint16_t MV_MAX = 3300;  // mesma faixa do solo no protocolo (SOLO_MAX_MV)

struct No {
  uint8_t mac[6];
  char nome[TAM_NOME];
  uint16_t mvSeco;   // ponto seco, mV; 0 = não definido
  uint16_t mvUmido;  // ponto úmido, mV; 0 = não definido
};

enum class Resultado : uint8_t {
  OK = 0,
  NOME_INVALIDO,      // vazio, longo demais ou com caractere fora de [A-Za-z0-9_-]
  TABELA_CHEIA,
  NAO_ENCONTRADO,
  MV_INVALIDO,        // fora de 1–3300 mV
  ORDEM_INVALIDA,     // com os dois pontos definidos, o seco precisa ser maior que o úmido
};
const char* nomeResultado(Resultado r);

enum class Ponto : uint8_t { SECO, UMIDO };

bool nomeValido(const char* nome);

// "aa:bb:cc:dd:ee:ff" (maiúsculas ou minúsculas). false se o formato não bater.
bool interpretarMac(const char* texto, uint8_t mac[6]);

// Calibração completa: os dois pontos definidos e seco > úmido (o sensor
// capacitivo dá tensão maior no solo seco).
bool temCalibracao(const No& n);

// Umidade do solo em décimos de % (0–1000), pela reta entre os dois pontos
// (docs/protocolo.md, seção 7), limitada a 0–100 %. false sem calibração.
bool umidadeSoloDecimos(const No& n, uint16_t mv, int16_t& decimos);

class Tabela {
 public:
  void limpar() { n_ = 0; }
  size_t quantidade() const { return n_; }
  const No& em(size_t i) const { return nos_[i]; }
  const No* buscar(const uint8_t mac[6]) const;

  // Cadastra o nó (se não existir) ou troca o nome.
  Resultado definirNome(const uint8_t mac[6], const char* nome);
  // Os dois pontos de uma vez (nó já cadastrado).
  Resultado definirCalibracao(const uint8_t mac[6], uint16_t mvSeco, uint16_t mvUmido);
  // Um ponto só (calibração em campo, um de cada vez).
  Resultado definirPonto(const uint8_t mac[6], Ponto p, uint16_t mv);
  Resultado removerCalibracao(const uint8_t mac[6]);
  Resultado remover(const uint8_t mac[6]);

  // ---------------------------------------------------------------- Blob (NVS)
  // Tamanho fixo: cabeçalho (versão, quantidade, 2 reservados) + MAX_NOS
  // entradas de 26 bytes (mac, nome, seco, úmido), little-endian.
  static constexpr uint8_t VERSAO_BLOB = 1;
  static constexpr size_t TAM_ENTRADA = 6 + TAM_NOME + 2 + 2;
  static constexpr size_t TAM_BLOB = 4 + MAX_NOS * TAM_ENTRADA;

  void serializar(uint8_t (&saida)[TAM_BLOB]) const;
  // Valida tudo (versão, tamanho, quantidade, nomes, pontos, MACs repetidos)
  // antes de trocar a tabela; se algo falhar, devolve false e nada muda.
  bool desserializar(const uint8_t* dados, size_t tamanho);

 private:
  No* buscarMut(const uint8_t mac[6]);
  No nos_[MAX_NOS] = {};
  size_t n_ = 0;
};

}  // namespace cadastro
