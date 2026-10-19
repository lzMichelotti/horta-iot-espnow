// JSON da API HTTP (Etapa 8): escritor em fluxo e leitor mínimo, sem
// alocação dinâmica. C++ puro: compila no ESP32 e no PC (testes nativos).
// Formato: RFC 8259 (https://www.rfc-editor.org/rfc/rfc8259).
//
// Escritor: escreve direto num destino (o bloco que vai para a rede), sem
// montar o documento na RAM. Números com casas decimais fixas a partir de
// inteiros (sem float: exato e igual no ESP32 e no PC, como a exportação CSV).
//
// Leitor: valida a sintaxe inteira de um texto pequeno (corpo de PUT/PATCH/POST)
// e o divide em "tokens" (objeto, lista, texto, primitivo), para buscar os
// poucos campos conhecidos de cada rota.
#pragma once

#include <cstddef>
#include <cstdint>

namespace json {

// ---------------------------------------------------------------- Escritor
using Destino = void (*)(const char* dados, size_t tamanho, void* contexto);

class Escritor {
 public:
  Escritor(Destino destino, void* contexto) : destino_(destino), contexto_(contexto) {}

  Escritor& objeto();
  Escritor& fimObjeto();
  Escritor& lista();
  Escritor& fimLista();
  Escritor& chave(const char* nome);  // dentro de um objeto, antes do valor

  Escritor& texto(const char* s);     // com escape (aspas, \, controles); UTF-8 passa intacto
  Escritor& inteiro(int64_t v);
  Escritor& natural(uint64_t v);
  // v / 10^casas com exatamente `casas` dígitos depois do ponto: decimal(-5, 2) = -0.05.
  Escritor& decimal(int32_t v, uint8_t casas);
  Escritor& booleano(bool v);
  Escritor& nulo();

  // Atalhos "chave": valor.
  Escritor& campo(const char* nome, const char* s) { return chave(nome).texto(s); }
  Escritor& campo(const char* nome, int64_t v) { return chave(nome).inteiro(v); }
  Escritor& campoNatural(const char* nome, uint64_t v) { return chave(nome).natural(v); }
  Escritor& campoBool(const char* nome, bool v) { return chave(nome).booleano(v); }
  Escritor& campoNulo(const char* nome) { return chave(nome).nulo(); }

  // false se a estrutura ficou errada (fechamento sem abertura, profundidade demais).
  bool ok() const { return ok_; }
  size_t profundidade() const { return n_; }

 private:
  static constexpr size_t MAX_PROFUNDIDADE = 16;
  void antesDeValor();
  void escrever(const char* s, size_t n) { destino_(s, n, contexto_); }
  void escrever(const char* s);
  void abrir(char c);
  void fechar(char c);

  Destino destino_;
  void* contexto_;
  bool primeiro_[MAX_PROFUNDIDADE] = {};
  size_t n_ = 0;
  bool depoisDeChave_ = false;
  bool ok_ = true;
};

// ---------------------------------------------------------------- Leitor
enum class Tipo : uint8_t { OBJETO, LISTA, TEXTO, PRIMITIVO };

struct Token {
  Tipo tipo;
  uint16_t inicio;   // posição do 1º caractere (para TEXTO, depois das aspas)
  uint16_t fim;      // posição depois do último (para TEXTO, antes das aspas)
  uint16_t proximo;  // índice do token seguinte a toda esta subárvore
};

// Analisa `s` (n caracteres) e preenche até `max` tokens. Devolve o número de
// tokens, ou −1 se o texto não for JSON válido, tiver mais tokens que `max`,
// passar de 8 níveis ou de 65535 caracteres. O 1º token é o valor raiz.
int analisar(const char* s, size_t n, Token* t, size_t max);

// Índice do valor do membro `nome` do objeto no token `objeto`, ou −1.
int buscar(const char* s, const Token* t, int ntokens, int objeto, const char* nome);

// Conversões de um token. false se o tipo não bater.
bool comoTexto(const char* s, const Token& t, char* saida, size_t max);  // só ASCII nos escapes \u
bool comoInteiro(const char* s, const Token& t, int64_t& v);            // sem fração nem expoente
bool ehNulo(const char* s, const Token& t);
bool comoBool(const char* s, const Token& t, bool& v);

}  // namespace json
