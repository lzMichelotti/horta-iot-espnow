#include "envio.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace envio {

namespace {

// Confirmação vinda do callback de envio (roda na tarefa do Wi-Fi).
struct Confirmacao {
  esp_now_send_status_t status;
  int64_t instanteUs;  // esp_timer_get_time() no callback
};

// Fila de 1 item: o callback sobrescreve (xQueueOverwrite), o loop() espera
// com tempo limite. Esvaziada antes de cada tentativa, para que um callback
// atrasado de uma tentativa anterior não seja tomado pelo da atual.
QueueHandle_t confirmacoes = nullptr;

// ESP-IDF 5.5: SUCCESS = "received successfully on the MAC layer" (ACK 802.11),
// não confirmação da aplicação (ESP-IDF v5.5, ESP-NOW, "Send ESP-NOW Data").
void aoEnviar(const esp_now_send_info_t* /*info*/, esp_now_send_status_t status) {
  Confirmacao c{status, esp_timer_get_time()};
  xQueueOverwrite(confirmacoes, &c);
}

}  // namespace

bool ligar(Resultado& r) {
  if (confirmacoes == nullptr) confirmacoes = xQueueCreate(1, sizeof(Confirmacao));
  int64_t t0 = esp_timer_get_time();

  WiFi.persistent(false);  // não gravar a config do Wi-Fi na NVS a cada ciclo
  // STA sem WiFi.begin(): o rádio liga, mas não procura nem se associa a nenhum AP.
  if (!WiFi.mode(WIFI_STA)) {
    Serial.println("[ESPNOW] falha ao ligar o Wi-Fi");
    return false;
  }
  // esp_wifi_set_channel: "should be called after esp_wifi_start()" (esp_wifi.h, ESP-IDF 5.5).
  if (WiFi.setChannel(config::CANAL_WIFI) != ESP_OK) {
    Serial.printf("[ESPNOW] falha ao fixar o canal %u\n", config::CANAL_WIFI);
    return false;
  }

  esp_err_t err = esp_now_init();
  if (err == ESP_OK) err = esp_now_register_send_cb(aoEnviar);
  if (err == ESP_OK) {
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, config::MAC_COORDENADOR, sizeof(peer.peer_addr));
    peer.channel = config::CANAL_WIFI;  // ≠ canal atual → esp_now_send retorna ESP_ERR_ESPNOW_CHAN
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    err = esp_now_add_peer(&peer);
  }
  if (err != ESP_OK) {
    Serial.printf("[ESPNOW] falha ao iniciar: %s\n", esp_err_to_name(err));
    return false;
  }
  r.ligarUs = static_cast<uint32_t>(esp_timer_get_time() - t0);
  return true;
}

void enviar(const uint8_t* dados, size_t tamanho, Resultado& r) {
  int64_t inicio = esp_timer_get_time();
  for (uint8_t k = 0; k < config::MAX_ENVIOS && !r.ack; k++) {
    if (k > 0) {
      uint32_t espera = config::BACKOFF_MS[k - 1] + esp_random() % (config::SORTEIO_MS + 1);
      delay(espera);
    }
    xQueueReset(confirmacoes);
    r.tentativas = k + 1;
    r.tentativaUs[k] = -1;

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_now_send(config::MAC_COORDENADOR, dados, tamanho);
    if (err != ESP_OK) {  // nem saiu para o ar (canal, interface, memória)
      r.ultimoErro = err;
      continue;
    }
    Confirmacao c;
    if (xQueueReceive(confirmacoes, &c, pdMS_TO_TICKS(config::TEMPO_LIMITE_CALLBACK_MS)) == pdTRUE) {
      r.tentativaUs[k] = static_cast<int32_t>(c.instanteUs - t0);
      r.ack = (c.status == ESP_NOW_SEND_SUCCESS);
    }
  }
  r.totalUs = static_cast<uint32_t>(esp_timer_get_time() - inicio);
}

void desligar() {
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);  // esp_wifi_stop + esp_wifi_deinit (WiFiGeneric.cpp, núcleo 3.3.12)
}

}  // namespace envio
