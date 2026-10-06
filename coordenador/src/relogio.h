// Relógio do coordenador: hora UTC + validade + tempo desde o boot.
//
// Implementação provisória (Etapa 7, com o DS3231 adiado): relógio do sistema
// do ESP32, acertado à mão pelo comando "hora" na serial. Quando o DS3231
// entrar (etapa 6), só esta implementação muda; a interface fica.
//
// O relógio do sistema sobrevive a resets por software (temporizador RTC), mas
// não a power-on: falta de energia e abertura da porta serial (pino EN).
// https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/system_time.html
#pragma once

#include <Arduino.h>

namespace relogio {

enum class Fonte : uint8_t {
  NENHUMA = 0,  // não acertada desde o último power-on: hora inválida
  MANUAL = 1,   // acertada pelo comando "hora" neste boot
  HERDADA = 2,  // acertada antes de um reset por software; o temporizador RTC manteve
  // 3: reservado para o DS3231 (etapa 6)
};

const char* nomeFonte(Fonte f);

struct Instante {
  int64_t utc_s;          // segundos desde 1970-01-01 UTC; sem sentido se !valida
  bool valida;            // fonte != NENHUMA
  Fonte fonte;
  uint64_t desdeBoot_ms;  // esp_timer (64 bits): não dá a volta como o millis()
  uint16_t boot;          // contador de boots do coordenador (NVS)
};

// Último acerto neste boot: permite reconstruir a hora de eventos anteriores
// do mesmo boot (calendario::reconstruir).
struct Acerto {
  int64_t utc_s;
  uint64_t desdeBoot_ms;
  int64_t salto_s;    // nova hora − hora anterior (só se a anterior era válida)
  bool havia;         // a hora já era válida antes deste acerto
};

// Incrementa o contador de boots (uma escrita na NVS) e decide a validade
// conforme o motivo do reset. Chamar uma vez no setup(), antes de usar agora().
void iniciar();

Instante agora();

// Acerta o relógio do sistema. false (e nada muda) se a hora estiver fora da
// faixa plausível de config.h.
bool acertar(int64_t utc_s);

// false se não houve acerto neste boot.
bool ultimoAcerto(Acerto& a);

// Comando "hora" da serial: sem argumento mostra o estado; com argumento
// (epoch ou AAAA-MM-DDTHH:MM:SSZ) acerta.
void comandoHora(const char* argumento);

void imprimirEstado();

}  // namespace relogio
