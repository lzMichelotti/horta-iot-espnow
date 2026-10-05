// Rádio do coordenador: SoftAP em canal fixo + recepção ESP-NOW por fila.
//
// O callback de recepção roda na tarefa do Wi-Fi e só copia o pacote para uma
// fila do FreeRTOS; o loop() retira e processa ("do not do lengthy operations
// in the callback function. Instead, post the necessary data to a queue",
// ESP-IDF v5.5, ESP-NOW).
#pragma once

#include <Arduino.h>
#include <esp_now.h>

namespace radio {

// Um pacote recebido, como saiu do callback (cópia por valor na fila).
struct Recebido {
  uint8_t mac[ESP_NOW_ETH_ALEN];        // MAC de origem: identifica o nó
  int8_t rssi;                          // dBm (rx_ctrl->rssi)
  int8_t ruido;                         // piso de ruído, dBm (rx_ctrl->noise_floor, só ESP32)
  uint8_t canal;                        // canal em que o quadro chegou
  uint32_t instanteUs;                  // rx_ctrl->timestamp: relógio do rádio, µs
  uint16_t tamanhoOriginal;             // len entregue pelo ESP-NOW
  uint16_t tamanho;                     // bytes copiados para `dados` (≤ 250)
  uint8_t dados[ESP_NOW_MAX_DATA_LEN];  // carga útil (limite do ESP-NOW v1)
};

// Varredura opcional, SoftAP no canal de config, ESP-NOW e callback.
// Retorna false se alguma etapa falhar (detalhe no log [WIFI]/[ESPNOW]).
bool iniciar();

// Retira um pacote da fila, esperando até `esperaMs`. false se não houver.
bool receber(Recebido& r, uint32_t esperaMs);

// Pacotes perdidos porque a fila estava cheia (instrumentação).
uint32_t descartadosFilaCheia();

}  // namespace radio
