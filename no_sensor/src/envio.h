// Envio ESP-NOW do nó: Wi-Fi em STA sem associação no canal fixo, coordenador
// como peer, envio com espera pelo callback e retransmissão com backoff.
// Política e medições em docs/comunicacao.md.
#pragma once

#include <Arduino.h>

#include "config.h"

namespace envio {

struct Resultado {
  bool ack = false;                                // alguma tentativa recebeu o ACK da camada MAC
  uint8_t tentativas = 0;                          // chamadas a esp_now_send (1..MAX_ENVIOS)
  uint32_t ligarUs = 0;                            // Wi-Fi STA + canal + esp_now_init + peer
  int32_t tentativaUs[config::MAX_ENVIOS] = {};    // esp_now_send → callback; −1 = sem callback no prazo
  uint32_t totalUs = 0;                            // do 1º esp_now_send ao fim (inclui backoffs)
  esp_err_t ultimoErro = ESP_OK;                   // retorno de esp_now_send ≠ ESP_OK, se houve
};

// Liga o Wi-Fi e o ESP-NOW e cadastra o coordenador. Mede `r.ligarUs`.
bool ligar(Resultado& r);

// Envia `dados` ao coordenador com até `maxEnvios` tentativas (≤ MAX_ENVIOS).
void enviar(const uint8_t* dados, size_t tamanho, Resultado& r, uint8_t maxEnvios = config::MAX_ENVIOS);

// Resposta recebida do coordenador (Etapa 7.1): só quadros vindos do MAC AP do
// coordenador e do tamanho de uma confirmação entram na fila de recepção.
struct Resposta {
  uint8_t dados[32];
  uint8_t tamanho;
  int64_t instanteUs;  // esp_timer_get_time() no callback de recepção
};

// Esvazia a fila de respostas (chamar antes de enviar o pacote que espera resposta).
void descartarRespostas();

// Espera uma resposta por até `esperaMs`. false se não chegou nenhuma.
bool receberResposta(Resposta& r, uint32_t esperaMs);

// Desliga o rádio (esp_wifi_stop) antes do deep sleep.
void desligar();

}  // namespace envio
