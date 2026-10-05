#include "sensores.h"

#include <Wire.h>

#include "aht20.h"
#include "config.h"
#include "energia.h"

namespace sensores {

namespace {

// Média de AMOSTRAS_ADC leituras calibradas em mV (eFuse, line fitting).
// https://docs.espressif.com/projects/arduino-esp32/en/latest/api/adc.html
float mediaMilivolts(uint8_t pino) {
  uint32_t soma = 0;
  for (int i = 0; i < config::AMOSTRAS_ADC; i++) soma += analogReadMilliVolts(pino);
  return (float)soma / config::AMOSTRAS_ADC;
}

Estado classificar(float valor, float minimo, float maximo) {
  return (valor < minimo || valor > maximo) ? Estado::FORA_DE_FAIXA : Estado::OK;
}

bool ahtIniciado = false;

// millis() em que os sensores foram energizados (para a espera do AHT20).
uint32_t energizadoMs() { return static_cast<uint32_t>(energia::instanteSensoresLigados() / 1000); }

}  // namespace

const char* nomeEstado(Estado e) {
  switch (e) {
    case Estado::OK:            return "ok";
    case Estado::ERRO:          return "erro";
    case Estado::FORA_DE_FAIXA: return "fora_de_faixa";
  }
  return "?";
}

bool iniciar() {
  // Atenuação global, e não por pino: no núcleo 3.3.12, analogSetPinAttenuation()
  // só age em pino já inicializado (1ª leitura), e a calibração em mV é criada
  // com a atenuação global (esp32-hal-adc.c). Os dois pinos usam 11 dB.
  analogReadResolution(12);
  analogSetAttenuation(config::ATENUACAO);

  Wire.begin(config::PINO_SDA, config::PINO_SCL, config::CLOCK_I2C_HZ);
  aht20::definirEspera(energia::esperarMs);  // esperas do AHT20 em light sleep
  aht20::Estado e = aht20::iniciar(Wire, energizadoMs());
  ahtIniciado = (e == aht20::Estado::OK);
  if (!ahtIniciado) Serial.printf("[SENS] AHT20 nao iniciou: %s\n", aht20::nomeEstado(e));
  return ahtIniciado;
}

Leituras lerTodas() {
  Leituras r{};
  uint32_t inicio = micros();

  // Ar (AHT20). Tenta iniciar de novo se falhou antes (sensor reconectado).
  if (!ahtIniciado) ahtIniciado = (aht20::iniciar(Wire, energizadoMs()) == aht20::Estado::OK);
  aht20::Leitura a{};
  aht20::Estado ea = ahtIniciado ? aht20::ler(a) : aht20::Estado::SEM_RESPOSTA;
  r.aht20Extras = a.tentativasExtras;
  if (ea == aht20::Estado::OK) {
    r.temperaturaC = {a.temperaturaC, classificar(a.temperaturaC, config::TEMP_MIN_C, config::TEMP_MAX_C)};
    r.umidadeArPct = {a.umidadePct, classificar(a.umidadePct, config::UR_MIN_PCT, config::UR_MAX_PCT)};
  } else {
    r.temperaturaC = {NAN, Estado::ERRO};
    r.umidadeArPct = {NAN, Estado::ERRO};
  }

  // Solo: a saída estabiliza em 300–400 ms após ligar o sensor (sensores.md 4.5).
  energia::esperarAte(energia::instanteSensoresLigados() + (int64_t)config::ESPERA_SOLO_MS * 1000);

  // Solo. Abaixo do mínimo do ADC não há sinal (sensor desligado ou solto: lê ~0 V).
  float solo = mediaMilivolts(config::PINO_SOLO);
  if (solo < config::ADC_MIN_MV) {
    r.soloMv = {solo, Estado::ERRO};
    r.soloPct = {NAN, Estado::ERRO};
  } else {
    r.soloMv = {solo, classificar(solo, config::ADC_MIN_MV, config::ADC_MAX_MV)};
    // Linear entre os pontos de calibração; a tensão cai quando a umidade sobe.
    float pct = (config::SOLO_MV_SECO - solo) / (config::SOLO_MV_SECO - config::SOLO_MV_UMIDO) * 100.0f;
    r.soloPct = {constrain(pct, 0.0f, 100.0f), classificar(pct, 0.0f, 100.0f)};
    if (r.soloMv.estado != Estado::OK) r.soloPct.estado = r.soloMv.estado;
  }

  // Alimentação: tensão no ponto médio do divisor, convertida para a entrada.
  float div = mediaMilivolts(config::PINO_ALIMENTACAO);
  r.alimentacaoMv = {div / config::RAZAO_DIVISOR, classificar(div, config::ADC_MIN_MV, config::ADC_MAX_MV)};

  r.duracaoUs = micros() - inicio;
  return r;
}

}  // namespace sensores
