// Proteções do ciclo do nó (Etapa 5, passo 5):
//  - prazo máximo acordado: se o ciclo travar, um esp_timer força o deep sleep;
//  - tensão baixa: 3 modos com histerese (normal, economia, crítico).
// Nenhuma falha pode deixar o nó acordado drenando a bateria.
#pragma once

#include <Arduino.h>

#include "sensores.h"

namespace protecao {

// Chamar no início do setup(). Zera o estado na RAM do RTC se não for o
// despertar do deep sleep e arma o prazo de TEMPO_MAX_ACORDADO_MS.
void iniciar(bool despertarDoSono);

// Desarma o prazo (chamar antes de entrar no sono normalmente).
void cancelarPrazo();

// O ciclo anterior foi interrompido pelo prazo (vai no bit CICLO_ANTERIOR_ABORTADO).
bool cicloAnteriorAbortado();

enum class Modo : uint8_t {
  NORMAL,    // envia e dorme o período
  ECONOMIA,  // V < V_TX: envia e dorme FATOR_PERIODO_ECONOMIA × o período
  CRITICO,   // V < V_CRIT: não liga o rádio e dorme PERIODO_CRITICO_MS
};
const char* nomeModo(Modo m);

// Atualiza o modo pela tensão de alimentação lida, com histerese; leitura com
// erro mantém o modo atual. Limiares 0 = proteção desativada (bateria ainda não definida).
Modo avaliarTensao(sensores::Medida& alimentacao);

// Período até o próximo despertar no modo atual.
uint32_t periodoMs(Modo m);

}  // namespace protecao
