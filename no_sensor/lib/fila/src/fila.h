// Fila de leituras pendentes do nó (Etapa 7.1): guarda cada leitura até o
// coordenador confirmar que ela está gravada ("tudo até (boot, seq)").
// Decisões e capacidade: docs/protocolo.md, seção 13.
//
// C++ puro (sem Arduino.h): compila no ESP32 e no PC (testes nativos). Quem
// põe o armazenamento na memória RTC (RTC_DATA_ATTR) é src/reenvio.cpp; aqui
// só a lógica, sobre um vetor e uma palavra de controle recebidos de fora.
//
// Anel com descarte da MAIS ANTIGA quando cheio. Início e quantidade ficam
// numa única palavra de 32 bits, gravada de uma vez: o prazo máximo acordado
// (outro núcleo) pode mandar o nó dormir a qualquer momento, e a fila nunca
// fica inconsistente — no pior caso, a leitura nova não entra (o buraco se
// resolve pelo bit SEM_ANTERIORES).
#pragma once

#include <cstddef>
#include <cstdint>

#include <protocolo.h>

namespace fila {

// Uma leitura guardada: os bytes do pacote antes da idade (a idade e o bit
// SEM_ANTERIORES são escritos no envio) + o instante da leitura.
struct __attribute__((packed)) Entrada {
  uint8_t pacote[protocolo::TAMANHO_SEM_IDADE];  //  0  bytes 0–21 do PacoteLeitura
  uint32_t instante_s;                           // 22  relógio do sistema do nó, s
};
static_assert(sizeof(Entrada) == 26, "Entrada deve ter 26 bytes");

// Monta a entrada a partir do pacote recém-montado (idade e SEM_ANTERIORES são ignorados).
Entrada criar(const protocolo::PacoteLeitura& p, uint32_t instante_s);

uint16_t bootDe(const Entrada& e);
uint32_t seqDe(const Entrada& e);

// Pacote pronto para assinar: idade = agora − instante (0 se o relógio voltou)
// e o bit SEM_ANTERIORES conforme `semAnteriores`.
protocolo::PacoteLeitura paraEnvio(const Entrada& e, uint32_t agora_s, bool semAnteriores);

class Fila {
 public:
  // `controle` e `itens` vivem fora (memória RTC no firmware, variáveis comuns
  // nos testes). Controle zerado = fila vazia.
  Fila(uint32_t& controle, Entrada* itens, uint16_t capacidade);

  uint16_t capacidade() const { return capacidade_; }
  uint16_t quantidade() const;
  bool vazia() const { return quantidade() == 0; }

  // i = 0 é a mais antiga. Sem checagem de faixa: i < quantidade().
  const Entrada& em(uint16_t i) const;

  // Acrescenta no fim. Cheia: descarta a mais antiga antes e devolve true.
  bool enfileirar(const Entrada& e);

  // Libera, a partir da mais antiga, as entradas até (boot, seq) inclusive
  // (confirmação cumulativa). Devolve quantas saíram.
  uint16_t liberarAte(uint16_t boot, uint32_t seq);

  void limpar() { controle_ = 0; }

  // false se a palavra de controle não faz sentido para esta capacidade
  // (ex.: firmware novo com outra capacidade); quem chama deve limpar().
  bool coerente() const;

 private:
  uint16_t inicio() const { return static_cast<uint16_t>(controle_ & 0xFFFFu); }
  static uint32_t empacotar(uint16_t inicio, uint16_t quantidade) {
    return static_cast<uint32_t>(inicio) | (static_cast<uint32_t>(quantidade) << 16);
  }

  uint32_t& controle_;
  Entrada* itens_;
  uint16_t capacidade_;
};

}  // namespace fila
