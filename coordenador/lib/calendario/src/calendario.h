// Conversões de data e hora UTC sem fuso horário e sem a libc (mktime/gmtime
// dependem da variável TZ). C++ puro: compila no ESP32 e no PC (testes nativos).
// Usado pelo relógio provisório (src/relogio.*) e, depois, pelas consultas.
#pragma once

#include <cstddef>
#include <cstdint>

namespace calendario {

struct DataHora {
  int32_t ano;
  uint8_t mes;  // 1–12
  uint8_t dia;  // 1–31
  uint8_t hora;
  uint8_t minuto;
  uint8_t segundo;  // 0–59 (segundo bissexto não é aceito)
};

constexpr int64_t SEGUNDOS_POR_DIA = 86400;

bool bissexto(int32_t ano);
uint8_t diasNoMes(int32_t ano, uint8_t mes);  // 0 se o mês for inválido
bool dataValida(const DataHora& d);

// Dias desde 1970-01-01 (negativo antes dessa data), calendário gregoriano.
// Algoritmo days_from_civil de Howard Hinnant (domínio público):
// https://howardhinnant.github.io/date_algorithms.html
int64_t diasDesdeEpoch(int32_t ano, uint8_t mes, uint8_t dia);

// Segundos desde 1970-01-01T00:00:00Z. Não valida: chamar dataValida antes.
int64_t paraEpoch(const DataHora& d);

// Inverso de paraEpoch (civil_from_days, mesma fonte).
DataHora deEpoch(int64_t utc_s);

// Aceita os dois formatos do comando "hora" na serial:
//   - epoch em segundos, só dígitos:   1791297000
//   - ISO 8601 em UTC, com o Z final: 2026-10-06T14:30:00Z
// false se o texto não estiver exatamente num desses formatos ou a data não existir.
bool interpretar(const char* texto, int64_t& utc_s);

// "2026-10-06T14:30:00Z". Anos fora de 0–9999 saem como "fora-de-faixa".
constexpr size_t TAM_ISO = 21;
void formatarIso(int64_t utc_s, char (&saida)[TAM_ISO]);

// Hora de um evento a partir de um acerto no MESMO boot: o acerto informou
// utcAcerto_s quando o tempo desde o boot era msAcerto; o evento aconteceu em
// msEvento (antes ou depois). Arredonda para baixo, como time().
int64_t reconstruir(int64_t utcAcerto_s, uint64_t msAcerto, uint64_t msEvento);

}  // namespace calendario
