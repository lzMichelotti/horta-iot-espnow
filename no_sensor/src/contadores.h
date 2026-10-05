// Contadores do protocolo no nó: boot (NVS, sobrevive à falta de energia) e
// número de sequência (RAM do RTC, sobrevive ao deep sleep). Regras em docs/protocolo.md.
#pragma once

#include <Arduino.h>

namespace contadores {

// Lê o motivo do boot e, se não for o despertar do deep sleep, incrementa o
// contador de boots na NVS e zera o seq. Chamar uma vez no setup().
void iniciar();

uint16_t boot();
uint8_t motivoBoot();  // esp_reset_reason()

// Devolve o seq do próximo pacote e avança o contador (0 é o primeiro do boot).
uint32_t proximoSeq();

// Resultado do envio do pacote atual, para o campo tentativas_ant e o bit
// ANTERIOR_SEM_ACK do próximo pacote (RAM do RTC; 0 depois de ligar).
void registrarEnvio(uint8_t tentativas, bool ack);
uint8_t tentativasAnt();
bool anteriorSemAck();

// Tempo acordado do ciclo atual (ms, da aplicação ao início do sono), para o
// campo acordado_ant_ms do próximo pacote (RAM do RTC; 0 depois de ligar).
void registrarAcordado(uint32_t ms);
uint16_t acordadoAnt();

}  // namespace contadores
