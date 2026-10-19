// Armazenamento e reenvio no nó (Etapa 7.1): cada leitura fica na fila da
// memória RTC (lib/fila) até o coordenador confirmar que está gravada.
// Confirmação cumulativa, assinada; regras em docs/protocolo.md, seção 13.
//
// Ciclo (rádio já ligado):
//  1. envia a leitura nova (SEM_ANTERIORES se ela é a única da fila);
//  2. espera a confirmação por ESPERA_CONFIRMACAO_MS e libera tudo até ela;
//  3. se sobrou alguma além da nova (o coordenador recusou por buraco, ou não
//     tem histórico do nó), reenvia em ordem, da mais antiga, até
//     REENVIOS_POR_CICLO pacotes; para no primeiro sem ACK MAC.
// Sem ACK ou sem confirmação: guarda tudo e dorme. A confirmação do que for
// enviado chega no ciclo seguinte (confirmação imediata, opção b do passo 2).
#pragma once

#include <Arduino.h>

#include <protocolo.h>

#include "envio.h"

namespace reenvio {

// Chamar no setup(), antes de guardar(): confere a fila na memória RTC.
void iniciar();

// Guarda a leitura recém-montada, antes de ligar o rádio. true se a fila
// estava cheia e a mais antiga foi descartada.
bool guardar(const protocolo::PacoteLeitura& p);

struct Resultado {
  envio::Resultado nova;         // envio da leitura nova (tentativas e ACK MAC)
  bool confirmou = false;        // chegou uma confirmação válida
  bool nadaGravado = false;      // a confirmação dizia NADA_GRAVADO
  uint16_t confBoot = 0;         // par confirmado
  uint32_t confSeq = 0;
  int32_t respostaUs = -1;       // do ACK MAC da nova até a confirmação válida (−1 = não chegou)
  uint8_t recusadas = 0;         // respostas que não passaram em validarConfirmacao
  protocolo::RejeicaoConfirmacao ultimaRecusa = protocolo::RejeicaoConfirmacao::NENHUMA;
  uint16_t liberadas = 0;        // saíram da fila neste ciclo
  uint8_t reenviadas = 0;        // pacotes da rajada
  uint8_t reenviadasAck = 0;     // com ACK MAC
  uint16_t filaAntes = 0;        // com a nova já incluída
  uint16_t filaDepois = 0;
  uint32_t totalUs = 0;          // do 1º envio ao fim da rajada
};

// Faz o ciclo de envio descrito acima. O rádio precisa estar ligado (envio::ligar).
void sincronizar(Resultado& r);

uint16_t quantidade();
uint32_t descartadas();  // descartes por fila cheia desde o último power-on

#ifdef TESTE_DUPLICATA
// Teste (env teste_duplicata): reenvia a leitura mais nova da fila, já
// enviada neste ciclo. O coordenador deve classificá-la como duplicada, não
// gravar e responder. Devolve true se a confirmação chegou.
bool repetirNova(envio::Resultado& r);
#endif

}  // namespace reenvio
