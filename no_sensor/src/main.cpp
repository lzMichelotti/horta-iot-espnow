// Etapa 1 — Passo 5: tarefa de comparação C++ × MicroPython (exemplo; será
// substituído). Mesma sequência de coordenador/src/main.py:
//   primeira linha → memória → liga Wi-Fi STA e lê o MAC → memória → pisca LED.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <esp_timer.h>

const uint8_t PINO_LED = LED_BUILTIN;  // GPIO2, LED azul (acende em nível alto)
const uint32_t INTERVALO_MS = 500;

// Heap livre do ESP-IDF acessível byte a byte (toda a DRAM livre).
// https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/mem_alloc.html
size_t heapLivre() { return heap_caps_get_free_size(MALLOC_CAP_8BIT); }

void setup() {
  Serial.begin(115200);
  Serial.println();
  // O script ferramentas/medir_boot.py mede do reset até esta linha.
  Serial.println("[BOOT] primeira linha");

  // Contador interno: não inclui ROM nem bootloader; só informativo.
  Serial.printf("[TEMPO] contador interno: %lld us\n", esp_timer_get_time());
  Serial.printf("[MEM] heap livre antes do Wi-Fi: %u bytes\n", heapLivre());

  int64_t t0 = esp_timer_get_time();
  WiFi.mode(WIFI_STA);  // inicia o driver Wi-Fi em modo estação
  // MAC lido direto do driver (WiFi.macAddress() logo após mode() devolveu zeros).
  // https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/network/esp_wifi.html
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  int64_t dt = esp_timer_get_time() - t0;
  Serial.printf("[WIFI] STA ativa e MAC lido em %lld us: %02x:%02x:%02x:%02x:%02x:%02x\n", dt,
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("[MEM] heap livre depois do Wi-Fi: %u bytes\n", heapLivre());

  pinMode(PINO_LED, OUTPUT);
}

void loop() {
  static bool aceso = false;
  aceso = !aceso;
  digitalWrite(PINO_LED, aceso ? HIGH : LOW);
  Serial.printf("[LED] %s  t=%lu ms\n", aceso ? "aceso  " : "apagado", millis());
  delay(INTERVALO_MS);
}
