// Firmware do nó sensor — Etapa 5: um ciclo por despertar.
// Acordar do deep sleep é um boot (ESP-IDF v5.5, Sleep Modes): o setup() faz o
// ciclo inteiro (ler → ligar rádio → enviar → dormir) e o loop() nunca roda.
// O estado entre ciclos fica na RAM do RTC (contadores.cpp).
#include <Arduino.h>
#include <esp_sleep.h>
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
  d.acordadoAntMs = contadores::acordadoAnt();
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
  Serial.printf(",%lu,%s,%u,%u,%lu,%lu,%u\n", (unsigned long)r.totalUs,
                r.ultimoErro == ESP_OK ? "" : esp_err_to_name(r.ultimoErro), p.tentativas_ant, p.flags,
                (unsigned long)leituraUs, (unsigned long)cicloUs, p.acordado_ant_ms);
}

// Modo de teste de duplicata: reenvia o pacote atual e o do ciclo anterior.
// O anterior fica na RAM do RTC (a RAM comum se perde no deep sleep).
RTC_DATA_ATTR uint8_t pacoteAnterior[sizeof(protocolo::PacoteLeitura)];
RTC_DATA_ATTR bool temAnterior = false;

void testeDuplicata(const protocolo::PacoteLeitura& p, const uint8_t* bytes, const envio::Resultado& r) {
  if (config::TESTE_DUPLICATA_A_CADA > 0 && r.ack &&
      p.seq % config::TESTE_DUPLICATA_A_CADA == config::TESTE_DUPLICATA_A_CADA - 1) {
    envio::Resultado dup, ant;
    envio::enviar(bytes, sizeof(p), dup);
    Serial.printf("[TESTE] reenvio do seq %lu (duplicado): ack=%u\n", (unsigned long)p.seq, dup.ack);
    if (temAnterior) {
      envio::enviar(pacoteAnterior, sizeof(pacoteAnterior), ant);
      Serial.printf("[TESTE] reenvio do seq %lu (antigo): ack=%u\n", (unsigned long)(p.seq - 1), ant.ack);
    }
  }
  memcpy(pacoteAnterior, bytes, sizeof(pacoteAnterior));
  temAnterior = true;
}

// Dorme até completar o período do ciclo. esp_deep_sleep_start() "will flush
// the contents of UART FIFOs" e não retorna (ESP-IDF v5.5, Sleep Modes); o
// Serial.flush() esvazia antes o buffer do driver do Arduino.
[[noreturn]] void dormir(uint32_t acordadoUs) {
  uint64_t periodoUs = (uint64_t)config::PERIODO_CICLO_MS * 1000;
  uint64_t sonoUs = acordadoUs + (uint64_t)config::SONO_MINIMO_MS * 1000 < periodoUs
                        ? periodoUs - acordadoUs
                        : (uint64_t)config::SONO_MINIMO_MS * 1000;
  esp_sleep_enable_timer_wakeup(sonoUs);
  Serial.flush();
  esp_deep_sleep_start();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  contadores::iniciar();
  if (contadores::motivoBoot() != ESP_RST_DEEPSLEEP) {
    // Só no boot "de verdade": no despertar, o cabeçalho seria repetido a cada ciclo.
    Serial.println();
    Serial.println("[BOOT] no sensor - etapa 5");
    Serial.printf("[BOOT] boot=%u motivo=%u protocolo v%u (%u bytes) canal=%u destino=" MACSTR " periodo=%lu ms\n",
                  contadores::boot(), contadores::motivoBoot(), protocolo::VERSAO,
                  (unsigned)sizeof(protocolo::PacoteLeitura), config::CANAL_WIFI, MAC2STR(config::MAC_COORDENADOR),
                  (unsigned long)config::PERIODO_CICLO_MS);
    if (config::TESTE_DUPLICATA_A_CADA > 0)
      Serial.printf("[BOOT] MODO DE TESTE: duplicata a cada %lu ciclos\n",
                    (unsigned long)config::TESTE_DUPLICATA_A_CADA);
    // t1..tN: µs do esp_now_send ao callback em cada tentativa (−1 = sem callback no prazo).
    Serial.print("[ENVIO] boot,seq,ack,tentativas,ligar_us");
    for (uint8_t k = 1; k <= config::MAX_ENVIOS; k++) Serial.printf(",t%u_us", k);
    Serial.println(",envio_us,erro,tent_ant,flags,leitura_us,ciclo_us,acordado_ant_ms");
  }

  // Ordem do ciclo: ler os sensores → ligar o rádio → enviar → desligar → dormir.
  sensores::iniciar();
  sensores::Leituras leituras = sensores::lerTodas();
  protocolo::PacoteLeitura p = montar(leituras);
  uint8_t bytes[sizeof(p)];
  memcpy(bytes, &p, sizeof(p));

  envio::Resultado r;
  if (envio::ligar(r)) {
    envio::enviar(bytes, sizeof(bytes), r);
    testeDuplicata(p, bytes, r);
  }
  envio::desligar();
  contadores::registrarEnvio(r.tentativas, r.ack);

  // esp_timer conta desde o início da aplicação: o tempo de ROM e bootloader
  // antes disso não entra (medido pelo PC, docs/energia.md).
  uint32_t acordadoUs = static_cast<uint32_t>(esp_timer_get_time());
  imprimirEnvio(p, r, leituras.duracaoUs, acordadoUs);
  contadores::registrarAcordado((static_cast<uint32_t>(esp_timer_get_time()) + 500) / 1000);
  dormir(static_cast<uint32_t>(esp_timer_get_time()));
}

void loop() {}  // nunca alcançado: o setup() termina em deep sleep
