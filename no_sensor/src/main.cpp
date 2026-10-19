// Firmware do nó sensor — Etapa 5: um ciclo por despertar.
// Acordar do deep sleep é um boot (ESP-IDF v5.5, Sleep Modes): o setup() faz o
// ciclo inteiro (ler → ligar rádio → enviar → dormir) e o loop() nunca roda.
// O estado entre ciclos fica na RAM do RTC (contadores.cpp).
#include <Arduino.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include <fila.h>
#include <protocolo.h>

#include "config.h"
#include "contadores.h"
#include "energia.h"
#include "envio.h"
#include "protecao.h"
#include "reenvio.h"
#include "segredos.h"
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
            (contadores::anteriorSemAck() ? protocolo::flag::ANTERIOR_SEM_ACK : 0) |
            (protecao::cicloAnteriorAbortado() ? protocolo::flag::CICLO_ANTERIOR_ABORTADO : 0);
  return protocolo::montarLeitura(d);
}

void imprimirEnvio(const protocolo::PacoteLeitura& p, const reenvio::Resultado& rc, uint32_t ligarUs,
                   uint32_t leituraUs, uint32_t cicloUs) {
  const envio::Resultado& r = rc.nova;
  Serial.printf("[ENVIO] %u,%lu,%u,%u,%lu", p.boot, (unsigned long)p.seq, r.ack, r.tentativas,
                (unsigned long)ligarUs);
  for (uint8_t k = 0; k < config::MAX_ENVIOS; k++)
    if (k < r.tentativas) Serial.printf(",%ld", (long)r.tentativaUs[k]);
    else Serial.print(",");
  Serial.printf(",%lu,%s,%u,%u,%lu,%lu,%u", (unsigned long)rc.totalUs,
                r.ultimoErro == ESP_OK ? "" : esp_err_to_name(r.ultimoErro), p.tentativas_ant, p.flags,
                (unsigned long)leituraUs, (unsigned long)cicloUs, p.acordado_ant_ms);
  // Etapa 7.1: confirmação e fila.
  if (!rc.confirmou) Serial.print(",,");
  else if (rc.nadaGravado) Serial.printf(",nada,%ld", (long)rc.respostaUs);
  else Serial.printf(",%u:%lu,%ld", rc.confBoot, (unsigned long)rc.confSeq, (long)rc.respostaUs);
  Serial.printf(",%u,%u,%u,%u,%u,%u,%lu\n", rc.recusadas, rc.filaAntes, rc.liberadas, rc.reenviadas,
                rc.reenviadasAck, rc.filaDepois, (unsigned long)reenvio::descartadas());
}

// Modo de teste de duplicata (env teste_duplicata): a cada N ciclos, depois do
// envio normal, repete a leitura nova. O coordenador deve classificá-la como
// "duplicado", não gravar e responder com a confirmação.
void testeDuplicata(const protocolo::PacoteLeitura& p, const reenvio::Resultado& rc) {
#ifdef TESTE_DUPLICATA
  if (rc.nova.ack && p.seq % config::TESTE_DUPLICATA_A_CADA == config::TESTE_DUPLICATA_A_CADA - 1) {
    envio::Resultado dup;
    bool conf = reenvio::repetirNova(dup);
    Serial.printf("[TESTE] repeticao do seq %lu (duplicado): ack=%u confirmacao=%u\n", (unsigned long)p.seq,
                  dup.ack, conf);
  }
#else
  (void)p;
  (void)rc;
#endif
}

// ---------------------------------------------------------------- Fases do ciclo
// Instantes do esp_timer (µs desde o início da aplicação) em cada fronteira.
// O que vem antes da aplicação (ROM e bootloader) é medido pelo PC.
enum Fase : uint8_t { SETUP, SERIAL_NVS, SENS_INICIAR, LEITURA, LIGAR, ENVIO, DESLIGAR, IMPRESSAO, NUM_FASES };
int64_t marcas[NUM_FASES];
void marcar(Fase f) { marcas[f] = esp_timer_get_time(); }

// Tempo de Serial.flush() do ciclo anterior: acontece depois da última medição.
RTC_DATA_ATTR uint32_t flushAntUs = 0;

uint8_t modoCiclo = 0;  // protecao::Modo deste ciclo, para a linha [FASES]

void imprimirFases() {
  // Cada fase = da marca anterior até a sua (SETUP = da aplicação até o setup()).
  Serial.printf("[FASES] %lld", marcas[SETUP]);
  for (uint8_t f = SERIAL_NVS; f < NUM_FASES; f++) Serial.printf(",%lld", marcas[f] - marcas[f - 1]);
  Serial.printf(",%lu,%lld,%lu,%u,%u\n", (unsigned long)flushAntUs, marcas[IMPRESSAO],
                (unsigned long)energia::lightSleepUs(), energia::lightSleeps(), modoCiclo);
}

// Dorme até completar o período do ciclo. esp_deep_sleep_start() "will flush
// the contents of UART FIFOs" e não retorna (ESP-IDF v5.5, Sleep Modes); o
// Serial.flush() esvazia antes o buffer do driver do Arduino.
[[noreturn]] void dormir(uint32_t acordadoUs, uint32_t periodoMs) {
  protecao::cancelarPrazo();
  uint64_t periodoUs = (uint64_t)periodoMs * 1000;
  uint64_t sonoUs = acordadoUs + (uint64_t)config::SONO_MINIMO_MS * 1000 < periodoUs
                        ? periodoUs - acordadoUs
                        : (uint64_t)config::SONO_MINIMO_MS * 1000;
  esp_sleep_enable_timer_wakeup(sonoUs);
  if (config::LOGS) {
    int64_t t = esp_timer_get_time();
    Serial.flush();
    flushAntUs = static_cast<uint32_t>(esp_timer_get_time() - t);
  }
  esp_deep_sleep_start();
}

}  // namespace

void setup() {
  marcar(SETUP);
  // Sensores ligados antes de tudo: as esperas de energização (AHT20, 100 ms)
  // e de estabilização (solo, 500 ms) começam a contar já.
  bool despertar = esp_reset_reason() == ESP_RST_DEEPSLEEP;
  energia::ligarSensores(despertar);
  protecao::iniciar(despertar);  // prazo máximo acordado armado desde já
  if (config::LOGS) {
    Serial.begin(115200);
    // Marca para o PC: (instante desta linha − instante do "rst:" da ROM) − esp_timer
    // = tempo de ROM + bootloader, que a aplicação não enxerga.
    Serial.printf("[T0] %lld\n", esp_timer_get_time());
  }
  contadores::iniciar();
  reenvio::iniciar();
  if (contadores::motivoBoot() != ESP_RST_DEEPSLEEP) flushAntUs = 0;
  if (config::LOGS && contadores::motivoBoot() != ESP_RST_DEEPSLEEP) {
    // Só no boot "de verdade": no despertar, o cabeçalho seria repetido a cada ciclo.
    Serial.println();
    Serial.println("[BOOT] no sensor - etapa 7.1");
    Serial.printf("[BOOT] boot=%u motivo=%u protocolo v%u (%u bytes) canal=%u destino=" MACSTR " periodo=%lu ms\n",
                  contadores::boot(), contadores::motivoBoot(), protocolo::VERSAO,
                  (unsigned)sizeof(protocolo::PacoteAssinado), config::CANAL_WIFI, MAC2STR(config::MAC_COORDENADOR),
                  (unsigned long)config::PERIODO_CICLO_MS);
    Serial.printf("[BOOT] fila: %u leituras na memoria RTC (%u bytes), reenvio de ate %u por ciclo\n",
                  config::FILA_CAPACIDADE, (unsigned)(config::FILA_CAPACIDADE * sizeof(fila::Entrada)),
                  config::REENVIOS_POR_CICLO);
    // A chave é derivada do MAC STA (ferramentas/chaves.py no MAC): gravada
    // para outra placa, o coordenador recusaria todo pacote (assinatura_invalida).
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (memcmp(mac, segredos::MAC_DA_CHAVE, sizeof(mac)) != 0)
      Serial.printf("[BOOT] ERRO: chave gerada para " MACSTR ", mas este no e " MACSTR
                    " (ferramentas/chaves.py no MAC)\n",
                    MAC2STR(segredos::MAC_DA_CHAVE), MAC2STR(mac));
    if (config::TESTE_DUPLICATA_A_CADA > 0)
      Serial.printf("[BOOT] MODO DE TESTE: duplicata a cada %lu ciclos\n",
                    (unsigned long)config::TESTE_DUPLICATA_A_CADA);
    // t1..tN: µs do esp_now_send ao callback em cada tentativa (−1 = sem callback no prazo).
    Serial.print("[ENVIO] boot,seq,ack,tentativas,ligar_us");
    for (uint8_t k = 1; k <= config::MAX_ENVIOS; k++) Serial.printf(",t%u_us", k);
    // Etapa 7.1: conf = boot:seq confirmado (ou "nada"); resp_us = do ACK MAC da nova à confirmação;
    // fila_antes inclui a nova; descartadas = fila cheia, desde o power-on.
    Serial.println(",envio_us,erro,tent_ant,flags,leitura_us,ciclo_us,acordado_ant_ms,conf,resp_us,conf_recusadas,"
                   "fila_antes,liberadas,reenviadas,reenviadas_ack,fila_depois,descartadas");
    // Durações em µs; setup_us = da aplicação ao setup(); fim_us = esp_timer antes do flush;
    // light_sleep_us = parte do ciclo dormida em light sleep (dentro das fases dos sensores).
    Serial.println("[FASES] setup_us,serial_nvs_us,sens_iniciar_us,leitura_us,ligar_us,envio_us,desligar_us,"
                   "impressao_us,flush_ant_us,fim_us,light_sleep_us,light_sleeps,modo");
  }
  marcar(SERIAL_NVS);

  // Ordem do ciclo: ler os sensores → ligar o rádio → enviar → desligar → dormir.
  sensores::iniciar();
  marcar(SENS_INICIAR);
  sensores::Leituras leituras = sensores::lerTodas();
  protecao::Modo modo = protecao::avaliarTensao(leituras.alimentacaoMv);
  modoCiclo = static_cast<uint8_t>(modo);
  if (modo == protecao::Modo::CRITICO) {
    // Bateria crítica: não liga o rádio nem consome um seq (não seria perda).
    energia::desligarSensores();
    if (config::LOGS)
      Serial.printf("[PROTECAO] modo critico: alim=%.0f mV, sem transmitir, dorme %lu ms\n",
                    leituras.alimentacaoMv.valor, (unsigned long)protecao::periodoMs(modo));
    contadores::registrarAcordado((static_cast<uint32_t>(esp_timer_get_time()) + 500) / 1000);
    dormir(static_cast<uint32_t>(esp_timer_get_time()), protecao::periodoMs(modo));
  }
  protocolo::PacoteLeitura p = montar(leituras);
  // Guardada antes de tudo o que pode falhar (rádio, prazo de 3 s): o seq
  // consumido sempre tem a leitura na fila.
  bool descartou = reenvio::guardar(p);
  if (descartou && config::LOGS)
    Serial.printf("[FILA] cheia: a mais antiga foi descartada (%lu no total)\n",
                  (unsigned long)reenvio::descartadas());
  if (config::TESTE_TRAVAR_A_CADA_N > 0 && p.seq % config::TESTE_TRAVAR_A_CADA_N == config::TESTE_TRAVAR_A_CADA_N - 1) {
    if (config::LOGS) Serial.printf("[TESTE] travando o ciclo do seq %lu\n", (unsigned long)p.seq);
    while (true) {
    }  // o prazo de protecao deve interromper isto
  }
  energia::desligarSensores();
  marcar(LEITURA);

  // Envio com confirmação e reenvio das pendentes (Etapa 7.1); cada pacote é
  // assinado no envio (protocolo v3, docs/protocolo.md, seções 12 e 13).
  envio::Resultado ligacao;
  bool ligado = envio::ligar(ligacao);
  marcar(LIGAR);
  reenvio::Resultado rc;
  if (ligado) {
    reenvio::sincronizar(rc);
    testeDuplicata(p, rc);
  }
  marcar(ENVIO);
  envio::desligar();
  contadores::registrarEnvio(rc.nova.tentativas, rc.nova.ack);
  marcar(DESLIGAR);

  // esp_timer conta desde o início da aplicação: o tempo de ROM e bootloader
  // antes disso não entra (medido pelo PC, docs/energia.md).
  if (config::LOGS)
    imprimirEnvio(p, rc, ligacao.ligarUs, leituras.duracaoUs, static_cast<uint32_t>(marcas[DESLIGAR]));
  marcar(IMPRESSAO);  // a linha [FASES] e o flush ficam de fora (flush: no próximo ciclo)
  if (config::LOGS) imprimirFases();
  contadores::registrarAcordado((static_cast<uint32_t>(esp_timer_get_time()) + 500) / 1000);
  dormir(static_cast<uint32_t>(esp_timer_get_time()), protecao::periodoMs(modo));
}

void loop() {}  // nunca alcançado: o setup() termina em deep sleep
