// Configuração do nó sensor: pinos, constantes de leitura, calibração e faixas válidas.
// Justificativas e medições em docs/sensores.md; ligações em docs/pinagem.md.
#pragma once

#include <Arduino.h>

namespace config {

// ---------------------------------------------------------------- Pinos (NÓ 1)
constexpr uint8_t PINO_SDA = 21;           // Wire padrão da variante doitESP32devkitV1
constexpr uint8_t PINO_SCL = 22;
constexpr uint8_t PINO_SOLO = 34;          // ADC1_CH6, saída AOUT do sensor de solo
constexpr uint8_t PINO_ALIMENTACAO = 35;   // ADC1_CH7, ponto médio do divisor

// ---------------------------------------------------------------- I²C / AHT20
// Datasheet do AHT20 (4.4): clock entre 10 e 400 kHz; ≥ 1 s entre medições.
constexpr uint32_t CLOCK_I2C_HZ = 100000;

// ---------------------------------------------------------------- ADC
// Só ADC1 (o ADC2 é compartilhado com o Wi-Fi). 11 dB: faixa efetiva de
// 150 a 2450 mV, erro de ±60 mV após calibração (ESP32 Datasheet v5.3, tab. 4-4).
constexpr adc_attenuation_t ATENUACAO = ADC_11db;
constexpr int AMOSTRAS_ADC = 64;           // média: ruído de 23 mV → ~2 mV
constexpr float ADC_MIN_MV = 150.0f;
constexpr float ADC_MAX_MV = 2450.0f;

// ---------------------------------------------------------------- Divisor de tensão
// R1 = R2 = 100 kΩ (5 %). Razão R2/(R1+R2) medida pela troca dos resistores
// (V1 = 1696,0 mV, V2 = 1691,2 mV). Trocar ao mudar os resistores (etapa 5).
constexpr float RAZAO_DIVISOR = 0.5007f;

// ---------------------------------------------------------------- Umidade do solo
// Índice relativo de 0 a 100 %, linear entre dois pontos medidos com o sensor
// alimentado pelo 3V3 da placa (~3,38 V). VALORES DE BANCADA (vaso, 30/09/2026):
// recalibrar no solo da horta seguindo o procedimento de docs/sensores.md.
constexpr float SOLO_MV_SECO = 1400.0f;    // 0 %: terra seca ao toque, acomodada
constexpr float SOLO_MV_UMIDO = 655.0f;    // 100 %: ~1 h após a rega

// ---------------------------------------------------------------- Faixas válidas
constexpr float TEMP_MIN_C = -40.0f;       // AHT20, tabela 2
constexpr float TEMP_MAX_C = 85.0f;
constexpr float UR_MIN_PCT = 0.0f;         // AHT20, tabela 1
constexpr float UR_MAX_PCT = 100.0f;

// ---------------------------------------------------------------- Ciclo
constexpr uint32_t INTERVALO_LEITURA_MS = 2000;

}  // namespace config
