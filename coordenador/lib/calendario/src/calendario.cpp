#include "calendario.h"

#include <cstdio>

namespace calendario {

namespace {

// Divisão inteira arredondada para −∞ (a de C++ trunca em direção a zero).
int64_t dividirParaBaixo(int64_t a, int64_t b) {
  int64_t q = a / b;
  return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

bool digito(char c) { return c >= '0' && c <= '9'; }

// Lê exatamente n dígitos a partir de p.
bool lerDigitos(const char* p, int n, int32_t& valor) {
  valor = 0;
  for (int i = 0; i < n; i++) {
    if (!digito(p[i])) return false;
    valor = valor * 10 + (p[i] - '0');
  }
  return true;
}

bool interpretarEpoch(const char* t, int64_t& utc_s) {
  // Até 12 dígitos: cobre qualquer data plausível e não estoura int64_t.
  int64_t v = 0;
  int n = 0;
  for (; t[n] != '\0'; n++) {
    if (!digito(t[n]) || n >= 12) return false;
    v = v * 10 + (t[n] - '0');
  }
  if (n == 0) return false;
  utc_s = v;
  return true;
}

bool interpretarIso(const char* t, int64_t& utc_s) {
  // Posições fixas de "AAAA-MM-DDTHH:MM:SSZ" (20 caracteres).
  static const char SEPARADORES[] = {'-', '-', 'T', ':', ':', 'Z'};
  static const int POSICOES[] = {4, 7, 10, 13, 16, 19};
  for (int i = 0; i < 20; i++)
    if (t[i] == '\0') return false;
  if (t[20] != '\0') return false;
  for (int i = 0; i < 6; i++)
    if (t[POSICOES[i]] != SEPARADORES[i]) return false;

  int32_t ano, mes, dia, hora, minuto, segundo;
  if (!lerDigitos(t, 4, ano) || !lerDigitos(t + 5, 2, mes) || !lerDigitos(t + 8, 2, dia) ||
      !lerDigitos(t + 11, 2, hora) || !lerDigitos(t + 14, 2, minuto) || !lerDigitos(t + 17, 2, segundo))
    return false;

  DataHora d{ano, static_cast<uint8_t>(mes), static_cast<uint8_t>(dia), static_cast<uint8_t>(hora),
             static_cast<uint8_t>(minuto), static_cast<uint8_t>(segundo)};
  if (!dataValida(d)) return false;
  utc_s = paraEpoch(d);
  return true;
}

}  // namespace

bool bissexto(int32_t ano) { return (ano % 4 == 0 && ano % 100 != 0) || ano % 400 == 0; }

uint8_t diasNoMes(int32_t ano, uint8_t mes) {
  static const uint8_t DIAS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (mes < 1 || mes > 12) return 0;
  return (mes == 2 && bissexto(ano)) ? 29 : DIAS[mes - 1];
}

bool dataValida(const DataHora& d) {
  return d.dia >= 1 && d.dia <= diasNoMes(d.ano, d.mes) && d.hora < 24 && d.minuto < 60 && d.segundo < 60;
}

int64_t diasDesdeEpoch(int32_t ano, uint8_t mes, uint8_t dia) {
  int64_t y = ano - (mes <= 2 ? 1 : 0);
  int64_t era = dividirParaBaixo(y, 400);
  int64_t yoe = y - era * 400;                                    // [0, 399]
  int64_t doy = (153 * (mes > 2 ? mes - 3 : mes + 9) + 2) / 5 + dia - 1;  // [0, 365]
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;            // [0, 146096]
  return era * 146097 + doe - 719468;
}

int64_t paraEpoch(const DataHora& d) {
  return diasDesdeEpoch(d.ano, d.mes, d.dia) * SEGUNDOS_POR_DIA + d.hora * 3600 + d.minuto * 60 + d.segundo;
}

DataHora deEpoch(int64_t utc_s) {
  int64_t z = dividirParaBaixo(utc_s, SEGUNDOS_POR_DIA);
  int64_t s = utc_s - z * SEGUNDOS_POR_DIA;  // [0, 86399]
  z += 719468;
  int64_t era = dividirParaBaixo(z, 146097);
  int64_t doe = z - era * 146097;                                   // [0, 146096]
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);            // [0, 365]
  int64_t mp = (5 * doy + 2) / 153;                                 // [0, 11]
  int64_t mes = mp < 10 ? mp + 3 : mp - 9;
  int64_t ano = yoe + era * 400 + (mes <= 2 ? 1 : 0);

  DataHora d{};
  d.ano = static_cast<int32_t>(ano);
  d.mes = static_cast<uint8_t>(mes);
  d.dia = static_cast<uint8_t>(doy - (153 * mp + 2) / 5 + 1);
  d.hora = static_cast<uint8_t>(s / 3600);
  d.minuto = static_cast<uint8_t>(s % 3600 / 60);
  d.segundo = static_cast<uint8_t>(s % 60);
  return d;
}

bool interpretar(const char* texto, int64_t& utc_s) {
  if (texto == nullptr || texto[0] == '\0') return false;
  // O ISO tem '-' na posição 4; o epoch, só dígitos. Não ler além do '\0'.
  for (int i = 0; i < 5; i++) {
    if (texto[i] == '\0') return interpretarEpoch(texto, utc_s);
    if (texto[i] == '-') return i == 4 && interpretarIso(texto, utc_s);
  }
  return interpretarEpoch(texto, utc_s);
}

void formatarIso(int64_t utc_s, char (&saida)[TAM_ISO]) {
  DataHora d = deEpoch(utc_s);
  if (d.ano < 0 || d.ano > 9999) {
    snprintf(saida, TAM_ISO, "fora-de-faixa");
    return;
  }
  // "% 100u" não muda os valores (já validados); só mostra ao compilador que
  // cada campo tem 2 dígitos, evitando o aviso -Wformat-truncation.
  snprintf(saida, TAM_ISO, "%04d-%02u-%02uT%02u:%02u:%02uZ", static_cast<int>(d.ano), d.mes % 100u, d.dia % 100u,
           d.hora % 100u, d.minuto % 100u, d.segundo % 100u);
}

int64_t reconstruir(int64_t utcAcerto_s, uint64_t msAcerto, uint64_t msEvento) {
  int64_t diferencaMs = static_cast<int64_t>(msEvento) - static_cast<int64_t>(msAcerto);
  return utcAcerto_s + dividirParaBaixo(diferencaMs, 1000);
}

}  // namespace calendario
