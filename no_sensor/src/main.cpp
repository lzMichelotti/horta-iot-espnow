// Firmware do nó sensor — Etapa 4: envio do PacoteLeitura por ESP-NOW ao
// coordenador, com espera pelo ACK da camada MAC e retransmissão.
// Ainda sem deep sleep (etapa 5): o loop() simula o ciclo a cada INTERVALO_LEITURA_MS,
// ligando o Wi-Fi do zero e desligando-o depois do envio.
#include <Arduino.h>
#include <esp_timer.h>

#include <protocolo.h>

#include "config.h"
#include "contadores.h"
#include "envio.h"
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
  d.tentativasAnt = contadores::tentativasAnt();
  d.flags = (r.aht20Extras > 0 ? protocolo::flag::AHT20_NOVA_TENTATIVA : 0) |
            (contadores::anteriorSemAck() ? protocolo::flag::ANTERIOR_SEM_ACK : 0);
  return protocolo::montarLeitura(d);
}

void imprimirEnvio(const protocolo::PacoteLeitura& p, const envio::Resultado& r, uint32_t leituraUs,
                   uint32_t cicloUs) {
  Serial.printf("[ENVIO] %u,%lu,%u,%u,%lu", p.boot, (unsigned long)p.seq, r.ack, r.tentativas,
                (unsigned long)r.ligarUs);
  for (uint8_t k = 0; k < config::MAX_ENVIOS; k++)
    if (k < r.tentativas) Serial.printf(",%ld", (long)r.tentativaUs[k]);
    else Serial.print(",");
  Serial.printf(",%lu,%s,%u,%u,%lu,%lu\n", (unsigned long)r.totalUs,
                r.ultimoErro == ESP_OK ? "" : esp_err_to_name(r.ultimoErro), p.tentativas_ant, p.flags,
                (unsigned long)leituraUs, (unsigned long)cicloUs);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[BOOT] no sensor - etapa 4");
  contadores::iniciar();
  Serial.printf("[BOOT] boot=%u motivo=%u protocolo v%u (%u bytes) canal=%u destino=" MACSTR "\n",
                contadores::boot(), contadores::motivoBoot(), protocolo::VERSAO,
                (unsigned)sizeof(protocolo::PacoteLeitura), config::CANAL_WIFI, MAC2STR(config::MAC_COORDENADOR));
  if (config::TESTE_DUPLICATA_A_CADA > 0)
    Serial.printf("[BOOT] MODO DE TESTE: duplicata a cada %lu ciclos\n", (unsigned long)config::TESTE_DUPLICATA_A_CADA);
  sensores::iniciar();
  // t1..tN: µs do esp_now_send ao callback em cada tentativa (−1 = sem callback no prazo).
  Serial.print("[ENVIO] boot,seq,ack,tentativas,ligar_us");
  for (uint8_t k = 1; k <= config::MAX_ENVIOS; k++) Serial.printf(",t%u_us", k);
  Serial.println(",envio_us,erro,tent_ant,flags,leitura_us,ciclo_us");
}

void loop() {
  static uint32_t proxima = 0;
  if ((int32_t)(millis() - proxima) < 0) return;
  proxima = millis() + config::INTERVALO_LEITURA_MS;

  // Ordem do ciclo real: ler os sensores → ligar o rádio → enviar → desligar.
  int64_t inicio = esp_timer_get_time();
  sensores::Leituras leituras = sensores::lerTodas();
  protocolo::PacoteLeitura p = montar(leituras);
  uint8_t bytes[sizeof(p)];
  memcpy(bytes, &p, sizeof(p));

  envio::Resultado r;
  bool ligado = envio::ligar(r);
  if (ligado) envio::enviar(bytes, sizeof(bytes), r);

  // Modo de teste de duplicata: reenvia o pacote atual e o do ciclo anterior.
  static uint8_t anterior[sizeof(bytes)];
  static bool temAnterior = false;
  if (config::TESTE_DUPLICATA_A_CADA > 0 && ligado && r.ack &&
      p.seq % config::TESTE_DUPLICATA_A_CADA == config::TESTE_DUPLICATA_A_CADA - 1) {
    envio::Resultado dup, ant;
    envio::enviar(bytes, sizeof(bytes), dup);
    Serial.printf("[TESTE] reenvio do seq %lu (duplicado): ack=%u\n", (unsigned long)p.seq, dup.ack);
    if (temAnterior) {
      envio::enviar(anterior, sizeof(anterior), ant);
      Serial.printf("[TESTE] reenvio do seq %lu (antigo): ack=%u\n", (unsigned long)(p.seq - 1), ant.ack);
    }
  }
  memcpy(anterior, bytes, sizeof(bytes));
  temAnterior = true;
  envio::desligar();
  contadores::registrarEnvio(r.tentativas, r.ack);

  imprimirEnvio(p, r, leituras.duracaoUs, static_cast<uint32_t>(esp_timer_get_time() - inicio));
}
