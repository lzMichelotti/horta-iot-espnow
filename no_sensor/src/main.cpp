// Etapa 1 — Passos 4 e 5: lê os MACs STA/AP, pisca o LED e imprime o tempo
// desde o boot (exemplo; será substituído).
#include <Arduino.h>
#include <esp_mac.h>
#include <esp_timer.h>

const uint8_t PINO_LED = LED_BUILTIN;  // GPIO2, LED azul (acende em nível alto)
const uint32_t INTERVALO_MS = 500;

// esp_read_mac(): lê o MAC base gravado em eFuse e deriva o da interface
// (STA = base, SoftAP = base + 1). Não precisa ligar o Wi-Fi.
// https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/misc_system_api.html
void imprimirMac(const char *nome, esp_mac_type_t tipo) {
  uint8_t mac[6];
  esp_read_mac(mac, tipo);
  Serial.printf("[MAC] %s %02x:%02x:%02x:%02x:%02x:%02x\n", nome,
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void setup() {
  // Microssegundos desde o início do app (não inclui ROM nem bootloader).
  int64_t us_no_setup = esp_timer_get_time();

  Serial.begin(115200);
  pinMode(PINO_LED, OUTPUT);

  Serial.println();
  Serial.printf("[BOOT] setup() iniciou %lld us apos o inicio do app\n", us_no_setup);
  imprimirMac("STA", ESP_MAC_WIFI_STA);
  imprimirMac("AP ", ESP_MAC_WIFI_SOFTAP);
}

void loop() {
  static bool aceso = false;
  aceso = !aceso;
  digitalWrite(PINO_LED, aceso ? HIGH : LOW);
  Serial.printf("[LED] %s  t=%lu ms\n", aceso ? "aceso  " : "apagado", millis());
  delay(INTERVALO_MS);
}
