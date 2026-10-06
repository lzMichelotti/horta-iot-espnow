// Lógica da consulta ao histórico (sem acesso a arquivos): hora efetiva com
// âncoras por boot, filtro por nó e período, e percurso pelos ids globais com
// cursor de paginação nas duas ordens.
// C++ puro: compila no ESP32 e no PC (testes nativos). Quem lê os arquivos é
// src/consulta.cpp. Detalhes: docs/persistencia.md.
#pragma once

#include <cstddef>
#include <cstdint>

#include <anel.h>
#include <registro.h>

namespace filtro {

// ---------------------------------------------------------------- Âncoras
// Só para os boots do coordenador que têm registros sem hora (o relógio foi
// acertado depois de o boot começar): desvio = utc − desde_boot do primeiro
// registro com hora válida daquele boot. Um registro sem hora do mesmo boot
// recebe utc = desvio + desde_boot (supõe que a hora não deu salto no meio).
// Boots com todos os registros com hora não ocupam lugar na tabela.
class Ancoras {
 public:
  static constexpr size_t CAPACIDADE = 32;  // boots com registros sem hora; o mais antigo sai primeiro

  void limpar() { n_ = 0; }
  // Chamar para cada registro válido, na ordem de gravação.
  void registrar(const registro::Registro& r);
  bool desvio(uint16_t boot, int64_t& saida) const;
  size_t quantidade() const { return n_; }

 private:
  struct Ancora {
    uint16_t boot;
    bool temDesvio;  // false: boot com registros sem hora, ainda sem nenhum com hora
    int64_t desvio;
  };
  Ancora tabela_[CAPACIDADE];
  size_t n_ = 0;
};

enum class Hora : uint8_t { VALIDA, RECONSTRUIDA, SEM_HORA };
const char* nomeHora(Hora h);

// Hora usada no filtro: a gravada, a reconstruída ou nenhuma.
Hora horaEfetiva(const registro::Registro& r, const Ancoras& a, uint32_t& utc);

// ---------------------------------------------------------------- Filtro
struct Filtro {
  bool porHora = false;
  uint32_t de = 0, ate = 0;  // UTC, inclusive, sobre a hora efetiva
  bool porNo = false;
  uint8_t mac[6] = {};
  bool decrescente = false;  // true = mais recentes primeiro
  uint64_t cursor = 0;       // id global de onde começar; 0 = do início (ou do fim, se decrescente)
  uint16_t limite = 100;     // registros entregues por página (mínimo 1)
  uint32_t maxLidos = 0;     // para depois de ler tantos registros (0 = sem limite)
};

// true se o registro entra no resultado.
bool passa(const Filtro& f, const registro::Registro& r, Hora h, uint32_t utcEfetivo);

// true se o segmento inteiro pode ser pulado sem ler. Segmentos com registros
// sem hora nunca são pulados numa consulta por período: a hora reconstruída
// pode cair fora do intervalo [utcMin, utcMax] do resumo.
bool podePular(const Filtro& f, const anel::Resumo& s);

// ---------------------------------------------------------------- Percurso
// Caminha pelas posições existentes no índice, na ordem pedida, a partir do
// cursor. Os ids não precisam ser contíguos (segmentos apagados pela rotação).
// O cursor da página seguinte é o id() da posição em que o percurso parou
// (ainda não visitada). Crescente: um cursor depois do último registro
// devolve "nada", mas continua valendo para buscar só os registros novos.
class Percurso {
 public:
  Percurso(const anel::Indice& ind, uint64_t cursor, bool decrescente);

  // Posição atual: segmento (índice na tabela, 0 = mais antigo) e registro.
  bool valido() const { return valido_; }
  size_t segmento() const { return seg_; }
  uint32_t posicao() const { return pos_; }
  uint64_t id() const;

  void avancar();         // próxima posição na ordem pedida
  void pularSegmento();   // vai para o próximo segmento na ordem pedida

  // O cursor apontava para registros já apagados pela rotação.
  bool cursorRotacionado() const { return rotacionado_; }

 private:
  // Primeira posição (crescente) ou última (decrescente) do segmento s ou do
  // próximo não vazio na ordem pedida.
  void irParaSegmento(size_t s);
  const anel::Indice& ind_;
  bool dec_;
  bool valido_ = false;
  bool rotacionado_ = false;
  size_t seg_ = 0;
  uint32_t pos_ = 0;
};

}  // namespace filtro
