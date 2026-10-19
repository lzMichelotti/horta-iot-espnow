// Formato JSON das leituras na API HTTP (Etapa 8; docs/api.md, seções 2 e 4.4).
// C++ puro: compila no ESP32 e no PC (testes nativos). Quem envia é src/api.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

#include <filtro.h>
#include <json.h>
#include <registro.h>

namespace apijson {

// "88:57:21:70:93:70" (minúsculas): a forma usada nas rotas e no JSON.
constexpr size_t TAM_MAC = 18;
void mac(char (&saida)[TAM_MAC], const uint8_t m[6]);

const char* nomeEstado(protocolo::Estado e);

// {"utc": "AAAA-MM-DDTHH:MM:SSZ" | null, "origem": "valida" | "reconstruida" | "sem_hora"}
void hora(json::Escritor& e, filtro::Hora h, uint32_t utc);

// Uma leitura do histórico (objeto). `utcLeitura` é a hora efetiva (já sem a
// idade); `pctDecimos` vale só com temPct. O nome vem do cadastro atual.
// id < 0: sem o campo "id" (última leitura em RAM, que não tem id global).
void leitura(json::Escritor& e, int64_t id, const registro::Registro& r, const char* nome, filtro::Hora h,
             uint32_t utcLeitura, bool temPct, int16_t pctDecimos);

}  // namespace apijson
