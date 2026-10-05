// Energia do nó durante o ciclo: liga/desliga a alimentação dos sensores
// (MOSFET, pino configurável) e faz as esperas em light sleep.
//
// Light sleep: "the digital peripherals, most of the RAM, and CPUs are
// clock-gated and their supply voltage is reduced. Upon exit [...] their
// internal states are preserved" (ESP-IDF v5.5, Sleep Modes). Durante a espera
// pelos sensores, a CPU dorme em vez de girar num delay().
#pragma once

#include <Arduino.h>

namespace energia {

// Liga a alimentação dos sensores e guarda o instante (esp_timer, µs).
// Sem chaveamento (PINO_ALIM_SENSORES < 0), os sensores estão sempre ligados:
// no POWERON, desde o boot (instante 0); depois do deep sleep, há muito tempo.
void ligarSensores(bool despertarDoSono);
void desligarSensores();

// Instante (esp_timer, µs) em que os sensores foram energizados.
int64_t instanteSensoresLigados();

// Espera até o instante dado (esp_timer, µs): light sleep se faltar mais que
// LIMIAR_LIGHT_SLEEP_US, espera ativa abaixo disso.
void esperarAte(int64_t instanteUs);
void esperarMs(uint32_t ms);

// Instrumentação: tempo total e número de light sleeps neste ciclo.
uint32_t lightSleepUs();
uint8_t lightSleeps();

}  // namespace energia
