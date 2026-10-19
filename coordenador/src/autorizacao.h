// Token das rotas de escrita da API HTTP (Etapa 8, passo 3; docs/api.md, seção 3).
//
// 128 bits aleatórios (32 caracteres hexadecimais), gerados na COORD pelo RNG
// de hardware com o rádio ligado ("produces true random numbers so long as
// [...] Wi-Fi or Bluetooth are enabled", ESP-IDF v5.5, Random Number Generation),
// guardados na NVS (namespace "coordenador", chave "token"). Configurado só pelo
// console serial. Sem token configurado, toda escrita é recusada.
#pragma once

#include <Arduino.h>

namespace autorizacao {

constexpr size_t TAM_TOKEN = 32;  // caracteres hexadecimais (128 bits)

// Lê o token da NVS. Chamar no setup().
void iniciar();

bool configurado();

// Confere o valor do cabeçalho Authorization ("Bearer <token>"), em tempo
// constante. false sem token configurado.
bool autorizado(const char* cabecalho);

// Comando "token" da serial: token | token gerar | token apagar
void comando(const char* argumento);

}  // namespace autorizacao
