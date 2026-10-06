// Lógica do histórico em segmentos numerados em anel (sem acesso a arquivos).
// C++ puro: compila no ESP32 e no PC (testes nativos). Quem mexe no LittleFS é
// src/historico.cpp, seguindo o Plano devolvido por Indice::planejar().
//
// Organização (docs/persistencia.md): arquivos /h/NNNNNNNN.seg com até K
// registros de 48 bytes; cheio, abre o número seguinte; acima do limite de
// segmentos, apaga o mais antigo. O registro i de um segmento fica no byte
// i × sizeof(registro::Registro).
#pragma once

#include <cstddef>
#include <cstdint>

#include <registro.h>

namespace anel {

constexpr size_t CAPACIDADE = 64;  // máximo de segmentos na tabela em RAM

// ---------------------------------------------------------------- Nomes
// "00000123.seg": 8 dígitos decimais + ".seg".
constexpr size_t TAM_NOME = 13;
void nomeSegmento(uint32_t numero, char (&saida)[TAM_NOME]);
// true só para exatamente 8 dígitos + ".seg" (outros arquivos são ignorados).
bool interpretarNome(const char* nome, uint32_t& numero);

// ---------------------------------------------------------------- Resumo
// Resumo de um segmento, usado para pular segmentos fora do período pedido.
struct Resumo {
  uint32_t numero = 0;
  uint32_t posicoes = 0;   // registros ocupados no arquivo (tamanho / 48), válidos ou não
  uint32_t validos = 0;    // CRC e versão corretos
  uint32_t semHora = 0;    // válidos com hora inválida
  uint32_t utcMin = 0;     // só dos válidos com hora (sem sentido se comHora() == 0)
  uint32_t utcMax = 0;
  uint16_t bootPrimeiro = 0;  // boot do coordenador no 1º e no último registro válido
  uint16_t bootUltimo = 0;
  // Não recebe mais registros mesmo sem estar cheio: o arquivo tem bytes
  // sobrando no fim (tamanho não múltiplo de 48, ex.: escrita parcial) e
  // anexar desalinharia todos os registros seguintes.
  bool fechado = false;

  uint32_t comHora() const { return validos - semHora; }
};

// Conta um registro lido ou gravado (ok = passou em registro::ler).
void acumular(Resumo& s, const registro::Registro& r, bool ok);

// true se algum registro com hora válida pode estar em [de, ate] (inclusive).
bool cruza(const Resumo& s, uint32_t de, uint32_t ate);

// ---------------------------------------------------------------- Id global
// Identificador estável de um registro: número do segmento × K + posição.
// Cresce com o tempo e serve de cursor de paginação (passo 7). Só vale
// enquanto K não mudar.
uint64_t idGlobal(uint32_t segmento, uint32_t posicao, uint32_t registrosPorSegmento);
void deIdGlobal(uint64_t id, uint32_t registrosPorSegmento, uint32_t& segmento, uint32_t& posicao);

// ---------------------------------------------------------------- Índice
struct Politica {
  uint32_t registrosPorSegmento;  // K
  uint32_t maxSegmentos;          // limitado a CAPACIDADE (e no mínimo 1)
};

// O que fazer antes de gravar o próximo registro, nesta ordem:
// 1. apagar os `apagar` segmentos mais antigos; 2. se abrirNovo, criar o
// segmento `numeroNovo`; 3. anexar o registro ao segmento atual.
struct Plano {
  uint32_t apagar = 0;
  bool abrirNovo = false;
  uint32_t numeroNovo = 0;
};

// Tabela em RAM dos segmentos existentes, do mais antigo (0) ao atual.
class Indice {
 public:
  explicit Indice(Politica p);

  const Politica& politica() const { return politica_; }
  void limpar();

  // Carga no boot: um resumo por segmento, em ordem crescente de número.
  // false (e nada muda) se a tabela estiver cheia ou fora de ordem.
  bool adicionar(const Resumo& s);

  size_t quantidade() const { return n_; }
  const Resumo& em(size_t i) const { return tabela_[i]; }  // 0 = mais antigo
  const Resumo* atual() const { return n_ ? &tabela_[n_ - 1] : nullptr; }
  uint32_t totalValidos() const;

  Plano planejar() const;

  // Atualizações depois que a operação no arquivo deu certo.
  void removerMaisAntigo();
  void abrirNovo(uint32_t numero);
  void registrarGravacao(const registro::Registro& r);  // no segmento atual
  void fecharAtual();  // o próximo planejar() abre um segmento novo

 private:
  Politica politica_;
  Resumo tabela_[CAPACIDADE];
  size_t n_ = 0;
};

}  // namespace anel
