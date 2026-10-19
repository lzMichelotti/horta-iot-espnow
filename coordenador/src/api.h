// API HTTP do coordenador (Etapa 8). Especificação: docs/api.md.
//
// WebServer do núcleo Arduino-ESP32 (síncrono): handleClient() roda no loop(),
// uma requisição por vez, sempre com "Connection: close". Respostas pequenas
// vão com Content-Length; as grandes (histórico, CSV) em partes (chunked),
// escritas direto na conexão, sem montar o documento na RAM.
//
// Convivência com o ESP-NOW: entre as páginas do histórico e do CSV, a API
// chama a função `ocioso` (no main: esvaziar a fila do ESP-NOW, responder ao
// nó e gravar). Nunca dentro de uma página, porque a gravação mexe no índice
// que a consulta está percorrendo.
#pragma once

#include <Arduino.h>

namespace api {

using Ocioso = void (*)();

// Registra as rotas e liga o servidor na porta 80. Chamar depois do SoftAP.
void iniciar(Ocioso ocioso);

// Atende no máximo uma requisição (chamar a cada volta do loop()).
void atender();

}  // namespace api
