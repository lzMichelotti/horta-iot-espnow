// Firmware do nó sensor — Etapa 2: leitura dos sensores, ainda sem rádio.
// A cada INTERVALO_LEITURA_MS imprime uma linha CSV com as 4 grandezas e seus
// estados (usada pelo teste de estabilidade em ferramentas/).
#include <Arduino.h>

#include "config.h"
#include "sensores.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[BOOT] no sensor - etapa 2");
  sensores::iniciar();
  Serial.println("[CSV] t_ms,temp_c,temp_estado,ur_pct,ur_estado,solo_mv,solo_mv_estado,"
                 "solo_pct,solo_pct_estado,alim_mv,alim_estado,aht20_extras,leitura_us");
}

void loop() {
  static uint32_t proxima = 0;
  if ((int32_t)(millis() - proxima) < 0) return;
  proxima = millis() + config::INTERVALO_LEITURA_MS;

  uint32_t t = millis();
  sensores::Leituras r = sensores::lerTodas();
  Serial.printf("[CSV] %lu,%.2f,%s,%.2f,%s,%.1f,%s,%.1f,%s,%.0f,%s,%u,%lu\n", t,
                r.temperaturaC.valor, sensores::nomeEstado(r.temperaturaC.estado),
                r.umidadeArPct.valor, sensores::nomeEstado(r.umidadeArPct.estado),
                r.soloMv.valor, sensores::nomeEstado(r.soloMv.estado),
                r.soloPct.valor, sensores::nomeEstado(r.soloPct.estado),
                r.alimentacaoMv.valor, sensores::nomeEstado(r.alimentacaoMv.estado),
                r.aht20Extras, r.duracaoUs);
}
