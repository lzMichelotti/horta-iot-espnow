// Testes unitários da exportação em CSV (coordenador/lib/exportacao), no PC:
//   cd coordenador && pio test -e native
// CRCs de referência calculados com zlib.crc32 do Python.
#include <unity.h>

#include <cstring>

#include <exportacao.h>

using namespace exportacao;

void setUp() {}
void tearDown() {}

namespace {

// O mesmo registro de referência de test_registro (NÓ 1, boot 38, seq 3).
registro::Registro registroExemplo(uint8_t estados = 0, int16_t temp = 2166, bool horaValida = true) {
  registro::Metadados m{};
  m.horaValida = horaValida;
  m.fonteRelogio = 1;
  m.bootCoord = 8;
  m.utc_s = 1791297000;  // 2026-10-06T14:30:00Z
  m.desdeBoot_ms = 3600999;
  const uint8_t mac[6] = {0x88, 0x57, 0x21, 0x70, 0x93, 0x70};
  memcpy(m.mac, mac, 6);
  m.rssi = -22;
  m.ruido = -96;
  m.classe = protocolo::Classe::NOVO;
  m.perdidos = 2;
  protocolo::PacoteLeitura p{};
  p.versao = protocolo::VERSAO;
  p.tipo = static_cast<uint8_t>(protocolo::Tipo::LEITURA);
  p.boot = 38;
  p.seq = 3;
  p.temperatura_c100 = temp;
  p.umidade_ar_c100 = 7514;
  p.solo_mv = 1066;
  p.alimentacao_mv = 3386;
  p.estados = estados;
  p.motivo_boot = 8;
  p.acordado_ant_ms = 133;
  p.tentativas_ant = 1;
  p.flags = 0;
  return registro::montar(m, p);
}

const char* LINHA_REFERENCIA =
    "1234,88:57:21:70:93:70,no1,valida,2026-10-06T14:30:00Z,8,3600,novo,2,-22,-96,"
    "38,3,21.66,75.14,1066,44.8,3386,ok,ok,ok,ok,8,133,1,0";

}  // namespace

void test_crc32_valor_de_verificacao() {
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc32(0, reinterpret_cast<const uint8_t*>("123456789"), 9));
  TEST_ASSERT_EQUAL_HEX32(0, crc32(0, nullptr, 0));
}

void test_crc32_incremental() {
  const char* s = "123456789";
  uint32_t c = crc32(0, reinterpret_cast<const uint8_t*>(s), 4);
  c = crc32(c, reinterpret_cast<const uint8_t*>(s + 4), 5);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, c);
}

void test_linha_referencia() {
  char l[TAM_LINHA];
  size_t n = formatarLinha(l, 1234, registroExemplo(), "no1", filtro::Hora::VALIDA, 1791297000, true, 448);
  TEST_ASSERT_EQUAL_STRING(LINHA_REFERENCIA, l);
  TEST_ASSERT_EQUAL(strlen(LINHA_REFERENCIA), n);
}

// O CRC do arquivo (cabeçalho + linha, cada um com '\n') bate com o do Python.
void test_crc_do_conteudo_igual_ao_python() {
  char l[TAM_LINHA];
  formatarLinha(l, 1234, registroExemplo(), "no1", filtro::Hora::VALIDA, 1791297000, true, 448);
  uint32_t c = crc32(0, reinterpret_cast<const uint8_t*>(cabecalho()), strlen(cabecalho()));
  c = crc32(c, reinterpret_cast<const uint8_t*>("\n"), 1);
  c = crc32(c, reinterpret_cast<const uint8_t*>(l), strlen(l));
  c = crc32(c, reinterpret_cast<const uint8_t*>("\n"), 1);
  TEST_ASSERT_EQUAL_HEX32(0xFB74C6E9, c);
}

void test_numero_de_colunas_igual_ao_cabecalho() {
  auto colunas = [](const char* s) {
    int n = 1;
    for (; *s; s++) n += (*s == ',');
    return n;
  };
  char l[TAM_LINHA];
  formatarLinha(l, 1, registroExemplo(5), "x", filtro::Hora::SEM_HORA, 0, false, 0);  // células vazias
  TEST_ASSERT_EQUAL(colunas(cabecalho()), colunas(l));
  TEST_ASSERT_EQUAL(26, colunas(cabecalho()));
}

// Temperatura e umidade com estado erro: células vazias; sem hora e sem %.
void test_celulas_vazias() {
  char l[TAM_LINHA];
  formatarLinha(l, 7, registroExemplo(0x05), "no1", filtro::Hora::SEM_HORA, 0, false, 0);
  TEST_ASSERT_EQUAL_STRING(
      "7,88:57:21:70:93:70,no1,sem_hora,,8,3600,novo,2,-22,-96,"
      "38,3,,,1066,,3386,erro,erro,ok,ok,8,133,1,0",
      l);
}

// Fora de faixa: o valor sai, marcado na coluna de estado. Negativos sem float.
void test_fora_de_faixa_e_negativos() {
  char l[TAM_LINHA];
  formatarLinha(l, 1, registroExemplo(0x02, -5), "n", filtro::Hora::RECONSTRUIDA, 1791297000, true, 1000);
  TEST_ASSERT_NOT_NULL(strstr(l, ",reconstruida,2026-10-06T14:30:00Z,"));
  TEST_ASSERT_NOT_NULL(strstr(l, ",-0.05,75.14,1066,100.0,3386,fora_de_faixa,ok,ok,ok,"));
  formatarLinha(l, 1, registroExemplo(0, -1234), "n", filtro::Hora::VALIDA, 1791297000, true, 0);
  TEST_ASSERT_NOT_NULL(strstr(l, ",-12.34,75.14,1066,0.0,"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc32_valor_de_verificacao);
  RUN_TEST(test_crc32_incremental);
  RUN_TEST(test_linha_referencia);
  RUN_TEST(test_crc_do_conteudo_igual_ao_python);
  RUN_TEST(test_numero_de_colunas_igual_ao_cabecalho);
  RUN_TEST(test_celulas_vazias);
  RUN_TEST(test_fora_de_faixa_e_negativos);
  return UNITY_END();
}
