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

// ---------------------------------------------------------------- ESP-NOW
// Mesmo canal do SoftAP do coordenador (coordenador/src/config.h): o ESP-NOW
// transmite no canal da interface, e o nó (STA sem associação) fica nele.
#ifdef TESTE_CANAL_WIFI  // teste de canal errado (env teste_canal* do platformio.ini)
constexpr uint8_t CANAL_WIFI = TESTE_CANAL_WIFI;
#else
constexpr uint8_t CANAL_WIFI = 1;
#endif
// Destino: MAC da interface AP do coordenador (docs/placas.md, placa COORD).
constexpr uint8_t MAC_COORDENADOR[6] = {0x88, 0x57, 0x21, 0x70, 0x91, 0xFD};

// Retransmissão na aplicação (docs/comunicacao.md). A camada MAC já repete o
// quadro antes de reportar FAIL; estas tentativas cobrem falhas mais longas.
constexpr uint8_t MAX_ENVIOS = 3;                    // 1 envio + 2 retransmissões
constexpr uint32_t BACKOFF_MS[MAX_ENVIOS - 1] = {10, 30};  // espera antes da 2ª e da 3ª
constexpr uint32_t SORTEIO_MS = 10;                  // + 0..10 ms aleatórios (evita colisões repetidas)
// Espera máxima pelo callback de envio: rede de segurança. O FAIL chegou por
// callback em 28–50 ms em todos os testes de bancada (docs/comunicacao.md, 6.1).
constexpr uint32_t TEMPO_LIMITE_CALLBACK_MS = 100;

// ---------------------------------------------------------------- Ciclo (deep sleep)
// Período entre pacotes: o nó dorme (período − tempo acordado), então o
// intervalo não acumula o tempo acordado. PROVISÓRIO (10 s) para os testes de
// bancada; o intervalo de operação é decisão do passo 6 da Etapa 5.
constexpr uint32_t PERIODO_CICLO_MS = 10000;
constexpr uint32_t SONO_MINIMO_MS = 1000;  // se o ciclo passar do período, dorme ao menos isto

// ---------------------------------------------------------------- Modos de teste (Etapa 4)
// Teste de duplicata (env teste_duplicata): a cada N ciclos, depois do envio
// normal, reenvia o MESMO pacote (o coordenador deve classificá-lo como
// "duplicado") e o pacote do ciclo anterior ("antigo"). 0 = desligado.
#ifdef TESTE_DUPLICATA
constexpr uint32_t TESTE_DUPLICATA_A_CADA = TESTE_DUPLICATA;
#else
constexpr uint32_t TESTE_DUPLICATA_A_CADA = 0;
#endif

}  // namespace config
