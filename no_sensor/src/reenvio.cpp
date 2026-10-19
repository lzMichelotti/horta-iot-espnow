#include "reenvio.h"

#include <esp_attr.h>
#include <esp_timer.h>
#include <sys/time.h>

#include <fila.h>

#include "config.h"
#include "contadores.h"
#include "segredos.h"

namespace reenvio {

namespace {

// Memória RTC lenta: "will keep its value during a deep sleep / wake cycle"
// (esp_attr.h, ESP-IDF 5.5) e volta a zero em qualquer outro reset — a fila
// se perde num corte de energia, pânico, watchdog ou brownout (limitação
// documentada; RTC_NOINIT_ATTR ficou como melhoria futura).
RTC_DATA_ATTR uint32_t controleRtc = 0;
RTC_DATA_ATTR uint32_t descartadasRtc = 0;
RTC_DATA_ATTR fila::Entrada itensRtc[config::FILA_CAPACIDADE];

fila::Fila f(controleRtc, itensRtc, config::FILA_CAPACIDADE);

// Hora do sistema: mantida pelo temporizador RTC no deep sleep e em qualquer
// reset que não seja power-on (ESP-IDF v5.5, System Time). O nó nunca a acerta:
// conta desde o power-on, o que basta para a idade.
uint32_t agoraS() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  return static_cast<uint32_t>(tv.tv_sec);
}

// Assina e envia a entrada i (0 = mais antiga, que vai com SEM_ANTERIORES).
bool enviarEntrada(uint16_t i, envio::Resultado& r, uint8_t maxEnvios = config::MAX_ENVIOS) {
  protocolo::PacoteLeitura p = fila::paraEnvio(f.em(i), agoraS(), i == 0);
  protocolo::PacoteAssinado a;
  if (!protocolo::assinar(p, segredos::CHAVE_NO, protocolo::hmacSha256, a)) {
    if (config::LOGS) Serial.println("[ESPNOW] ERRO: falha ao assinar; pacote nao enviado");
    return false;
  }
  uint8_t bytes[sizeof(a)];
  memcpy(bytes, &a, sizeof(bytes));
  envio::enviar(bytes, sizeof(bytes), r, maxEnvios);
  return r.ack;
}

// Espera a confirmação até o prazo. Respostas inválidas (forjadas, de outra
// versão, além do gerado) são contadas e ignoradas: a espera continua.
bool esperarConfirmacao(int64_t desdeUs, Resultado& r) {
  int64_t limite = esp_timer_get_time() + static_cast<int64_t>(config::ESPERA_CONFIRMACAO_MS) * 1000;
  for (;;) {
    int64_t resta = limite - esp_timer_get_time();
    if (resta <= 0) return false;
    envio::Resposta resp;
    if (!envio::receberResposta(resp, static_cast<uint32_t>((resta + 999) / 1000))) return false;
    protocolo::Confirmacao c;
    protocolo::RejeicaoConfirmacao rej =
        protocolo::validarConfirmacao(resp.dados, resp.tamanho, segredos::CHAVE_NO, protocolo::hmacSha256,
                                      contadores::boot(), contadores::ultimoSeq(), c);
    if (rej != protocolo::RejeicaoConfirmacao::NENHUMA) {
      r.recusadas++;
      r.ultimaRecusa = rej;
      if (config::LOGS) Serial.printf("[CONF] recusada: %s\n", protocolo::nomeRejeicaoConfirmacao(rej));
      continue;
    }
    r.confirmou = true;
    r.respostaUs = static_cast<int32_t>(resp.instanteUs - desdeUs);
    r.nadaGravado = (c.flags & protocolo::flag_conf::NADA_GRAVADO) != 0;
    if (!r.nadaGravado) {
      r.confBoot = c.boot;
      r.confSeq = c.seq;
      r.liberadas += f.liberarAte(c.boot, c.seq);
    }
    return true;
  }
}

}  // namespace

void iniciar() {
  if (!f.coerente()) f.limpar();  // ex.: firmware gravado com outra capacidade
}

bool guardar(const protocolo::PacoteLeitura& p) {
  bool descartou = f.enfileirar(fila::criar(p, agoraS()));
  if (descartou) descartadasRtc++;
  return descartou;
}

void sincronizar(Resultado& r) {
  r.filaAntes = f.quantidade();
  int64_t inicio = esp_timer_get_time();
  if (r.filaAntes == 0) return;

  // 1. A leitura nova (a última da fila). Regra adaptativa (Etapa 8, passo 0):
  // se o ciclo anterior terminou sem ACK, só ENVIOS_APOS_FALHA tentativa(s).
  envio::descartarRespostas();
  uint8_t tentativas = contadores::anteriorSemAck() ? config::ENVIOS_APOS_FALHA : config::MAX_ENVIOS;
  enviarEntrada(f.quantidade() - 1, r.nova, tentativas);
  if (r.nova.ack) {
    // 2. Confirmação do que já está gravado ("tudo até X").
    bool confirmou = esperarConfirmacao(esp_timer_get_time(), r);
    // 3. Sobrou alguma além da nova: o coordenador está esperando uma anterior.
    if (confirmou && f.quantidade() > 1) {
      uint16_t n = f.quantidade() < config::REENVIOS_POR_CICLO ? f.quantidade() : config::REENVIOS_POR_CICLO;
      for (uint16_t i = 0; i < n; i++) {
        envio::Resultado ri;
        r.reenviadas++;
        if (!enviarEntrada(i, ri)) break;  // coordenador sumiu: o resto fica para o próximo ciclo
        r.reenviadasAck++;
      }
    }
  }
  r.filaDepois = f.quantidade();
  r.totalUs = static_cast<uint32_t>(esp_timer_get_time() - inicio);
}

uint16_t quantidade() { return f.quantidade(); }

uint32_t descartadas() { return descartadasRtc; }

#ifdef TESTE_DUPLICATA
bool repetirNova(envio::Resultado& r) {
  if (f.vazia()) return false;
  envio::descartarRespostas();
  if (!enviarEntrada(f.quantidade() - 1, r)) return false;
  Resultado tmp;
  return esperarConfirmacao(esp_timer_get_time(), tmp);
}
#endif

}  // namespace reenvio
