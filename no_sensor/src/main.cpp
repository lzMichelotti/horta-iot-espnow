// Etapa 0: só valida a gravação e a serial. Sem lógica do sistema.
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("[BOOT] hello do no sensor");
  Serial.printf("[BOOT] ESP-IDF %s\n", esp_get_idf_version());
}

void loop() {
  Serial.printf("[BOOT] vivo ha %lu ms\n", millis());
  delay(2000);
}
