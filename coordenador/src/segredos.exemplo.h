// Modelo de segredos.h. Copie para segredos.h (ignorado pelo Git) e troque os valores:
//   cp coordenador/src/segredos.exemplo.h coordenador/src/segredos.h
//   .venv/bin/python ferramentas/chaves.py mestra     (gera a CHAVE_MESTRA)
#pragma once

#include <cstdint>

namespace segredos {

constexpr const char* AP_SSID = "horta-coord";
constexpr const char* AP_SENHA = "troque-esta-senha";  // WPA2: 8 a 63 caracteres

// Chave mestra do HMAC (protocolo v2, docs/protocolo.md, seção 12): a chave de
// cada nó é HMAC-SHA256(CHAVE_MESTRA, MAC do nó). Zeros = não gerada.
constexpr uint8_t CHAVE_MESTRA[32] = {};

}  // namespace segredos
