#include "contadores.h"

#include <Preferences.h>
#include <esp_attr.h>
#include <esp_system.h>

namespace contadores {

namespace {

// RTC_DATA_ATTR: "will keep its value during a deep sleep / wake cycle"
// (esp_attr.h, ESP-IDF 5.5); em qualquer outro reset volta ao valor inicial.
RTC_DATA_ATTR uint32_t seqRtc = 0;
RTC_DATA_ATTR uint16_t bootRtc = 0;
RTC_DATA_ATTR uint8_t tentativasAntRtc = 0;  // 0 = desconhecido
RTC_DATA_ATTR bool semAckAntRtc = false;
RTC_DATA_ATTR uint16_t acordadoAntRtc = 0;   // 0 = desconhecido

uint8_t motivo = 0;

// NVS via Preferences (Arduino-ESP32): nomes de até 15 caracteres.
// https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html
constexpr const char* NVS_NAMESPACE = "protocolo";
constexpr const char* NVS_CHAVE_BOOT = "boot";

}  // namespace

void iniciar() {
  esp_reset_reason_t r = esp_reset_reason();
  motivo = static_cast<uint8_t>(r);
  if (r == ESP_RST_DEEPSLEEP) return;  // mesmo boot: mantém bootRtc e seqRtc

  // Qualquer outro início é um boot novo: uma única escrita na flash por boot.
  Preferences nvs;
  nvs.begin(NVS_NAMESPACE, false);
  bootRtc = static_cast<uint16_t>(nvs.getUShort(NVS_CHAVE_BOOT, 0) + 1);  // dá a volta em 65535
  nvs.putUShort(NVS_CHAVE_BOOT, bootRtc);
  nvs.end();
  seqRtc = 0;
  tentativasAntRtc = 0;
  semAckAntRtc = false;
  acordadoAntRtc = 0;
}

uint16_t boot() { return bootRtc; }

uint8_t motivoBoot() { return motivo; }

uint32_t proximoSeq() { return seqRtc++; }

void registrarEnvio(uint8_t tentativas, bool ack) {
  tentativasAntRtc = tentativas;
  semAckAntRtc = !ack;
}

uint8_t tentativasAnt() { return tentativasAntRtc; }

bool anteriorSemAck() { return semAckAntRtc; }

void registrarAcordado(uint32_t ms) { acordadoAntRtc = ms > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(ms); }

uint16_t acordadoAnt() { return acordadoAntRtc; }

}  // namespace contadores
