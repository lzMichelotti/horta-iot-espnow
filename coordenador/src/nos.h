// Estado por nó no coordenador: rastreador de sequência (comum/protocolo) e
// contadores para a avaliação (taxa de entrega e RSSI). Tabela fixa, indexada
// pelo MAC de origem.
#pragma once

#include <Arduino.h>

#include <protocolo.h>

#include "config.h"

namespace nos {

struct No {
  bool usado = false;
  uint8_t mac[6] = {};
  const char* nome = nullptr;
  protocolo::RastreadorSequencia sequencia;
  uint32_t recebidos = 0;   // todos os quadros deste MAC (válidos ou não)
  uint32_t rejeitados = 0;  // reprovados em protocolo::validar
  uint32_t semAck = 0;      // pacotes com o bit ANTERIOR_SEM_ACK (falhas que o nó relatou)
  int32_t somaRssi = 0;
  int8_t rssiMin = 0;
  int8_t rssiMax = 0;
};

// Encontra o nó pelo MAC ou ocupa uma posição livre. nullptr se a tabela estiver cheia.
No* buscar(const uint8_t mac[6]);

// Atualiza os contadores de recepção (antes da validação).
void contarRecepcao(No& n, int8_t rssi);

// Uma linha [RESUMO] por nó.
void imprimirResumo();

}  // namespace nos
