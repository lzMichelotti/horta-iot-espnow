// Varre os endereços I²C e mede o nível de repouso de SDA/SCL.
// Mesmos pinos e clock do nó (no_sensor/src/config.h): SDA 21, SCL 22, 100 kHz.
// Wire do Arduino-ESP32 3.x: https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2c.html
#include <Arduino.h>
#include <Wire.h>

constexpr uint8_t SDA_PINO = 21;
constexpr uint8_t SCL_PINO = 22;

// Com o pull-down interno (~45 kΩ) ligado, a linha só fica em 1 se houver um
// pull-up externo alimentado (o do módulo AHT20, ligado ao 3V3).
void niveisDeRepouso() {
  pinMode(SDA_PINO, INPUT_PULLDOWN);
  pinMode(SCL_PINO, INPUT_PULLDOWN);
  delay(5);
  int sda = digitalRead(SDA_PINO), scl = digitalRead(SCL_PINO);
  Serial.printf("[I2C] repouso com pull-down interno: SDA=%d SCL=%d (%s)\n", sda, scl,
                sda && scl ? "ha pull-up externo alimentado" : "sem pull-up: modulo sem 3V3/GND ou fio solto");
}

void setup() {
  Serial.begin(115200);
  delay(300);
}

int varrer(uint8_t sda, uint8_t scl) {
  Wire.begin(sda, scl, 100000);
  int achados = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C] SDA=GPIO%d SCL=GPIO%d: responde em 0x%02X%s\n", sda, scl, a, a == 0x38 ? " (AHT20)" : "");
      achados++;
    }
  }
  Wire.end();
  return achados;
}

// Medição completa do AHT20 (datasheet, seção 7): comando 0xAC 0x33 0x00, espera
// de 80 ms e leitura de 7 bytes (estado, 20 bits de umidade, 20 de temperatura, CRC).
void medirAht20() {
  Wire.begin(SDA_PINO, SCL_PINO, 100000);
  Wire.beginTransmission(0x38);
  Wire.write(0xAC); Wire.write(0x33); Wire.write(0x00);
  uint8_t erroEnvio = Wire.endTransmission();
  delay(80);
  uint8_t d[7] = {0};
  size_t lidos = Wire.requestFrom((uint8_t)0x38, (uint8_t)7);
  for (size_t i = 0; i < lidos && i < 7; i++) d[i] = Wire.read();
  Wire.end();
  if (erroEnvio != 0 || lidos != 7) {
    Serial.printf("[I2C] medicao AHT20: FALHOU (envio=%u, bytes lidos=%u)\n", erroEnvio, (unsigned)lidos);
    return;
  }
  uint32_t ur = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
  uint32_t t = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
  Serial.printf("[I2C] medicao AHT20: estado=0x%02X temperatura=%.2f C umidade=%.2f %%\n", d[0],
                t * 200.0 / 1048576.0 - 50.0, ur * 100.0 / 1048576.0);
}

void loop() {
  niveisDeRepouso();
  // As duas ordens: se só a trocada responder, os fios SDA e SCL estão invertidos.
  int certa = varrer(SDA_PINO, SCL_PINO);
  int trocada = varrer(SCL_PINO, SDA_PINO);
  if (certa > 0) medirAht20();
  Serial.printf("[I2C] varredura: %d na ordem certa (SDA=21, SCL=22), %d com os fios trocados\n", certa, trocada);
  delay(3000);
}
