// Regras locais e alertas no coordenador (Etapa 9; docs/alertas.md): liga a
// lógica de lib/alertas ao histórico, ao relógio e ao cadastro.
//
// Os alertas vivem só na RAM e se reconstroem no boot: observar() recebe cada
// registro do histórico, tanto na carga (em ordem) quanto em cada gravação, na
// tarefa do loop() que grava (nunca no callback de recepção do ESP-NOW).
// Nenhuma escrita a mais na flash.
#pragma once

#include <Arduino.h>

#include <alertas.h>
#include <filtro.h>
#include <registro.h>

namespace regras {

// Em volta de historico::iniciar(): durante a carga, as transições não vão
// para a serial (seriam milhares); no fim sai um resumo [ALERTA].
void iniciarCarga();
void terminarCarga();

// Observador do histórico: avalia as regras com o registro gravado (ou lido na carga).
void observar(const registro::Registro& r);

// Chamar a cada volta do loop(): confere o sem_comunicar a cada config::ALERTA_VERIFICACAO_MS.
void verificar();

const alertas::Tabela& tabela();
alertas::Limiares limiares();

// Limite do sem_comunicar para um nó no modo de bateria dado, em s.
uint32_t limiteSemComunicar(protocolo::ModoBateria m);

// Hora da leitura de referência, como em /leituras (valida, reconstruida ou sem_hora).
filtro::Hora hora(const alertas::Ref& ref, uint32_t& utcLeitura);

// Segundos desde a LEITURA de referência (recepção + idade), pelo relógio
// interno no mesmo boot ou pela hora UTC entre boots. false se não dá para saber.
bool haQuanto(const alertas::Ref& ref, uint32_t& segundos);

// Custo da última avaliação (µs) e o maior desde o boot, para docs/alertas.md.
uint32_t ultimaAvaliacaoUs();
uint32_t maiorAvaliacaoUs();

// Comando "alertas" da serial: estado de cada regra de cada nó.
void comando(const char* argumento);

}  // namespace regras
