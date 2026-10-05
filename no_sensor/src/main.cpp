// Firmware do nó sensor — Etapa 3: leitura dos sensores e montagem do pacote,
// ainda sem rádio. A cada INTERVALO_LEITURA_MS imprime a linha CSV das
// leituras e o pacote montado em hexadecimal (para conferir com docs/protocolo.md).
#include <Arduino.h>

#include <protocolo.h>

#include "config.h"
#include "contadores.h"
#include "sensores.h"

namespace {

protocolo::Estado converter(sensores::Estado e) {
  switch (e) {
    case sensores::Estado::OK:            return protocolo::Estado::OK;
    case sensores::Estado::FORA_DE_FAIXA: return protocolo::Estado::FORA_DE_FAIXA;
    case sensores::Estado::ERRO:          return protocolo::Estado::ERRO;
  }
  return protocolo::Estado::ERRO;
}

protocolo::PacoteLeitura montar(const sensores::Leituras& r) {
  protocolo::DadosLeitura d{};
  d.boot = contadores::boot();
  d.seq = contadores::proximoSeq();
  d.temperaturaC = r.temperaturaC.valor;
  d.estadoTemperatura = converter(r.temperaturaC.estado);
  d.umidadeArPct = r.umidadeArPct.valor;
  d.estadoUmidadeAr = converter(r.umidadeArPct.estado);
  d.soloMv = r.soloMv.valor;
  d.estadoSolo = converter(r.soloMv.estado);
  d.alimentacaoMv = r.alimentacaoMv.valor;
  d.estadoAlimentacao = converter(r.alimentacaoMv.estado);
  d.motivoBoot = contadores::motivoBoot();
  d.acordadoAntMs = 0;  // sem deep sleep ainda (etapa 5): desconhecido
  d.tentativasAnt = 0;  // sem rádio ainda (etapa 4): desconhecido
  d.flags = r.aht20Extras > 0 ? protocolo::flag::AHT20_NOVA_TENTATIVA : 0;
  return protocolo::montarLeitura(d);
}

void imprimirPacote(const protocolo::PacoteLeitura& p) {
  uint8_t bytes[sizeof(p)];
  memcpy(bytes, &p, sizeof(p));
  Serial.printf("[PKT] %u bytes:", (unsigned)sizeof(p));
  for (uint8_t b : bytes) Serial.printf(" %02X", b);
  Serial.println();

  // Autoverificação: o pacote montado tem de passar na validação do coordenador.
  protocolo::PacoteLeitura v;
  protocolo::Rejeicao rej = protocolo::validar(bytes, sizeof(bytes), v);
  Serial.printf("[PKT] boot=%u seq=%lu T=%d UR=%u solo=%u mV alim=%u mV estados=0x%02X "
                "motivo_boot=%u flags=0x%02X validacao=%s\n",
                v.boot, (unsigned long)v.seq, v.temperatura_c100, v.umidade_ar_c100, v.solo_mv,
                v.alimentacao_mv, v.estados, v.motivo_boot, v.flags, protocolo::nomeRejeicao(rej));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[BOOT] no sensor - etapa 3");
  contadores::iniciar();
  Serial.printf("[BOOT] boot=%u motivo=%u protocolo v%u (%u bytes)\n", contadores::boot(),
                contadores::motivoBoot(), protocolo::VERSAO, (unsigned)sizeof(protocolo::PacoteLeitura));
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
  imprimirPacote(montar(r));
}
