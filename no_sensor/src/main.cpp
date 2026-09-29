// Etapa 1 — Passo 2: pisca-LED + mensagem na serial (exemplo; será substituído).
#include <Arduino.h>
#include <esp_timer.h>

// LED_BUILTIN vem do arquivo de variante da placa no Arduino-ESP32
// (variants/doitESP32devkitV1/pins_arduino.h define GPIO2). Confirmar
// observando a placa: é um clone, o LED pode estar em outro pino.
const uint8_t PINO_LED = LED_BUILTIN;
const uint32_t INTERVALO_MS = 500;

// setup(): roda uma única vez, depois do boot, dentro da tarefa loopTask.
void setup() {
  // esp_timer_get_time(): microssegundos desde a inicialização do esp_timer,
  // no começo do app. Não inclui o tempo do ROM nem do bootloader.
  int64_t us_no_setup = esp_timer_get_time();

  Serial.begin(115200);  // precisa bater com monitor_speed no platformio.ini
  pinMode(PINO_LED, OUTPUT);

  Serial.println();
  Serial.printf("[BOOT] setup() iniciou %lld us apos o inicio do app\n", us_no_setup);
  Serial.printf("[BOOT] LED no GPIO%u, piscando a cada %lu ms\n", PINO_LED, INTERVALO_MS);
}

// loop(): chamado repetidamente pela loopTask, para sempre.
void loop() {
  static bool aceso = false;
  aceso = !aceso;
  digitalWrite(PINO_LED, aceso ? HIGH : LOW);
  Serial.printf("[LED] %s  t=%lu ms\n", aceso ? "aceso " : "apagado", millis());
  delay(INTERVALO_MS);  // bloqueia esta tarefa; o FreeRTOS roda outras enquanto isso
}
