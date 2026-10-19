// Testes do formato JSON das leituras (lib/apijson), no PC:
//   cd coordenador && pio test -e native -f test_apijson
#include <unity.h>

#include <cstring>
#include <string>

#include <apijson.h>
#include <json.h>

void setUp() {}
void tearDown() {}

namespace {

void paraString(const char* d, size_t n, void* ctx) { static_cast<std::string*>(ctx)->append(d, n); }

// O mesmo registro de referência de test_exportacao (NÓ 1, boot 38, seq 3).
registro::Registro registroExemplo(uint8_t estados = 0, int16_t temp = 2166, uint32_t idade = 0) {
  registro::Metadados m{};
  m.horaValida = true;
  m.fonteRelogio = 1;
  m.bootCoord = 8;
  m.utc_s = 1791297000;
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
  p.idade_s = idade;
  return registro::montar(m, p);
}

std::string formatar(const registro::Registro& r, filtro::Hora h, uint32_t utc, bool temPct, int16_t pct) {
  std::string s;
  json::Escritor e(paraString, &s);
  apijson::leitura(e, 26104, r, "no1", h, utc, temPct, pct);
  TEST_ASSERT_TRUE(e.ok());
  return s;
}

}  // namespace

void test_leitura_referencia() {
  std::string s = formatar(registroExemplo(), filtro::Hora::VALIDA, 1791297000, true, 448);
  TEST_ASSERT_EQUAL_STRING(
      "{\"id\":26104,\"mac\":\"88:57:21:70:93:70\",\"nome\":\"no1\","
      "\"hora\":{\"utc\":\"2026-10-06T14:30:00Z\",\"origem\":\"valida\"},\"idade_s\":0,"
      "\"temperatura_c\":21.66,\"umidade_ar_pct\":75.14,\"solo_mv\":1066,\"solo_pct\":44.8,\"alimentacao_mv\":3386,"
      "\"estados\":{\"temperatura\":\"ok\",\"umidade_ar\":\"ok\",\"solo\":\"ok\",\"alimentacao\":\"ok\"},"
      "\"rssi_dbm\":-22,\"no_boot\":38,\"no_seq\":3,\"perdidos_antes\":2}",
      s.c_str());
}

void test_erro_vira_null_e_sem_hora() {
  // temperatura e umidade em erro (estados = 0x05), sem hora, sem calibração
  std::string s = formatar(registroExemplo(0x05), filtro::Hora::SEM_HORA, 0, false, 0);
  TEST_ASSERT_NOT_NULL(strstr(s.c_str(), "\"hora\":{\"utc\":null,\"origem\":\"sem_hora\"}"));
  TEST_ASSERT_NOT_NULL(strstr(s.c_str(), "\"temperatura_c\":null,\"umidade_ar_pct\":null,\"solo_mv\":1066,"
                                         "\"solo_pct\":null"));
  TEST_ASSERT_NOT_NULL(strstr(s.c_str(), "\"temperatura\":\"erro\",\"umidade_ar\":\"erro\""));
}

void test_fora_de_faixa_tem_valor_e_negativos() {
  std::string s = formatar(registroExemplo(0x02, -5, 3600), filtro::Hora::RECONSTRUIDA, 1791293400, true, 1000);
  TEST_ASSERT_NOT_NULL(strstr(s.c_str(), "\"origem\":\"reconstruida\""));
  TEST_ASSERT_NOT_NULL(strstr(s.c_str(), "\"idade_s\":3600"));
  TEST_ASSERT_NOT_NULL(strstr(s.c_str(), "\"temperatura_c\":-0.05"));
  TEST_ASSERT_NOT_NULL(strstr(s.c_str(), "\"solo_pct\":100.0"));
  TEST_ASSERT_NOT_NULL(strstr(s.c_str(), "\"temperatura\":\"fora_de_faixa\""));
}

void test_saida_e_json_valido() {
  std::string s = formatar(registroExemplo(0x05), filtro::Hora::SEM_HORA, 0, false, 0);
  json::Token t[64];
  TEST_ASSERT_GREATER_THAN(0, json::analisar(s.data(), s.size(), t, 64));
}

void test_mac_em_minusculas() {
  const uint8_t m[6] = {0xAA, 0x0B, 0x00, 0xFF, 0x10, 0x01};
  char txt[apijson::TAM_MAC];
  apijson::mac(txt, m);
  TEST_ASSERT_EQUAL_STRING("aa:0b:00:ff:10:01", txt);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_leitura_referencia);
  RUN_TEST(test_erro_vira_null_e_sem_hora);
  RUN_TEST(test_fora_de_faixa_tem_valor_e_negativos);
  RUN_TEST(test_saida_e_json_valido);
  RUN_TEST(test_mac_em_minusculas);
  return UNITY_END();
}
