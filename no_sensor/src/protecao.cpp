#include "protecao.h"

#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include "config.h"
#include "contadores.h"
#include "energia.h"

namespace protecao {

namespace {

RTC_DATA_ATTR bool abortadoRtc = false;
RTC_DATA_ATTR Modo modoRtc = Modo::NORMAL;
RTC_DATA_ATTR uint16_t rampaIndice = 0;  // só no teste de tensão simulada
bool anteriorAbortado = false;
esp_timer_handle_t prazo = nullptr;

// Callback do prazo: roda na tarefa do esp_timer (CPU 0), mesmo com o setup()
// (loopTask, CPU 1) preso num laço. Apaga o que gasta energia e dorme o período
// normal. esp_wifi_stop() é exigido antes do deep sleep (ESP-IDF v5.5, Sleep
// Modes); se o Wi-Fi não estava ligado, o erro é ignorado.
void estourouPrazo(void*) {
  abortadoRtc = true;
  contadores::registrarAcordado(config::TEMPO_MAX_ACORDADO_MS);
  contadores::registrarEnvio(0, true);  // tentativas desconhecidas; sem ANTERIOR_SEM_ACK
  energia::desligarSensores();
  esp_wifi_stop();
  esp_sleep_enable_timer_wakeup((uint64_t)(config::PERIODO_CICLO_MS - config::TEMPO_MAX_ACORDADO_MS) * 1000);
  esp_deep_sleep_start();
}

#ifdef TESTE_RAMPA_ALIM
// Teste: tensão simulada que desce de 3700 a 3100 mV e volta, 50 mV por ciclo.
float tensaoSimulada() {
  constexpr int PASSOS = 12;  // (3700 − 3100) / 50
  int i = rampaIndice++ % (2 * PASSOS);
  return 3700.0f - 50.0f * (i <= PASSOS ? i : 2 * PASSOS - i);
}
#endif

}  // namespace

void iniciar(bool despertarDoSono) {
  if (!despertarDoSono) {
    abortadoRtc = false;
    modoRtc = Modo::NORMAL;
    rampaIndice = 0;
  }
  anteriorAbortado = abortadoRtc;
  abortadoRtc = false;

  const esp_timer_create_args_t args = {.callback = estourouPrazo, .name = "prazo"};
  if (prazo == nullptr) esp_timer_create(&args, &prazo);
  esp_timer_start_once(prazo, (uint64_t)config::TEMPO_MAX_ACORDADO_MS * 1000);
}

void cancelarPrazo() {
  if (prazo != nullptr) esp_timer_stop(prazo);
}

bool cicloAnteriorAbortado() { return anteriorAbortado; }

const char* nomeModo(Modo m) {
  switch (m) {
    case Modo::NORMAL:   return "normal";
    case Modo::ECONOMIA: return "economia";
    case Modo::CRITICO:  return "critico";
  }
  return "?";
}

Modo avaliarTensao(sensores::Medida& alimentacao) {
#ifdef TESTE_RAMPA_ALIM
  alimentacao = {tensaoSimulada(), sensores::Estado::OK};
#endif
  if (config::V_TX_MV == 0 || alimentacao.estado == sensores::Estado::ERRO) return modoRtc;
  float v = alimentacao.valor;
  // Histerese: só sai de um modo de proteção com a tensão HISTERESE_MV acima do limiar.
  switch (modoRtc) {
    case Modo::NORMAL:
      if (v < config::V_CRIT_MV) modoRtc = Modo::CRITICO;
      else if (v < config::V_TX_MV) modoRtc = Modo::ECONOMIA;
      break;
    case Modo::ECONOMIA:
      if (v < config::V_CRIT_MV) modoRtc = Modo::CRITICO;
      else if (v >= config::V_TX_MV + config::HISTERESE_MV) modoRtc = Modo::NORMAL;
      break;
    case Modo::CRITICO:
      if (v >= config::V_TX_MV + config::HISTERESE_MV) modoRtc = Modo::NORMAL;
      else if (v >= config::V_CRIT_MV + config::HISTERESE_MV) modoRtc = Modo::ECONOMIA;
      break;
  }
  return modoRtc;
}

uint32_t periodoMs(Modo m) {
  switch (m) {
    case Modo::ECONOMIA: return config::PERIODO_CICLO_MS * config::FATOR_PERIODO_ECONOMIA;
    case Modo::CRITICO:  return config::PERIODO_CRITICO_MS;
    default:             return config::PERIODO_CICLO_MS;
  }
}

}  // namespace protecao
