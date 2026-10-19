// Testes da lib/json (escritor e leitor da API HTTP), no PC:
//   cd coordenador && pio test -e native -f test_json
#include <unity.h>

#include <cstring>
#include <string>

#include <json.h>

using namespace json;

void setUp() {}
void tearDown() {}

namespace {

void paraString(const char* d, size_t n, void* ctx) { static_cast<std::string*>(ctx)->append(d, n); }

int tokens(const char* s, Token* t, size_t max = 32) { return analisar(s, strlen(s), t, max); }

}  // namespace

// ---------------------------------------------------------------- Escritor
void test_objeto_com_virgulas_e_aninhamento() {
  std::string s;
  Escritor e(paraString, &s);
  e.objeto().campo("a", int64_t(1)).chave("b").lista().inteiro(2).inteiro(3).objeto().fimObjeto().fimLista()
      .campoNulo("c").campoBool("d", false).fimObjeto();
  TEST_ASSERT_EQUAL_STRING("{\"a\":1,\"b\":[2,3,{}],\"c\":null,\"d\":false}", s.c_str());
  TEST_ASSERT_TRUE(e.ok());
  TEST_ASSERT_EQUAL_UINT(0, e.profundidade());
}

void test_escape_de_texto() {
  std::string s;
  Escritor e(paraString, &s);
  e.texto("a\"b\\c\nd\x01" "\xC3\xA7");  // aspas, barra, \n, controle e "ç" em UTF-8
  TEST_ASSERT_EQUAL_STRING("\"a\\\"b\\\\c\\nd\\u0001\xC3\xA7\"", s.c_str());
}

void test_decimal_sem_float() {
  std::string s;
  Escritor e(paraString, &s);
  e.lista().decimal(2166, 2).decimal(-5, 2).decimal(-1234, 2).decimal(448, 1).decimal(0, 2).decimal(7, 0).fimLista();
  TEST_ASSERT_EQUAL_STRING("[21.66,-0.05,-12.34,44.8,0.00,7]", s.c_str());
}

void test_inteiros_nos_limites() {
  std::string s;
  Escritor e(paraString, &s);
  e.lista().inteiro(INT64_MIN + 1).natural(UINT64_MAX).inteiro(-1).fimLista();
  TEST_ASSERT_EQUAL_STRING("[-9223372036854775807,18446744073709551615,-1]", s.c_str());
}

void test_estrutura_errada_e_detectada() {
  std::string s;
  Escritor e(paraString, &s);
  e.fimObjeto();
  TEST_ASSERT_FALSE(e.ok());
  Escritor f(paraString, &s);
  for (int i = 0; i < 20; i++) f.lista();  // além de 16 níveis
  TEST_ASSERT_FALSE(f.ok());
}

// ---------------------------------------------------------------- Leitor
void test_le_objeto_e_busca_campos() {
  const char* s = "{ \"utc\" : \"2026-10-19T14:30:00Z\", \"n\": -42, \"cal\": {\"seco\": 1400, \"umido\": null}, "
                  "\"x\": [1, {\"y\": 2}], \"ok\": true }";
  Token t[32];
  int n = tokens(s, t);
  TEST_ASSERT_GREATER_THAN(0, n);
  char txt[32];
  int k = buscar(s, t, n, 0, "utc");
  TEST_ASSERT_TRUE(comoTexto(s, t[k], txt, sizeof(txt)));
  TEST_ASSERT_EQUAL_STRING("2026-10-19T14:30:00Z", txt);
  int64_t v = 0;
  TEST_ASSERT_TRUE(comoInteiro(s, t[buscar(s, t, n, 0, "n")], v));
  TEST_ASSERT_EQUAL_INT64(-42, v);
  int cal = buscar(s, t, n, 0, "cal");
  TEST_ASSERT_TRUE(comoInteiro(s, t[buscar(s, t, n, cal, "seco")], v));
  TEST_ASSERT_EQUAL_INT64(1400, v);
  TEST_ASSERT_TRUE(ehNulo(s, t[buscar(s, t, n, cal, "umido")]));
  bool b = false;
  TEST_ASSERT_TRUE(comoBool(s, t[buscar(s, t, n, 0, "ok")], b));
  TEST_ASSERT_TRUE(b);
  TEST_ASSERT_EQUAL_INT(-1, buscar(s, t, n, 0, "y"));       // só membros diretos
  TEST_ASSERT_EQUAL_INT(-1, buscar(s, t, n, 0, "inexistente"));
}

void test_rejeita_json_invalido() {
  Token t[32];
  const char* invalidos[] = {"", "{", "{\"a\":}", "{\"a\" 1}", "{\"a\":1,}", "[1,]", "{a:1}", "\"sem fim",
                             "01", "1.", "-", "tru", "{\"a\":1} lixo", "\"\x01\"", "\"\\x\"", "\"\\u12\"",
                             "[[[[[[[[[1]]]]]]]]]"};  // 9 níveis
  for (const char* s : invalidos) TEST_ASSERT_EQUAL_INT_MESSAGE(-1, tokens(s, t), s);
}

void test_tokens_demais_falham() {
  Token t[3];
  TEST_ASSERT_EQUAL_INT(-1, tokens("[1,2,3,4]", t, 3));
  TEST_ASSERT_EQUAL_INT(3, tokens("[1,2]", t, 3));
}

void test_conversoes_conferem_o_tipo() {
  const char* s = "{\"a\":\"12\",\"b\":1.5,\"c\":12,\"d\":\"\\u0041\\\"\",\"e\":\"\\u00e7\"}";
  Token t[16];
  int n = tokens(s, t, 16);
  int64_t v;
  char txt[8];
  TEST_ASSERT_FALSE(comoInteiro(s, t[buscar(s, t, n, 0, "a")], v));  // texto não é número
  TEST_ASSERT_FALSE(comoInteiro(s, t[buscar(s, t, n, 0, "b")], v));  // fração
  TEST_ASSERT_FALSE(comoTexto(s, t[buscar(s, t, n, 0, "c")], txt, sizeof(txt)));
  TEST_ASSERT_TRUE(comoTexto(s, t[buscar(s, t, n, 0, "d")], txt, sizeof(txt)));
  TEST_ASSERT_EQUAL_STRING("A\"", txt);
  TEST_ASSERT_FALSE(comoTexto(s, t[buscar(s, t, n, 0, "e")], txt, sizeof(txt)));  // \u fora do ASCII
  TEST_ASSERT_FALSE(comoTexto(s, t[buscar(s, t, n, 0, "d")], txt, 2));             // não cabe
}

void test_ida_e_volta_escritor_leitor() {
  std::string s;
  Escritor e(paraString, &s);
  e.objeto().campo("nome", "can\"teiro\\2").campo("mv", int64_t(655)).fimObjeto();
  Token t[8];
  int n = analisar(s.data(), s.size(), t, 8);
  TEST_ASSERT_GREATER_THAN(0, n);
  char txt[32];
  TEST_ASSERT_TRUE(comoTexto(s.data(), t[buscar(s.data(), t, n, 0, "nome")], txt, sizeof(txt)));
  TEST_ASSERT_EQUAL_STRING("can\"teiro\\2", txt);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_objeto_com_virgulas_e_aninhamento);
  RUN_TEST(test_escape_de_texto);
  RUN_TEST(test_decimal_sem_float);
  RUN_TEST(test_inteiros_nos_limites);
  RUN_TEST(test_estrutura_errada_e_detectada);
  RUN_TEST(test_le_objeto_e_busca_campos);
  RUN_TEST(test_rejeita_json_invalido);
  RUN_TEST(test_tokens_demais_falham);
  RUN_TEST(test_conversoes_conferem_o_tipo);
  RUN_TEST(test_ida_e_volta_escritor_leitor);
  return UNITY_END();
}
