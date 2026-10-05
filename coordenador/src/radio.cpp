#include "radio.h"

#include <WiFi.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "config.h"
#include "segredos.h"

namespace radio {

namespace {

QueueHandle_t fila = nullptr;
std::atomic<uint32_t> descartados{0};

// Callback de recepção (ESP-IDF 5.5): roda na tarefa do Wi-Fi.
// `dados` só é válido durante a chamada; por isso a cópia vai por valor na fila.
// Espera 0 no xQueueSend: nunca bloquear a tarefa do Wi-Fi.
void aoReceber(const esp_now_recv_info_t* info, const uint8_t* dados, int len) {
  if (info == nullptr || dados == nullptr || len <= 0) return;
  Recebido r;
  memcpy(r.mac, info->src_addr, ESP_NOW_ETH_ALEN);
  r.rssi = static_cast<int8_t>(info->rx_ctrl->rssi);
  r.ruido = static_cast<int8_t>(info->rx_ctrl->noise_floor);
  r.canal = static_cast<uint8_t>(info->rx_ctrl->channel);
  r.instanteUs = info->rx_ctrl->timestamp;
  r.tamanhoOriginal = static_cast<uint16_t>(len);
  r.tamanho = static_cast<uint16_t>(len < (int)sizeof(r.dados) ? len : sizeof(r.dados));
  memcpy(r.dados, dados, r.tamanho);
  if (xQueueSend(fila, &r, 0) != pdTRUE) descartados++;
}

// Lista as redes visíveis e resume a ocupação dos canais 1, 6 e 11.
// Redes a até 4 canais de distância se sobrepõem (canais de 22 MHz espaçados de 5 MHz).
void varrerCanais() {
  Serial.println("[WIFI] varrendo canais...");
  uint32_t t0 = millis();
  int16_t n = WiFi.scanNetworks(false, true);  // síncrona, inclui redes ocultas
  Serial.printf("[WIFI] varredura: %d redes em %lu ms\n", n, millis() - t0);
  if (n < 0) return;

  Serial.println("[SCAN] canal,rssi_dbm,ssid");
  for (int16_t i = 0; i < n; i++)
    Serial.printf("[SCAN] %ld,%ld,%s\n", (long)WiFi.channel(i), (long)WiFi.RSSI(i), WiFi.SSID(i).c_str());

  for (uint8_t c : {1, 6, 11}) {
    int mesmo = 0, vizinhos = 0;
    long maisForte = -127;
    for (int16_t i = 0; i < n; i++) {
      long canal = WiFi.channel(i);
      if (canal == c) mesmo++;
      else if (labs(canal - c) <= 4) vizinhos++;
      else continue;
      maisForte = max(maisForte, (long)WiFi.RSSI(i));
    }
    if (mesmo + vizinhos == 0)
      Serial.printf("[WIFI] canal %2u: livre\n", c);
    else
      Serial.printf("[WIFI] canal %2u: %d no canal, %d sobrepostas, mais forte %ld dBm\n", c, mesmo, vizinhos,
                    maisForte);
  }
  WiFi.scanDelete();
}

}  // namespace

bool iniciar() {
  fila = xQueueCreate(config::FILA_ESPNOW_ITENS, sizeof(Recebido));
  if (fila == nullptr) {
    Serial.println("[ESPNOW] sem memória para a fila");
    return false;
  }

  // Não gravar a configuração do Wi-Fi na NVS a cada boot (padrão do núcleo: grava).
  WiFi.persistent(false);

  if (config::VARRER_CANAIS_NO_BOOT) {
    WiFi.mode(WIFI_STA);  // a varredura usa a interface STA
    varrerCanais();
  }

  // Só AP: o rádio fica fixo no canal do SoftAP (a STA da varredura é desligada).
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(segredos::AP_SSID, segredos::AP_SENHA, config::CANAL_WIFI, 0, config::AP_MAX_CONEXOES)) {
    Serial.println("[WIFI] falha ao subir o SoftAP");
    return false;
  }
  Serial.printf("[WIFI] SoftAP \"%s\" canal %u, MAC AP %s, IP %s\n", segredos::AP_SSID, WiFi.channel(),
                WiFi.softAPmacAddress().c_str(), WiFi.softAPIP().toString().c_str());

  esp_err_t err = esp_now_init();
  if (err != ESP_OK) {
    Serial.printf("[ESPNOW] esp_now_init: %s\n", esp_err_to_name(err));
    return false;
  }
  uint32_t versao = 0;
  esp_now_get_version(&versao);
  err = esp_now_register_recv_cb(aoReceber);
  if (err != ESP_OK) {
    Serial.printf("[ESPNOW] esp_now_register_recv_cb: %s\n", esp_err_to_name(err));
    return false;
  }
  Serial.printf("[ESPNOW] pronto: versao %lu, fila de %u itens (%u bytes cada)\n", (unsigned long)versao,
                (unsigned)config::FILA_ESPNOW_ITENS, (unsigned)sizeof(Recebido));
  return true;
}

bool receber(Recebido& r, uint32_t esperaMs) {
  return fila != nullptr && xQueueReceive(fila, &r, pdMS_TO_TICKS(esperaMs)) == pdTRUE;
}

uint32_t descartadosFilaCheia() { return descartados.load(); }

}  // namespace radio
