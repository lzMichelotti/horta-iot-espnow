// Coordenador de experimento: SoftAP no canal 1 (como a produção) + recepção
// ESP-NOW. CRIPTO=1: NÓ 1 cadastrado como peer cifrado. BLE_SCAN: varredura
// BLE passiva contínua ao mesmo tempo (coexistência Wi-Fi/BLE no mesmo rádio,
// ESP-IDF v5.5 "RF Coexistence"). Imprime uma linha por pacote recebido.
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/queue.h>

#ifdef BLE_SCAN
#include "hci.h"
#endif

#ifndef CRIPTO
#define CRIPTO 0
#endif

namespace {

constexpr uint8_t CANAL = 1;
constexpr uint8_t MAC_NO1_STA[6] = {0x88, 0x57, 0x21, 0x70, 0x93, 0x70};
const uint8_t PMK[16] = {'P', 'M', 'K', '_', 'h', 'o', 'r', 't', 'a', '_', 'e', 'x', 'p', '_', '0', '1'};
const uint8_t LMK_OK[16] = {'L', 'M', 'K', '_', 'n', 'o', '1', '_', 'c', 'o', 'r', 'r', 'e', 't', 'a', '_'};

struct Rx {
  uint8_t origem;  // 0 = ESP-NOW, 1 = BLE
  int8_t rssi;
  uint32_t seq;
  uint8_t mac[6];
};
QueueHandle_t fila;
volatile uint32_t descartados = 0, outrosEspnow = 0;

void aoReceberEspnow(const esp_now_recv_info_t* info, const uint8_t* d, int n) {
  if (n != 22 || d[0] != 'H' || d[1] != 'X') {
    outrosEspnow++;
    return;
  }
  Rx r{0, static_cast<int8_t>(info->rx_ctrl->rssi), 0, {}};
  memcpy(&r.seq, &d[2], 4);
  memcpy(r.mac, info->src_addr, 6);
  if (xQueueSend(fila, &r, 0) != pdTRUE) descartados++;
}

#ifdef BLE_SCAN
// Procura o AD de fabricante 0xFFFF com a carga "HX" (no.cpp, MODO_BLE_*).
void aoRelatarBle(const uint8_t* d, uint8_t n, int8_t rssi) {
  for (uint8_t i = 0; i + 1 < n;) {
    uint8_t tam = d[i];
    if (tam == 0 || i + 1 + tam > n) return;
    if (d[i + 1] == 0xFF && tam >= 1 + 2 + 6 && d[i + 2] == 0xFF && d[i + 3] == 0xFF && d[i + 4] == 'H' &&
        d[i + 5] == 'X') {
      Rx r{1, rssi, 0, {}};
      memcpy(&r.seq, &d[i + 6], 4);
      if (xQueueSend(fila, &r, 0) != pdTRUE) descartados++;
      return;
    }
    i += 1 + tam;
  }
}
#endif

uint32_t contEspnow = 0, contBleUnicos = 0, contBleEventos = 0;

// Página que se recarrega a cada 2 s: simula o app consultando a API (tráfego
// no SoftAP durante a recepção ESP-NOW). Mede o tempo de atendimento.
WebServer servidor(80);
uint32_t contHttp = 0;
uint64_t somaHttpUs = 0;
uint32_t maxHttpUs = 0;

void aoPedirPagina() {
  int64_t t0 = esp_timer_get_time();
  char html[200];
  snprintf(html, sizeof(html),
           "<html><head><meta http-equiv=\"refresh\" content=\"2\"></head><body>"
           "espnow=%lu ble=%lu http=%lu</body></html>",
           (unsigned long)contEspnow, (unsigned long)contBleUnicos, (unsigned long)contHttp);
  servidor.send(200, "text/html", html);
  uint32_t dt = static_cast<uint32_t>(esp_timer_get_time() - t0);
  contHttp++;
  somaHttpUs += dt;
  if (dt > maxHttpUs) maxHttpUs = dt;
}
int64_t ultimoSeqBle = -1;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  fila = xQueueCreate(64, sizeof(Rx));
  Serial.printf("\n[BOOT] coord experimento cripto=%d ble_scan=%d heap_livre=%lu\n", CRIPTO,
#ifdef BLE_SCAN
                1,
#else
                0,
#endif
                (unsigned long)ESP.getFreeHeap());

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("horta-exp", "experimento-123", CANAL, 0, 4);
  esp_now_init();
  esp_now_register_recv_cb(aoReceberEspnow);
  if (CRIPTO) {
    esp_now_set_pmk(PMK);
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, MAC_NO1_STA, 6);
    peer.channel = CANAL;
    peer.ifidx = WIFI_IF_AP;
    peer.encrypt = true;
    memcpy(peer.lmk, LMK_OK, 16);
    Serial.printf("[ESPNOW] peer cifrado: %s\n", esp_err_to_name(esp_now_add_peer(&peer)));
  }
  Serial.printf("[WIFI] SoftAP canal %u, MAC AP %s, heap_livre=%lu\n", WiFi.channel(),
                WiFi.softAPmacAddress().c_str(), (unsigned long)ESP.getFreeHeap());

#ifdef BLE_SCAN
  if (hci::iniciar(aoRelatarBle)) {
    // Passiva, intervalo = janela = 60 ms (0x60 × 0,625 ms): escuta o tempo todo
    // que o rádio estiver com o BLE (o resto é do Wi-Fi, por divisão de tempo).
    uint8_t p[7] = {0x00, 0x60, 0x00, 0x60, 0x00, 0x00, 0x00};
    uint8_t s1 = hci::comando(hci::LE_SET_SCAN_PARAM, p, sizeof(p));
    uint8_t e[2] = {0x01, 0x00};  // ligar, sem filtro de duplicatas
    uint8_t s2 = hci::comando(hci::LE_SET_SCAN_ENABLE, e, sizeof(e));
    Serial.printf("[BLE] varredura: param=%u enable=%u heap_livre=%lu\n", s1, s2, (unsigned long)ESP.getFreeHeap());
  }
#endif
  servidor.on("/", aoPedirPagina);
  servidor.begin();
  Serial.println("[RX] origem,seq,rssi");
}

void loop() {
  servidor.handleClient();
  Rx r;
  while (xQueueReceive(fila, &r, pdMS_TO_TICKS(5)) == pdTRUE) {
    if (r.origem == 0) {
      contEspnow++;
      Serial.printf("[RX] espnow,%lu,%d\n", (unsigned long)r.seq, r.rssi);
    } else {
      contBleEventos++;
      if ((int64_t)r.seq != ultimoSeqBle) {  // o mesmo anúncio chega em vários eventos/canais
        ultimoSeqBle = r.seq;
        contBleUnicos++;
        Serial.printf("[RX] ble,%lu,%d\n", (unsigned long)r.seq, r.rssi);
      }
    }
  }
  static uint32_t ultimo = 0;
  if (millis() - ultimo >= 30000) {
    ultimo = millis();
    Serial.printf("[EST] espnow=%lu ble_unicos=%lu ble_eventos=%lu outros_espnow=%lu descartados=%lu clientes_ap=%u "
                  "http=%lu http_medio_us=%lu http_max_us=%lu heap=%lu\n",
                  (unsigned long)contEspnow, (unsigned long)contBleUnicos, (unsigned long)contBleEventos,
                  (unsigned long)outrosEspnow, (unsigned long)descartados, WiFi.softAPgetStationNum(),
                  (unsigned long)contHttp, (unsigned long)(contHttp ? somaHttpUs / contHttp : 0),
                  (unsigned long)maxHttpUs, (unsigned long)ESP.getFreeHeap());
  }
}
