// Etapa 2 — Passo 2: teste do driver do AHT20 (temporário; o firmware de
// leitura dos sensores é organizado no passo 6).
#include <Arduino.h>
#include <Wire.h>

#include "aht20.h"

// Faixa do datasheet: 10 kHz a 400 kHz (4.4). O padrão do Wire é 100 kHz.
const uint32_t CLOCK_I2C_HZ = 100000;
// Intervalo ≥ 1 s para limitar o autoaquecimento a 0,1 °C (datasheet 4.4).
const uint32_t INTERVALO_MS = 2000;

uint32_t total = 0, ok = 0, falhas = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[BOOT] teste AHT20");

  Wire.begin(SDA, SCL, CLOCK_I2C_HZ);
  aht20::Estado e = aht20::iniciar();
  Serial.printf("[AHT20] iniciar: %s\n", aht20::nomeEstado(e));
}

void loop() {
  aht20::Leitura l{};
  aht20::Estado e = aht20::ler(l);
  total++;
  if (e == aht20::Estado::OK) ok++; else falhas++;

  Serial.printf("[AHT20] bytes=%02X %02X %02X %02X %02X %02X %02X crc_calc=%02X conv=%lu ms "
                "extras=%u estado=%s T=%.2f C UR=%.2f %% (ok %lu/%lu)\n",
                l.bruto[0], l.bruto[1], l.bruto[2], l.bruto[3], l.bruto[4], l.bruto[5], l.bruto[6],
                aht20::crc8(l.bruto, 6), l.conversaoMs, l.tentativasExtras, aht20::nomeEstado(e),
                l.temperaturaC, l.umidadePct, ok, total);
  delay(INTERVALO_MS);
}
