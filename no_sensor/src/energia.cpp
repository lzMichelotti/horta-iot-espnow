#include "energia.h"

#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include "config.h"

namespace energia {

namespace {

int64_t ligadosUs = 0;
uint32_t dormidoUs = 0;
uint8_t vezes = 0;

constexpr bool TEM_CHAVE = config::PINO_ALIM_SENSORES >= 0;
constexpr gpio_num_t PINO_CHAVE = static_cast<gpio_num_t>(TEM_CHAVE ? config::PINO_ALIM_SENSORES : 0);

void escreverChave(bool ligado) {
  if (!TEM_CHAVE) return;
  pinMode(config::PINO_ALIM_SENSORES, OUTPUT);
  digitalWrite(config::PINO_ALIM_SENSORES, ligado == config::CHAVE_ATIVA_EM_ALTO ? HIGH : LOW);
}

void lightSleep(uint64_t us) {
  esp_sleep_enable_timer_wakeup(us);
  // gpio_hold_en mantém o nível de saída mesmo se o domínio do pino for
  // desligado no light sleep (driver/gpio.h, ESP-IDF 5.5): o MOSFET não pisca.
  if (TEM_CHAVE) gpio_hold_en(PINO_CHAVE);
  int64_t t = esp_timer_get_time();
  esp_light_sleep_start();
  dormidoUs += static_cast<uint32_t>(esp_timer_get_time() - t);
  vezes++;
  if (TEM_CHAVE) gpio_hold_dis(PINO_CHAVE);
}

}  // namespace

void ligarSensores(bool despertarDoSono) {
  if (TEM_CHAVE) {
    escreverChave(true);
    ligadosUs = esp_timer_get_time();
  } else {
    // Sempre alimentados: energizados no boot (0) ou, depois do deep sleep, há
    // pelo menos um período inteiro — nenhuma espera de energização se aplica.
    ligadosUs = despertarDoSono ? -(int64_t)config::PERIODO_CICLO_MS * 1000 : 0;
  }
}

void desligarSensores() { escreverChave(false); }

int64_t instanteSensoresLigados() { return ligadosUs; }

void esperarAte(int64_t instanteUs) {
  int64_t falta = instanteUs - esp_timer_get_time();
  if (falta <= 0) return;
  if (config::LIGHT_SLEEP_NAS_ESPERAS && falta > (int64_t)config::LIMIAR_LIGHT_SLEEP_US) {
    lightSleep(static_cast<uint64_t>(falta));
  }
  while (esp_timer_get_time() < instanteUs) {
  }  // resto curto (ou tudo, sem light sleep): espera ativa
}

void esperarMs(uint32_t ms) { esperarAte(esp_timer_get_time() + (int64_t)ms * 1000); }

uint32_t lightSleepUs() { return dormidoUs; }

uint8_t lightSleeps() { return vezes; }

}  // namespace energia
