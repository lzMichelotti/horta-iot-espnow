// Leitura das grandezas do nó: temperatura e umidade do ar (AHT20), umidade do
// solo (sensor capacitivo) e tensão de alimentação (divisor). Cada leitura
// retorna valor + estado.
#pragma once

#include <Arduino.h>

namespace sensores {

enum class Estado : uint8_t {
  OK,
  ERRO,           // o sensor não respondeu ou o sinal é inválido
  FORA_DE_FAIXA,  // leitura obtida, mas fora da faixa válida definida em config.h
};

const char* nomeEstado(Estado e);

struct Medida {
  float valor;
  Estado estado;
};

struct Leituras {
  Medida temperaturaC;
  Medida umidadeArPct;
  Medida soloMv;          // tensão de saída do sensor de solo
  Medida soloPct;         // índice de 0 a 100 % pela calibração de config.h
  Medida alimentacaoMv;   // tensão na entrada do divisor
  uint8_t aht20Extras;    // consultas repetidas pelo driver (ver aht20.h)
  uint32_t duracaoUs;     // tempo gasto para ler tudo
};

// Configura I²C, ADC e AHT20. Retorna false se o AHT20 não iniciar
// (as leituras seguintes do ar vão dar ERRO; as analógicas continuam).
bool iniciar();

Leituras lerTodas();

}  // namespace sensores
