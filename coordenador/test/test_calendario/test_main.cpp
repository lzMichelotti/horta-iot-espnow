// Testes unitários do calendário (coordenador/lib/calendario), rodados no PC:
//   cd coordenador && pio test -e native
// Valores de referência conferidos com o datetime do Python (UTC).
#include <unity.h>

#include <cstring>

#include <calendario.h>

using namespace calendario;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- Datas
void test_dias_desde_epoch_referencias() {
  TEST_ASSERT_EQUAL_INT64(0, diasDesdeEpoch(1970, 1, 1));
  TEST_ASSERT_EQUAL_INT64(-1, diasDesdeEpoch(1969, 12, 31));
  TEST_ASSERT_EQUAL_INT64(11017, diasDesdeEpoch(2000, 3, 1));
  TEST_ASSERT_EQUAL_INT64(20454, diasDesdeEpoch(2026, 1, 1));
  TEST_ASSERT_EQUAL_INT64(47482, diasDesdeEpoch(2100, 1, 1));
}

void test_bissexto() {
  TEST_ASSERT_TRUE(bissexto(2024));
  TEST_ASSERT_TRUE(bissexto(2000));   // divisível por 400
  TEST_ASSERT_FALSE(bissexto(2100));  // divisível por 100, não por 400
  TEST_ASSERT_FALSE(bissexto(2026));
  TEST_ASSERT_EQUAL_UINT8(29, diasNoMes(2024, 2));
  TEST_ASSERT_EQUAL_UINT8(28, diasNoMes(2100, 2));
  TEST_ASSERT_EQUAL_UINT8(0, diasNoMes(2026, 13));
}

void test_para_epoch_referencias() {
  TEST_ASSERT_EQUAL_INT64(1791297000, paraEpoch({2026, 10, 6, 14, 30, 0}));
  TEST_ASSERT_EQUAL_INT64(1709251199, paraEpoch({2024, 2, 29, 23, 59, 59}));
  TEST_ASSERT_EQUAL_INT64(-1, paraEpoch({1969, 12, 31, 23, 59, 59}));
}

// Ida e volta para um dia de cada vez de 1970 a 2100, mais horas do dia.
void test_ida_e_volta() {
  for (int64_t dia = 0; dia <= 47482; dia++) {
    int64_t t = dia * SEGUNDOS_POR_DIA + (dia * 7919) % SEGUNDOS_POR_DIA;
    DataHora d = deEpoch(t);
    TEST_ASSERT_TRUE(dataValida(d));
    TEST_ASSERT_EQUAL_INT64(t, paraEpoch(d));
  }
}

void test_de_epoch_negativo() {
  DataHora d = deEpoch(-1);
  TEST_ASSERT_EQUAL_INT32(1969, d.ano);
  TEST_ASSERT_EQUAL_UINT8(12, d.mes);
  TEST_ASSERT_EQUAL_UINT8(31, d.dia);
  TEST_ASSERT_EQUAL_UINT8(23, d.hora);
  TEST_ASSERT_EQUAL_UINT8(59, d.minuto);
  TEST_ASSERT_EQUAL_UINT8(59, d.segundo);
}

// ---------------------------------------------------------------- interpretar
void test_interpretar_epoch() {
  int64_t t = 0;
  TEST_ASSERT_TRUE(interpretar("1791297000", t));
  TEST_ASSERT_EQUAL_INT64(1791297000, t);
  TEST_ASSERT_TRUE(interpretar("0", t));
  TEST_ASSERT_EQUAL_INT64(0, t);
}

void test_interpretar_iso() {
  int64_t t = 0;
  TEST_ASSERT_TRUE(interpretar("2026-10-06T14:30:00Z", t));
  TEST_ASSERT_EQUAL_INT64(1791297000, t);
  TEST_ASSERT_TRUE(interpretar("2024-02-29T23:59:59Z", t));
  TEST_ASSERT_EQUAL_INT64(1709251199, t);
}

void test_interpretar_rejeita() {
  const char* invalidos[] = {
      "",
      "abc",
      "-5",                     // epoch negativo
      "12a4",                   // letra no epoch
      "1234567890123",          // 13 dígitos
      "2026-10-06 14:30:00Z",   // espaço no lugar do T
      "2026-10-06T14:30:00",    // sem o Z (fuso ambíguo)
      "2026-10-06T14:30:00+00:00",
      "2026-10-06T14:30:00Zx",  // lixo no fim
      "2026-10-6T14:30:00Z",    // dia com 1 dígito
      "2026-13-01T00:00:00Z",   // mês 13
      "2026-02-29T00:00:00Z",   // 2026 não é bissexto
      "2026-04-31T00:00:00Z",   // abril tem 30 dias
      "2026-10-06T24:00:00Z",
      "2026-10-06T23:60:00Z",
      "2026-10-06T23:59:60Z",   // segundo bissexto
      "202-10-06T14:30:00Z",    // ano com 3 dígitos
      "1-2",
  };
  for (const char* s : invalidos) {
    int64_t t = 123;
    TEST_ASSERT_FALSE_MESSAGE(interpretar(s, t), s);
  }
  int64_t t = 0;
  TEST_ASSERT_FALSE(interpretar(nullptr, t));
}

// ---------------------------------------------------------------- formatarIso
void test_formatar_iso() {
  char s[TAM_ISO];
  formatarIso(1791297000, s);
  TEST_ASSERT_EQUAL_STRING("2026-10-06T14:30:00Z", s);
  formatarIso(0, s);
  TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:00Z", s);
  formatarIso(INT64_C(400000000000), s);  // ano ~14645
  TEST_ASSERT_EQUAL_STRING("fora-de-faixa", s);
}

void test_formatar_e_interpretar_sao_inversos() {
  char s[TAM_ISO];
  int64_t t = 0;
  formatarIso(1709251199, s);
  TEST_ASSERT_TRUE(interpretar(s, t));
  TEST_ASSERT_EQUAL_INT64(1709251199, t);
}

// ---------------------------------------------------------------- reconstruir
void test_reconstruir() {
  // Acerto: 1000 s UTC quando o boot tinha 5000 ms.
  TEST_ASSERT_EQUAL_INT64(1000, reconstruir(1000, 5000, 5000));
  TEST_ASSERT_EQUAL_INT64(997, reconstruir(1000, 5000, 2000));   // 3 s antes
  TEST_ASSERT_EQUAL_INT64(998, reconstruir(1000, 5000, 3500));   // 1,5 s antes: para baixo
  TEST_ASSERT_EQUAL_INT64(1001, reconstruir(1000, 5000, 6999));  // 1,999 s depois
  TEST_ASSERT_EQUAL_INT64(995, reconstruir(1000, 5000, 0));      // o próprio boot
  // Coordenador ligado há 2 h sem hora; acerto às 10:00 reconstrói as 08:00.
  TEST_ASSERT_EQUAL_INT64(1791297000 - 7200, reconstruir(1791297000, 7200000ULL + 60000, 60000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_dias_desde_epoch_referencias);
  RUN_TEST(test_bissexto);
  RUN_TEST(test_para_epoch_referencias);
  RUN_TEST(test_ida_e_volta);
  RUN_TEST(test_de_epoch_negativo);
  RUN_TEST(test_interpretar_epoch);
  RUN_TEST(test_interpretar_iso);
  RUN_TEST(test_interpretar_rejeita);
  RUN_TEST(test_formatar_iso);
  RUN_TEST(test_formatar_e_interpretar_sao_inversos);
  RUN_TEST(test_reconstruir);
  return UNITY_END();
}
