// Firmware de DIAGNÓSTICO: corrente do sensor de solo (não é o firmware de produção).
// Ambiente: pio run -e diag_corrente -t upload   (no_sensor/platformio.ini)
//
// Método (sem multímetro): um resistor "shunt" do kit fica no fio de GND do
// sensor de solo. A corrente do sensor passa por ele e cria uma tensão
// V = I × R, lida no GPIO32 (ADC1_CH4). Corrente = (V − zero) / R.
//
//   3V3 ───────────── VCC do sensor
//                     GND do sensor ──┬── GPIO32
//                                     R (shunt)
//   GND ──────────────────────────────┘
//
// ADC a 0 dB: faixa efetiva de 100 a 950 mV, erro de ±23 mV após a calibração
// de fábrica (ESP32 Series Datasheet v5.3, tab. 4-4). Abaixo de 100 mV o
// resultado não é confiável: trocar por um resistor maior.
// API: analogSetAttenuation() global e analogReadMilliVolts() (Arduino-ESP32 3.3.12,
// https://docs.espressif.com/projects/arduino-esp32/en/latest/api/adc.html).
//
// Comandos (uma letra + Enter):
//   1  shunt de 100 Ω     4  shunt de 470 Ω     k  shunt de 1 kΩ
//   z  zero: medir com o fio VCC do sensor DESCONECTADO (guarda o desvio do ADC)
//   m  medição de 30 s com resumo (média, mínimo, máximo)
//   a  alterna a escala: fina (0 dB, padrão) ou larga (11 dB, 150–2450 mV, ±60 mV)
//   t  lê o AHT20 (confere o 3V3 compartilhado com o sensor de solo)
//   h  ajuda
// Sem comando, imprime uma linha por segundo.
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>

#include "aht20.h"
#include "config.h"

namespace {

constexpr uint8_t PINO_SHUNT = 32;  // ADC1_CH4, livre no nó (docs/pinagem.md)
constexpr int AMOSTRAS = 64;        // mesma média da produção (config.h)
constexpr float FAIXA_MIN_MV = 100.0f;
constexpr float FAIXA_MAX_MV = 950.0f;

float shuntOhm = 100.0f;
float zeroMv = 0.0f;
bool escalaLarga = false;  // 'a': 11 dB (150–2450 mV, ±60 mV) para diagnosticar leitura saturada
float faixaMin() { return escalaLarga ? 150.0f : FAIXA_MIN_MV; }
float faixaMax() { return escalaLarga ? 2450.0f : FAIXA_MAX_MV; }

float lerMv() {
  uint32_t soma = 0;
  for (int i = 0; i < AMOSTRAS; i++) soma += analogReadMilliVolts(PINO_SHUNT);
  return (float)soma / AMOSTRAS;
}

const char* aviso(float mv) {
  if (mv < faixaMin()) return "abaixo da faixa: usar resistor maior";
  if (mv > faixaMax()) return "acima da faixa: usar resistor menor ou escala larga (a)";
  return "";
}

void linha(float mv) {
  float liquido = mv - zeroMv;
  Serial.printf("%lu,%.0f,%.1f,%.1f,%.3f,%s\n", (unsigned long)millis(), shuntOhm, mv, liquido,
                liquido / shuntOhm, aviso(mv));
}

void ajuda() {
  Serial.println("# diag_corrente: 1 (100 ohm), 4 (470 ohm), k (1 kohm), z (zero), m (30 s), h (ajuda)");
  Serial.printf("# shunt=%.0f ohm zero=%.1f mV | colunas: tempo_ms,shunt_ohm,mv,mv_liquido,corrente_ma,aviso\n",
                shuntOhm, zeroMv);
}

// Confere a montagem: o AHT20 divide o 3V3 com o sensor de solo.
void testarAht20() {
  aht20::Estado e = aht20::iniciar(Wire, 0);
  if (e != aht20::Estado::OK) {
    Serial.printf("# AHT20: %s\n", aht20::nomeEstado(e));
    return;
  }
  aht20::Leitura l;
  e = aht20::ler(l);
  Serial.printf("# AHT20: %s temperatura=%.2f C umidade=%.1f %%\n", aht20::nomeEstado(e), l.temperaturaC,
                l.umidadePct);
}

void zerar() {
  Serial.println("# Zero: o fio VCC do sensor deve estar DESCONECTADO. Medindo 5 s...");
  float soma = 0;
  for (int i = 0; i < 5; i++) { soma += lerMv(); delay(1000); }
  zeroMv = soma / 5;
  Serial.printf("# zero=%.1f mV. Reconecte o VCC do sensor.\n", zeroMv);
}

void medir30s() {
  Serial.printf("# Medicao de 30 s, shunt=%.0f ohm\n", shuntOhm);
  float soma = 0, mn = 1e9, mx = -1e9, somaMv = 0;
  for (int i = 0; i < 30; i++) {
    float mv = lerMv();
    float ma = (mv - zeroMv) / shuntOhm;
    linha(mv);
    soma += ma;
    somaMv += mv;
    mn = min(mn, ma);
    mx = max(mx, ma);
    delay(1000);
  }
  float mediaMv = somaMv / 30;
  // ±23 mV do ADC (tab. 4-4) e ±5 % do resistor
  float incerteza = (escalaLarga ? 60.0f : 23.0f) / shuntOhm + 0.05f * (soma / 30);
  Serial.printf("# RESULTADO: corrente media=%.3f mA (min %.3f, max %.3f), +/- %.3f mA | shunt=%.0f ohm, %.0f mV %s\n",
                soma / 30, mn, mx, incerteza, shuntOhm, mediaMv, aviso(mediaMv));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  WiFi.mode(WIFI_OFF);
  analogReadResolution(12);
  analogSetAttenuation(ADC_0db);  // global; faixa de 100–950 mV
  Wire.begin(config::PINO_SDA, config::PINO_SCL, config::CLOCK_I2C_HZ);
  Serial.printf("# diag_corrente | Arduino-ESP32 %d.%d.%d | shunt no GPIO%u\n", ESP_ARDUINO_VERSION_MAJOR,
                ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH, PINO_SHUNT);
  ajuda();
}

void loop() {
  static uint32_t ultimo = 0;
  if (Serial.available()) {
    switch (Serial.read()) {
      case '1': shuntOhm = 100; Serial.println("# shunt=100 ohm"); break;
      case '4': shuntOhm = 470; Serial.println("# shunt=470 ohm"); break;
      case 'k': shuntOhm = 1000; Serial.println("# shunt=1000 ohm"); break;
      case 'z': zerar(); break;
      case 'm': medir30s(); break;
      case 't': testarAht20(); break;
      case 's': {  // confere a montagem: D32 (shunt) e D34 (AOUT) devem ser diferentes
        analogSetAttenuation(ADC_11db);
        uint32_t s32 = 0, s34 = 0;
        for (int i = 0; i < AMOSTRAS; i++) { s32 += analogReadMilliVolts(PINO_SHUNT); s34 += analogReadMilliVolts(config::PINO_SOLO); }
        analogSetAttenuation(escalaLarga ? ADC_11db : ADC_0db);
        Serial.printf("# 11 dB: D32 (shunt)=%.0f mV  D34 (AOUT do solo)=%.0f mV\n", (float)s32 / AMOSTRAS,
                      (float)s34 / AMOSTRAS);
        break;
      }
      case 'a':
        escalaLarga = !escalaLarga;
        analogSetAttenuation(escalaLarga ? ADC_11db : ADC_0db);
        Serial.printf("# escala %s\n", escalaLarga ? "larga: 11 dB, 150-2450 mV, +/-60 mV" : "fina: 0 dB, 100-950 mV, +/-23 mV");
        break;
      case 'h': ajuda(); break;
      default: break;
    }
  }
  if (millis() - ultimo >= 1000) {
    ultimo = millis();
    linha(lerMv());
  }
}
