#include "relogio.h"

#include <Preferences.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <sys/time.h>

#include <calendario.h>

#include "config.h"

namespace relogio {

namespace {

// RTC_NOINIT_ATTR: "will keep its value after restart or during a deep sleep /
// wake cycle" (esp_attr.h, ESP-IDF 5.5). No power-on o conteúdo é indefinido,
// por isso a marca e o seu complemento: lixo dificilmente acerta os dois.
constexpr uint32_t MARCA = 0x484F5241;  // "HORA"
struct MarcaRtc {
  uint32_t marca;
  uint32_t complemento;
};
RTC_NOINIT_ATTR MarcaRtc marcaRtc;

// NVS via Preferences (Arduino-ESP32): nomes de até 15 caracteres.
// https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html
constexpr const char* NVS_NAMESPACE = "coordenador";
constexpr const char* NVS_CHAVE_BOOT = "boot";

Fonte fonte = Fonte::NENHUMA;
uint16_t bootAtual = 0;
Acerto acerto{};
bool houveAcerto = false;

bool plausivel(int64_t utc_s) { return utc_s >= config::HORA_MINIMA_UTC && utc_s < config::HORA_MAXIMA_UTC; }

bool marcaPresente() { return marcaRtc.marca == MARCA && marcaRtc.complemento == ~MARCA; }

void gravarMarca() {
  marcaRtc.marca = MARCA;
  marcaRtc.complemento = ~MARCA;
}

void apagarMarca() {
  marcaRtc.marca = 0;
  marcaRtc.complemento = 0;
}

// Resets em que o temporizador RTC continua contando ("persist time keeping
// across any resets (with the exception of power-on resets)", System Time,
// ESP-IDF v5.5). Brownout e motivo desconhecido ficam de fora por cautela:
// sugerem problema na alimentação. No ESP32 o pino EN gera ESP_RST_POWERON.
bool resetMantemHora(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_SW:
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return true;
    default:
      return false;
  }
}

uint64_t desdeBootMs() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }

}  // namespace

const char* nomeFonte(Fonte f) {
  switch (f) {
    case Fonte::NENHUMA: return "nenhuma";
    case Fonte::MANUAL: return "manual";
    case Fonte::HERDADA: return "herdada";
  }
  return "?";
}

void iniciar() {
  // Uma escrita na NVS por boot (mesmo esquema do contador do nó).
  Preferences nvs;
  nvs.begin(NVS_NAMESPACE, false);
  bootAtual = static_cast<uint16_t>(nvs.getUShort(NVS_CHAVE_BOOT, 0) + 1);  // dá a volta em 65535
  nvs.putUShort(NVS_CHAVE_BOOT, bootAtual);
  nvs.end();

  esp_reset_reason_t motivo = esp_reset_reason();
  if (resetMantemHora(motivo) && marcaPresente() && plausivel(time(nullptr))) {
    fonte = Fonte::HERDADA;
  } else {
    fonte = Fonte::NENHUMA;
    apagarMarca();
  }
  Serial.printf("[RELOGIO] boot %u, motivo do reset %d\n", bootAtual, static_cast<int>(motivo));
  imprimirEstado();
}

Instante agora() {
  timeval tv{};
  gettimeofday(&tv, nullptr);
  Instante i{};
  i.utc_s = tv.tv_sec;
  i.fonte = fonte;
  i.valida = fonte != Fonte::NENHUMA;
  i.desdeBoot_ms = desdeBootMs();
  i.boot = bootAtual;
  return i;
}

bool acertar(int64_t utc_s) {
  if (!plausivel(utc_s)) return false;
  Instante antes = agora();
  timeval tv{};
  tv.tv_sec = static_cast<time_t>(utc_s);
  settimeofday(&tv, nullptr);

  acerto.utc_s = utc_s;
  acerto.desdeBoot_ms = antes.desdeBoot_ms;
  acerto.havia = antes.valida;
  acerto.salto_s = antes.valida ? utc_s - antes.utc_s : 0;
  houveAcerto = true;
  fonte = Fonte::MANUAL;
  gravarMarca();
  return true;
}

bool ultimoAcerto(Acerto& a) {
  if (houveAcerto) a = acerto;
  return houveAcerto;
}

void comandoHora(const char* argumento) {
  if (argumento == nullptr || argumento[0] == '\0') {
    imprimirEstado();
    return;
  }
  int64_t utc = 0;
  if (!calendario::interpretar(argumento, utc)) {
    Serial.println("[RELOGIO] formato invalido. Use: hora 1791297000  ou  hora 2026-10-06T14:30:00Z");
    return;
  }
  if (!acertar(utc)) {
    char min[calendario::TAM_ISO], max[calendario::TAM_ISO];
    calendario::formatarIso(config::HORA_MINIMA_UTC, min);
    calendario::formatarIso(config::HORA_MAXIMA_UTC, max);
    Serial.printf("[RELOGIO] hora fora da faixa plausivel [%s, %s)\n", min, max);
    return;
  }
  if (acerto.havia)
    Serial.printf("[RELOGIO] acertada; ajuste de %+lld s\n", static_cast<long long>(acerto.salto_s));
  else
    Serial.println("[RELOGIO] acertada (antes: invalida)");
  imprimirEstado();
}

void imprimirEstado() {
  Instante i = agora();
  char iso[calendario::TAM_ISO];
  calendario::formatarIso(i.utc_s, iso);
  Serial.printf("[RELOGIO] utc=%s epoch=%lld valida=%d fonte=%s boot=%u desde_boot_ms=%llu\n",
                i.valida ? iso : "invalida", static_cast<long long>(i.utc_s), i.valida, nomeFonte(i.fonte), i.boot,
                static_cast<unsigned long long>(i.desdeBoot_ms));
}

}  // namespace relogio
