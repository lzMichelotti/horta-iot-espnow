// Etapa 1: exemplo do coordenador — imprime os MACs STA/AP e pisca o LED
// (exemplo; será substituído).
#include <Arduino.h>
#include <esp_mac.h>

#include <protocolo.h>

const uint8_t PINO_LED = LED_BUILTIN;  // GPIO2, LED azul (acende em nível alto)
const uint32_t INTERVALO_MS = 500;

// esp_read_mac(): MAC base do eFuse e derivados (STA = base, SoftAP = base + 1).
// Os nós enviarão o ESP-NOW para o MAC AP do coordenador.
// https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/misc_system_api.html
void imprimirMac(const char *nome, esp_mac_type_t tipo) {
  uint8_t mac[6];
  esp_read_mac(mac, tipo);
  Serial.printf("[MAC] %s %02x:%02x:%02x:%02x:%02x:%02x\n", nome,
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_LED, OUTPUT);

  Serial.println();
  Serial.println("[BOOT] coordenador");
  imprimirMac("STA", ESP_MAC_WIFI_STA);
  imprimirMac("AP ", ESP_MAC_WIFI_SOFTAP);
  Serial.printf("[PROTO] versao %u, PacoteLeitura com %u bytes\n", protocolo::VERSAO,
                (unsigned)sizeof(protocolo::PacoteLeitura));
}

void loop() {
  static bool aceso = false;
  aceso = !aceso;
  digitalWrite(PINO_LED, aceso ? HIGH : LOW);
  Serial.printf("[LED] %s  t=%lu ms\n", aceso ? "aceso  " : "apagado", millis());
  delay(INTERVALO_MS);
}
