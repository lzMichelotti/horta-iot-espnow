// Teste na placa do HMAC-SHA256 real (protocolo::hmacSha256, mbedTLS do ESP-IDF):
//   cd no_sensor && pio test -e esp32doit-devkit-v1 -f test_hmac_esp32
// Vetores: RFC 4231 (casos 1, 2 e 6), conferidos também com o hmac do Python.
// Mede o tempo de assinar e de validar um pacote (média de 1000 execuções).
#include <Arduino.h>
#include <esp_timer.h>
#include <unity.h>

#include <cstring>

#include <protocolo.h>

using namespace protocolo;

void setUp() {}
void tearDown() {}

namespace {

void conferir(const uint8_t* chave, size_t nChave, const char* msg, const uint8_t esperado[TAMANHO_HMAC]) {
  uint8_t saida[TAMANHO_HMAC];
  TEST_ASSERT_TRUE(hmacSha256(chave, nChave, reinterpret_cast<const uint8_t*>(msg), strlen(msg), saida));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(esperado, saida, TAMANHO_HMAC);
}

const uint8_t CHAVE[TAMANHO_CHAVE] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                                      17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

PacoteLeitura pacoteExemplo() {
  DadosLeitura d{};
  d.boot = 38;
  d.seq = 3;
  d.temperaturaC = 21.66f;
  d.umidadeArPct = 75.14f;
  d.soloMv = 1066;
  d.alimentacaoMv = 3386;
  return montarLeitura(d);
}

}  // namespace

void test_rfc4231_caso1() {
  uint8_t chave[20];
  memset(chave, 0x0b, sizeof(chave));
  const uint8_t esperado[32] = {0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf,
                                0xce, 0xaf, 0x0b, 0xf1, 0x2b, 0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83,
                                0x3d, 0xa7, 0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7};
  conferir(chave, sizeof(chave), "Hi There", esperado);
}

void test_rfc4231_caso2() {
  const uint8_t esperado[32] = {0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e, 0x6a, 0x04, 0x24,
                                0x26, 0x08, 0x95, 0x75, 0xc7, 0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27,
                                0x39, 0x83, 0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43};
  conferir(reinterpret_cast<const uint8_t*>("Jefe"), 4, "what do ya want for nothing?", esperado);
}

void test_rfc4231_caso6_chave_maior_que_o_bloco() {
  uint8_t chave[131];
  memset(chave, 0xaa, sizeof(chave));
  const uint8_t esperado[32] = {0x60, 0xe4, 0x31, 0x59, 0x1e, 0xe0, 0xb6, 0x7f, 0x0d, 0x8a, 0x26,
                                0xaa, 0xcb, 0xf5, 0xb7, 0x7f, 0x8e, 0x0b, 0xc6, 0x21, 0x37, 0x28,
                                0xc5, 0x14, 0x05, 0x46, 0x04, 0x0f, 0x0e, 0xe3, 0x7f, 0x54};
  conferir(chave, sizeof(chave), "Test Using Larger Than Block-Size Key - Hash Key First", esperado);
}

void test_ida_e_volta_e_tempo() {
  PacoteLeitura p = pacoteExemplo();
  PacoteAssinado a{};
  constexpr int N = 1000;

  int64_t t0 = esp_timer_get_time();
  for (int i = 0; i < N; i++) TEST_ASSERT_TRUE(assinar(p, CHAVE, hmacSha256, a));
  int64_t tAssinar = esp_timer_get_time() - t0;

  uint8_t b[sizeof(a)];
  memcpy(b, &a, sizeof(b));
  PacoteLeitura r{};
  t0 = esp_timer_get_time();
  for (int i = 0; i < N; i++) TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validar(b, sizeof(b), CHAVE, hmacSha256, r));
  int64_t tValidar = esp_timer_get_time() - t0;

  const uint8_t mac[TAMANHO_MAC] = {0x88, 0x57, 0x21, 0x70, 0x93, 0x70};
  uint8_t k[TAMANHO_CHAVE];
  t0 = esp_timer_get_time();
  for (int i = 0; i < N; i++) TEST_ASSERT_TRUE(derivarChaveNo(CHAVE, mac, hmacSha256, k));
  int64_t tDerivar = esp_timer_get_time() - t0;

  char msg[120];
  snprintf(msg, sizeof(msg), "[TEMPO] assinar %.1f us, validar %.1f us, derivar chave %.1f us (media de %d, %lu MHz)",
           tAssinar / (double)N, tValidar / (double)N, tDerivar / (double)N, N, (unsigned long)getCpuFrequencyMhz());
  TEST_MESSAGE(msg);

  b[10] ^= 0x01;
  TEST_ASSERT_EQUAL(Rejeicao::ASSINATURA_INVALIDA, validar(b, sizeof(b), CHAVE, hmacSha256, r));
}

void setup() {
  delay(2000);  // tempo para o monitor de testes abrir a serial
  UNITY_BEGIN();
  RUN_TEST(test_rfc4231_caso1);
  RUN_TEST(test_rfc4231_caso2);
  RUN_TEST(test_rfc4231_caso6_chave_maior_que_o_bloco);
  RUN_TEST(test_ida_e_volta_e_tempo);
  UNITY_END();
}

void loop() {}
