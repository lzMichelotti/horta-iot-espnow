// Driver do sensor de temperatura e umidade AHT20, escrito a partir do datasheet:
// Aosong/ASAIR, "AHT20 Data Sheet", V1.0, maio/2021.
// https://www.aosong.com/userfiles/files/media/Data%20Sheet%20AHT20.pdf
#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace aht20 {

// Endereço I²C fixo de 7 bits (datasheet 7.3).
constexpr uint8_t ENDERECO = 0x38;

enum class Estado : uint8_t {
  OK,
  SEM_RESPOSTA,   // NACK ou número errado de bytes: sensor ausente, fio solto
  NAO_CALIBRADO,  // bits de calibração do estado diferentes de 0x18 (datasheet 7.4, passo 1)
  TIMEOUT,        // bit de ocupado não voltou a 0 dentro do limite
  ERRO_CRC,       // CRC-8 recebido não confere com o calculado (datasheet 7.4, passo 4)
};

const char* nomeEstado(Estado e);

struct Leitura {
  float temperaturaC;
  float umidadePct;
  uint8_t bruto[7];      // estado, 5 bytes de dados, CRC (figura da pág. 13)
  uint32_t conversaoMs;  // do comando 0xAC até o bit de ocupado ir a 0
  // Consultas de estado que falharam no I²C e foram repetidas. Normalmente 0;
  // 1 indica o estado anômalo descrito em docs/sensores.md (1ª leitura após o
  // 0xAC falha até o sensor ser desligado e religado).
  uint8_t tentativasExtras;
};

// Verifica a presença e a calibração do sensor. Chamar depois de Wire.begin().
Estado iniciar(TwoWire& wire = Wire);

// Dispara uma medição, espera a conversão e lê os 7 bytes.
Estado ler(Leitura& leitura);

// CRC-8 do datasheet: valor inicial 0xFF, polinômio x^8 + x^5 + x^4 + 1 (0x31).
uint8_t crc8(const uint8_t* dados, size_t n);

}  // namespace aht20
