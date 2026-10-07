// Configuração persistente do coordenador: cadastro dos nós (MAC → nome e
// calibração do solo) guardado na NVS, num único blob (lib/cadastro).
//
// Por que NVS e não LittleFS (docs/persistencia.md): a configuração fica
// independente do histórico; se o LittleFS não montar ou for formatado, os
// nomes e as calibrações continuam. A NVS "works best for storing many small
// values" e não perde dados numa queda de energia, "except for the new
// key-value pair if it was being written" (ESP-IDF v5.5, NVS):
// https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/storage/nvs_flash.html
#pragma once

#include <Arduino.h>

#include <cadastro.h>
#include <registro.h>

namespace configuracao {

// Lê o cadastro da NVS; sem cadastro (ou inválido), grava a semente de
// config::NOS. Chamar no setup() antes do histórico e do rádio.
void iniciar();

const cadastro::Tabela& tabela();

// Umidade do solo do registro, em décimos de %, com a calibração ATUAL do nó
// (recalibrar corrige todo o histórico). false sem calibração ou com o solo
// em estado diferente de OK.
bool umidadeSolo(const registro::Registro& r, int16_t& decimos);

// Comando "no" da serial:
//   no | no nome MAC NOME | no cal MAC SECO UMIDO | no seco MAC | no umido MAC
//   no semcal MAC | no apagar MAC | no padrao
void comando(const char* argumento);

}  // namespace configuracao
