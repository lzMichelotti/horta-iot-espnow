#include "regras.h"

#include <cstring>
#include <esp_mac.h>

#include <cadastro.h>

#include "config.h"
#include "configuracao.h"
#include "consulta.h"
#include "relogio.h"

namespace regras {

static_assert(alertas::MAX_NOS == config::MAX_NOS, "lib/alertas e config.h com tamanhos de tabela diferentes");

namespace {

alertas::Tabela tabelaAlertas;
bool carregando = false;
uint32_t transicoesNaCarga = 0;
uint32_t ultimaUs = 0;
uint32_t maiorUs = 0;

const char* nomeDoNo(const uint8_t mac[6]) {
  const cadastro::No* c = configuracao::tabela().buscar(mac);
  return c ? c->nome : "desconhecido";
}

// Valor no formato do JSON e da serial (texto curto).
void formatarValor(char (&s)[16], alertas::Tipo t, int32_t v) {
  switch (alertas::unidade(t)) {
    case alertas::Unidade::PCT_DECIMOS:
      snprintf(s, sizeof(s), "%s%ld.%ld%%", v < 0 ? "-" : "", labs(v) / 10, labs(v) % 10);
      break;
    case alertas::Unidade::C_CENTESIMOS:
      snprintf(s, sizeof(s), "%s%ld.%02ldC", v < 0 ? "-" : "", labs(v) / 100, labs(v) % 100);
      break;
    case alertas::Unidade::MV: snprintf(s, sizeof(s), "%ldmV", (long)v); break;
    case alertas::Unidade::SEGUNDOS: snprintf(s, sizeof(s), "%lds", (long)v); break;
    default: snprintf(s, sizeof(s), "-"); break;
  }
}

// [ALERTA] ativou|resolveu,tipo,mac,nome,valor (só fora da carga do boot).
void avisar(const alertas::No& n, alertas::Tipo t, alertas::Mudanca m, void*) {
  if (carregando) {
    transicoesNaCarga++;
    return;
  }
  const alertas::EstadoAlerta& s = n.estado[static_cast<size_t>(t)];
  char v[16];
  if (s.temValor) formatarValor(v, t, s.valor);
  else snprintf(v, sizeof(v), "-");
  Serial.printf("[ALERTA] %s,%s," MACSTR ",%s,%s\n", m == alertas::Mudanca::ATIVOU ? "ativou" : "resolveu",
                alertas::nomeTipo(t), MAC2STR(n.mac), nomeDoNo(n.mac), v);
}

alertas::Ref referencia(const registro::Registro& r) {
  alertas::Ref ref;
  ref.desdeBoot_s = r.desde_boot_s;
  ref.utcRecepcao_s = r.utc_s;
  ref.idade_s = registro::idade(r);
  ref.bootCoord = r.boot_coord;
  ref.flagsRegistro = r.flags;
  ref.definida = true;
  return ref;
}

// Segundos desde a RECEPÇÃO de ref (sem a idade).
bool haRecepcao(const alertas::Ref& ref, uint32_t& segundos) {
  relogio::Instante a = relogio::agora();
  uint64_t agoraS = a.desdeBoot_ms / 1000;
  if (ref.bootCoord == a.boot && agoraS >= ref.desdeBoot_s) {
    segundos = static_cast<uint32_t>(agoraS - ref.desdeBoot_s);
    return true;
  }
  bool horaValida = (ref.flagsRegistro & registro::flag::HORA_VALIDA) != 0;
  if (a.valida && horaValida && a.utc_s >= static_cast<int64_t>(ref.utcRecepcao_s)) {
    segundos = static_cast<uint32_t>(a.utc_s - ref.utcRecepcao_s);
    return true;
  }
  return false;
}

}  // namespace

alertas::Limiares limiares() {
  alertas::Limiares l{};
  l.soloSecoDecimos = config::ALERTA_SOLO_SECO_DECIMOS;
  l.soloUmidoDecimos = config::ALERTA_SOLO_UMIDO_DECIMOS;
  l.soloEncharcadoDecimos = config::ALERTA_SOLO_ENCHARCADO_DECIMOS;
  l.soloBandaDecimos = config::ALERTA_SOLO_BANDA_DECIMOS;
  l.temperaturaAltaC100 = config::ALERTA_TEMP_ALTA_C100;
  l.temperaturaBaixaC100 = config::ALERTA_TEMP_BAIXA_C100;
  l.temperaturaBandaC100 = config::ALERTA_TEMP_BANDA_C100;
  l.leiturasParaAtivar = config::ALERTA_LEITURAS_PARA_ATIVAR;
  l.leiturasEncharcado = config::ALERTA_LEITURAS_ENCHARCADO;
  l.errosParaAtivar = config::ALERTA_ERROS_PARA_ATIVAR;
  l.erroAlimentacao = config::ALERTA_ERRO_ALIMENTACAO;
  return l;
}

uint32_t limiteSemComunicar(protocolo::ModoBateria m) {
  uint32_t periodo = config::ALERTA_PERIODO_NO_S;
  if (m == protocolo::ModoBateria::ECONOMIA) periodo *= config::ALERTA_FATOR_ECONOMIA;
  if (m == protocolo::ModoBateria::CRITICO) periodo = config::ALERTA_PERIODO_CRITICO_S;
  return periodo * config::ALERTA_SEM_COMUNICAR_CICLOS;
}

void iniciarCarga() {
  carregando = true;
  transicoesNaCarga = 0;
  tabelaAlertas.limpar();
}

void terminarCarga() {
  carregando = false;
  Serial.printf("[ALERTA] reconstruidos do historico: %u ativos (%lu transicoes na carga); tabela de %u bytes\n",
                (unsigned)tabelaAlertas.ativos(), (unsigned long)transicoesNaCarga, (unsigned)sizeof(tabelaAlertas));
  for (size_t i = 0; i < alertas::MAX_NOS; i++) {
    const alertas::No& n = tabelaAlertas.posicao(i);
    if (!n.usado) continue;
    for (size_t t = 0; t < alertas::NUM_TIPOS; t++)
      if (n.estado[t].ativo)
        Serial.printf("[ALERTA] ativo,%s," MACSTR ",%s\n", alertas::nomeTipo(static_cast<alertas::Tipo>(t)),
                      MAC2STR(n.mac), nomeDoNo(n.mac));
  }
}

void observar(const registro::Registro& r) {
  uint32_t t0 = micros();
  uint8_t mac[6];
  memcpy(mac, r.mac, 6);  // cópia: não passar ponteiro de campo de struct packed
  alertas::No* n = tabelaAlertas.buscar(mac, true);
  if (n == nullptr) return;  // tabela cheia (mesmo limite da tabela de nós)

  protocolo::PacoteLeitura p = r.pacote;  // cópia: não ler campos packed por referência
  alertas::Leitura l;
  l.ref = referencia(r);
  l.estados = p.estados;
  l.temperaturaC100 = p.temperatura_c100;
  l.alimentacaoMv = p.alimentacao_mv;
  l.modo = protocolo::lerModoBateria(p.flags);
  const cadastro::No* c = configuracao::tabela().buscar(mac);
  l.temPct = c != nullptr && cadastro::umidadeSoloDecimosSemLimite(*c, p.solo_mv, l.soloDecimos);

  tabelaAlertas.avaliar(*n, l, limiares(), avisar, nullptr);
  ultimaUs = micros() - t0;
  if (!carregando && ultimaUs > maiorUs) maiorUs = ultimaUs;
}

void verificar() {
  static uint32_t proxima = 0;
  if ((int32_t)(millis() - proxima) < 0) return;
  proxima = millis() + config::ALERTA_VERIFICACAO_MS;
  for (size_t i = 0; i < alertas::MAX_NOS; i++) {
    alertas::No& n = const_cast<alertas::No&>(tabelaAlertas.posicao(i));
    uint32_t ha = 0;
    if (!n.usado || !haRecepcao(n.ultima, ha)) continue;
    tabelaAlertas.verificarSilencio(n, ha, limiteSemComunicar(n.modo), avisar, nullptr);
  }
}

const alertas::Tabela& tabela() { return tabelaAlertas; }

filtro::Hora hora(const alertas::Ref& ref, uint32_t& utcLeitura) {
  // Um registro só com os campos que a hora usa: recepção, boot, flags e idade.
  registro::Registro r{};
  r.boot_coord = ref.bootCoord;
  r.desde_boot_s = ref.desdeBoot_s;
  r.utc_s = ref.utcRecepcao_s;
  r.flags = ref.flagsRegistro;
  protocolo::PacoteLeitura p{};
  p.idade_s = ref.idade_s;
  r.pacote = p;
  return filtro::horaEfetiva(r, consulta::ancoras(), utcLeitura);
}

bool haQuanto(const alertas::Ref& ref, uint32_t& segundos) {
  uint32_t s = 0;
  if (!ref.definida || !haRecepcao(ref, s)) return false;
  segundos = s + ref.idade_s;
  return true;
}

uint32_t ultimaAvaliacaoUs() { return ultimaUs; }
uint32_t maiorAvaliacaoUs() { return maiorUs; }

void comando(const char*) {
  Serial.printf("[ALERTA] %u ativos; avaliacao: ultima %lu us, maior %lu us; tabela de %u bytes\n",
                (unsigned)tabelaAlertas.ativos(), (unsigned long)ultimaUs, (unsigned long)maiorUs,
                (unsigned)sizeof(tabelaAlertas));
  Serial.println("[ALERTA] mac,nome,tipo,estado,seguidas,valor,ha_s");
  for (size_t i = 0; i < alertas::MAX_NOS; i++) {
    const alertas::No& n = tabelaAlertas.posicao(i);
    if (!n.usado) continue;
    Serial.printf("[ALERTA] " MACSTR ",%s,modo=%s,solo=%s,limite_sem_comunicar=%lus\n", MAC2STR(n.mac),
                  nomeDoNo(n.mac), protocolo::nomeModoBateria(n.modo),
                  alertas::nomeClasseSolo(n.classe) ? alertas::nomeClasseSolo(n.classe) : "-",
                  (unsigned long)limiteSemComunicar(n.modo));
    for (size_t t = 0; t < alertas::NUM_TIPOS; t++) {
      const alertas::EstadoAlerta& s = n.estado[t];
      if (!s.ativo && !s.resolvido && s.seguidas == 0) continue;
      char v[16];
      if (s.temValor) formatarValor(v, static_cast<alertas::Tipo>(t), s.valor);
      else snprintf(v, sizeof(v), "-");
      uint32_t ha = 0;
      bool temHa = haQuanto(s.ativo || s.resolvido ? s.inicio : s.candidato, ha);
      Serial.printf("[ALERTA] " MACSTR ",%s,%s,%s,%u,%s,%s%lu\n", MAC2STR(n.mac), nomeDoNo(n.mac),
                    alertas::nomeTipo(static_cast<alertas::Tipo>(t)),
                    s.ativo ? "ativo" : (s.resolvido ? "resolvido" : "candidato"), s.seguidas, v,
                    temHa ? "" : "?", (unsigned long)(temHa ? ha : 0));
    }
  }
}

}  // namespace regras
