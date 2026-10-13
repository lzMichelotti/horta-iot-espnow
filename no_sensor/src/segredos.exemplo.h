// Modelo de segredos.h do nó (o real fica fora do Git). Não copiar à mão: gerar
// com a chave mestra do coordenador, antes de gravar CADA nó:
//   .venv/bin/python ferramentas/chaves.py no 88:57:21:70:93:70   (MAC STA do nó)
#pragma once

#include <cstdint>

namespace segredos {

constexpr uint8_t MAC_DA_CHAVE[6] = {};  // MAC STA para o qual a chave foi derivada
constexpr uint8_t CHAVE_NO[32] = {};     // HMAC-SHA256(CHAVE_MESTRA, MAC_DA_CHAVE)

}  // namespace segredos
