// Nó de experimento: a cada despertar do deep sleep, liga o rádio, envia 22 B
// (tamanho do PacoteLeitura), desliga e dorme. Mede cada fase com esp_timer.
// Sem sensores: a leitura custa o mesmo em qualquer rádio e já foi medida na
// Etapa 5 (dados/etapa5). Modos (platformio.ini): MODO_ESPNOW [CRIPTO=1|2],
// MODO_BLE_VHCI, MODO_BLE_ARDUINO.
#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#if defined(MODO_ESPNOW)
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/queue.h>
#elif defined(MODO_BLE_VHCI)
#include "hci.h"
#elif defined(MODO_BLE_ARDUINO)
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#endif

#ifndef CRIPTO
#define CRIPTO 0
#endif
#ifndef ANUNCIO_MS
#define ANUNCIO_MS 100
#endif

namespace {

constexpr uint32_t PERIODO_MS = 3000;
constexpr uint8_t CANAL = 1;
constexpr uint8_t MAC_COORD_AP[6] = {0x88, 0x57, 0x21, 0x70, 0x91, 0xFD};
constexpr size_t TAMANHO = 22;  // sizeof(protocolo::PacoteLeitura)

RTC_DATA_ATTR uint32_t seq = 0;

struct Fases {
  int64_t setup = 0, ligar = 0, enviar = 0, desligar = 0;
  bool ok = false;      // ESP-NOW: ACK da camada MAC; BLE: não há confirmação
  uint8_t tentativas = 0;
  uint8_t extra = 0;    // BLE VHCI: tipo de anúncio aceito
};

void montar(uint8_t* p) {
  memset(p, 0, TAMANHO);
  p[0] = 'H';
  p[1] = 'X';
  memcpy(&p[2], &seq, sizeof(seq));
}

#if defined(MODO_ESPNOW)
// ---------------------------------------------------------------- ESP-NOW
// Igual a no_sensor/src/envio.cpp (produção), com criptografia opcional.
const char* MODO = CRIPTO == 1 ? "espnow_cripto" : CRIPTO == 2 ? "espnow_chave_errada" : "espnow";
// Chaves de 16 B (ESP_NOW_KEY_LEN). Só para a bancada.
const uint8_t PMK[16] = {'P', 'M', 'K', '_', 'h', 'o', 'r', 't', 'a', '_', 'e', 'x', 'p', '_', '0', '1'};
const uint8_t LMK_OK[16] = {'L', 'M', 'K', '_', 'n', 'o', '1', '_', 'c', 'o', 'r', 'r', 'e', 't', 'a', '_'};
const uint8_t LMK_ERRADA[16] = {'L', 'M', 'K', '_', 'n', 'o', '1', '_', 'e', 'r', 'r', 'a', 'd', 'a', '_', '_'};

QueueHandle_t fila;
void aoEnviar(const esp_now_send_info_t*, esp_now_send_status_t s) { xQueueOverwrite(fila, &s); }

bool ligar() {
  fila = xQueueCreate(1, sizeof(esp_now_send_status_t));
  WiFi.persistent(false);
  if (!WiFi.mode(WIFI_STA) || WiFi.setChannel(CANAL) != ESP_OK) return false;
  if (esp_now_init() != ESP_OK || esp_now_register_send_cb(aoEnviar) != ESP_OK) return false;
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, MAC_COORD_AP, 6);
  peer.channel = CANAL;
  peer.ifidx = WIFI_IF_STA;
  if (CRIPTO) {
    esp_now_set_pmk(PMK);
    peer.encrypt = true;
    memcpy(peer.lmk, CRIPTO == 1 ? LMK_OK : LMK_ERRADA, 16);
  }
  return esp_now_add_peer(&peer) == ESP_OK;
}

void enviar(const uint8_t* p, Fases& f) {
  for (uint8_t k = 0; k < 3 && !f.ok; k++) {
    if (k > 0) delay(k == 1 ? 10 : 30);
    xQueueReset(fila);
    f.tentativas = k + 1;
    if (esp_now_send(MAC_COORD_AP, p, TAMANHO) != ESP_OK) continue;
    esp_now_send_status_t s;
    if (xQueueReceive(fila, &s, pdMS_TO_TICKS(100)) == pdTRUE) f.ok = (s == ESP_NOW_SEND_SUCCESS);
  }
}

void desligar() { esp_wifi_stop(); }

#elif defined(MODO_BLE_VHCI)
// ---------------------------------------------------------------- BLE (controlador)
const char* MODO = ANUNCIO_MS <= 30 ? "ble_vhci_30ms" : "ble_vhci";
uint8_t tipoAnuncio = 0x03;

bool ligar() {
  if (!hci::iniciar()) return false;
  // Intervalo 20 ms (0x20 × 0,625 ms), canais 37–39. ADV_NONCONN_IND (0x03) é o
  // tipo de um "beacon"; na especificação 4.2 ele exige intervalo ≥ 100 ms
  // (Vol 6, Part B, 4.4.2.2): se o controlador recusar, usa ADV_IND (0x00).
  uint8_t p[15] = {0x20, 0x00, 0x20, 0x00, tipoAnuncio, 0, 0, 0, 0, 0, 0, 0, 0, 0x07, 0x00};
  if (hci::comando(hci::LE_SET_ADV_PARAM, p, sizeof(p)) != 0) {
    tipoAnuncio = 0x00;
    p[4] = tipoAnuncio;
    if (hci::comando(hci::LE_SET_ADV_PARAM, p, sizeof(p)) != 0) return false;
  }
  return true;
}

void enviar(const uint8_t* dados, Fases& f) {
  // AD: flags (3 B) + fabricante (2 + 2 + 22 B) = 29 B ≤ 31 B do anúncio legado
  uint8_t p[32] = {0};
  uint8_t i = 1;
  p[i++] = 0x02; p[i++] = 0x01; p[i++] = 0x06;
  p[i++] = 1 + 2 + TAMANHO; p[i++] = 0xFF; p[i++] = 0xFF; p[i++] = 0xFF;  // 0xFFFF: ID de teste
  memcpy(&p[i], dados, TAMANHO);
  i += TAMANHO;
  p[0] = i - 1;
  f.tentativas = 1;
  uint8_t ligado = 1, desligado = 0;
  bool ok = hci::comando(hci::LE_SET_ADV_DATA, p, 32) == 0 && hci::comando(hci::LE_SET_ADV_ENABLE, &ligado, 1) == 0;
  delay(ANUNCIO_MS);
  hci::comando(hci::LE_SET_ADV_ENABLE, &desligado, 1);
  f.ok = ok;  // só "anunciou"; o BLE não tem confirmação de entrega
  f.extra = tipoAnuncio;
}

void desligar() { hci::desligar(); }

#elif defined(MODO_BLE_ARDUINO)
// ---------------------------------------------------------------- BLE (biblioteca Arduino / Bluedroid)
const char* MODO = "ble_arduino";
BLEAdvertising* anuncio = nullptr;

bool ligar() {
  if (!BLEDevice::init("")) return false;
  anuncio = BLEDevice::getAdvertising();
  return anuncio != nullptr;
}

void enviar(const uint8_t* dados, Fases& f) {
  BLEAdvertisementData d;
  d.setFlags(0x06);
  char md[2 + 2 + TAMANHO] = {static_cast<char>(1 + 2 + TAMANHO), static_cast<char>(0xFF), static_cast<char>(0xFF),
                              static_cast<char>(0xFF)};
  memcpy(&md[4], dados, TAMANHO);
  d.addData(md, sizeof(md));
  anuncio->setAdvertisementData(d);
  anuncio->setScanResponse(false);
  anuncio->setMinInterval(0x20);
  anuncio->setMaxInterval(0x20);
  f.tentativas = 1;
  f.ok = anuncio->start();
  delay(ANUNCIO_MS);
  anuncio->stop();
}

void desligar() { BLEDevice::deinit(false); }
#endif

}  // namespace

void setup() {
  Fases f;
  f.setup = esp_timer_get_time();
  Serial.begin(115200);
  Serial.printf("[T0] %lld\n", f.setup);

  uint8_t p[TAMANHO];
  montar(p);

  int64_t t = esp_timer_get_time();
  bool ligado = ligar();
  f.ligar = esp_timer_get_time() - t;
  t = esp_timer_get_time();
  if (ligado) enviar(p, f);
  f.enviar = esp_timer_get_time() - t;
  t = esp_timer_get_time();
  desligar();
  f.desligar = esp_timer_get_time() - t;
  int64_t fim = esp_timer_get_time();

  // modo,seq,ligado,ok,tentativas,extra,setup_us,ligar_us,enviar_us,desligar_us,fim_us
  Serial.printf("[CICLO] %s,%lu,%u,%u,%u,%u,%lld,%lld,%lld,%lld,%lld\n", MODO, (unsigned long)seq, ligado, f.ok,
                f.tentativas, f.extra, f.setup, f.ligar, f.enviar, f.desligar, fim);
  seq++;

  uint64_t ativoUs = esp_timer_get_time();
  esp_sleep_enable_timer_wakeup((uint64_t)PERIODO_MS * 1000 - ativoUs);
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {}
