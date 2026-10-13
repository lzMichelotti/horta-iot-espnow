// Estado por nó no coordenador: rastreador de sequência (comum/protocolo) e
// contadores para a avaliação (taxa de entrega e RSSI). Tabela fixa, indexada
// pelo MAC de origem.
#pragma once

#include <Arduino.h>

#include <protocolo.h>
#include <registro.h>

#include "config.h"

namespace nos {

struct No {
  bool usado = false;
  uint8_t mac[6] = {};
  char nome[16] = "desconhecido";  // cópia do cadastro (atualizarNomes após editar)
  // Chave do HMAC deste nó, derivada da chave mestra pelo MAC quando o nó entra
  // na tabela (protocolo v2). Sem chave mestra válida, temChave = false e todo
  // pacote do nó é recusado (assinatura_invalida).
  uint8_t chave[protocolo::TAMANHO_CHAVE] = {};
  bool temChave = false;
  protocolo::RastreadorSequencia sequencia;
  uint32_t recebidos = 0;   // todos os quadros deste MAC (válidos ou não)
  uint32_t rejeitados = 0;  // reprovados em protocolo::validar
  uint32_t semAck = 0;      // pacotes com o bit ANTERIOR_SEM_ACK (falhas que o nó relatou)
  int32_t somaRssi = 0;
  int8_t rssiMin = 0;
  int8_t rssiMax = 0;
  // Última leitura gravada no histórico (preenchida na carga do boot e a cada
  // gravação): o app tem o valor atual logo depois de um reinício.
  bool temUltima = false;
  registro::Registro ultima{};
};

// Encontra o nó pelo MAC ou ocupa uma posição livre. nullptr se a tabela estiver cheia.
No* buscar(const uint8_t mac[6]);

// Copia de novo o nome de cada nó do cadastro (depois de um comando "no").
void atualizarNomes();

// Encontra o nó pelo MAC sem ocupar posição. nullptr se não estiver na tabela.
const No* encontrar(const uint8_t mac[6]);

// Posição i da tabela (0 a config::MAX_NOS − 1); conferir No::usado.
const No& posicao(size_t i);

// Guarda r como a última leitura do nó de origem (ocupa uma posição se preciso)
// e retoma no rastreador o (boot, seq) desse registro: na carga do histórico no
// boot, um pacote repetido não passa como "primeiro" (protocolo v2, seção 6).
void atualizarUltima(const registro::Registro& r);

// Esquece o (boot, seq) do nó (comando "no esquecer MAC"): o próximo pacote é
// aceito como "primeiro". Para um nó cuja NVS foi apagada (boot_antigo).
bool esquecer(const uint8_t mac[6]);

// true se a chave mestra do segredos.h foi gerada (não é toda zero).
bool chaveMestraValida();

// Atualiza os contadores de recepção (antes da validação).
void contarRecepcao(No& n, int8_t rssi);

// Uma linha [RESUMO] por nó.
void imprimirResumo();

}  // namespace nos
