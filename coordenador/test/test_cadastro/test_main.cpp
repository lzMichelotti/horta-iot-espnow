// Testes unitários do cadastro de nós (coordenador/lib/cadastro), no PC:
//   cd coordenador && pio test -e native
#include <unity.h>

#include <cstring>

#include <cadastro.h>

using namespace cadastro;

void setUp() {}
void tearDown() {}

namespace {
const uint8_t MAC1[6] = {0x88, 0x57, 0x21, 0x70, 0x93, 0x70};
const uint8_t MAC2[6] = {0x02, 0, 0, 0, 0, 0x02};

No calibrado(uint16_t seco, uint16_t umido) {
  No n{};
  n.mvSeco = seco;
  n.mvUmido = umido;
  return n;
}
}  // namespace

// ---------------------------------------------------------------- Nome e MAC
void test_nome_valido() {
  TEST_ASSERT_TRUE(nomeValido("no1"));
  TEST_ASSERT_TRUE(nomeValido("canteiro_A-2"));
  TEST_ASSERT_TRUE(nomeValido("123456789012345"));    // 15
  TEST_ASSERT_FALSE(nomeValido("1234567890123456"));  // 16
  TEST_ASSERT_FALSE(nomeValido(""));
  TEST_ASSERT_FALSE(nomeValido("com espaco"));
  TEST_ASSERT_FALSE(nomeValido("acentuação"));
  TEST_ASSERT_FALSE(nomeValido("a,b"));  // quebraria o CSV
  TEST_ASSERT_FALSE(nomeValido("a\"b"));  // quebraria o JSON
  TEST_ASSERT_FALSE(nomeValido(nullptr));
}

void test_interpretar_mac() {
  uint8_t m[6];
  TEST_ASSERT_TRUE(interpretarMac("88:57:21:70:93:70", m));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(MAC1, m, 6);
  TEST_ASSERT_TRUE(interpretarMac("AA:bb:0C:dd:EE:ff", m));
  TEST_ASSERT_EQUAL_HEX8(0xAA, m[0]);
  TEST_ASSERT_EQUAL_HEX8(0x0C, m[2]);
  const char* invalidos[] = {"", "88:57:21:70:93", "88:57:21:70:93:7", "88:57:21:70:93:700", "88-57-21-70-93-70",
                             "88:57:21:70:93:7g", "88:57:21:70:93:70 "};
  for (const char* s : invalidos) TEST_ASSERT_FALSE_MESSAGE(interpretarMac(s, m), s);
  TEST_ASSERT_FALSE(interpretarMac(nullptr, m));
}

// ---------------------------------------------------------------- Umidade do solo
void test_umidade_solo() {
  No n = calibrado(1400, 655);  // valores de bancada (docs/sensores.md, 4.3)
  int16_t d = -1;
  TEST_ASSERT_TRUE(umidadeSoloDecimos(n, 1400, d));
  TEST_ASSERT_EQUAL_INT16(0, d);
  TEST_ASSERT_TRUE(umidadeSoloDecimos(n, 655, d));
  TEST_ASSERT_EQUAL_INT16(1000, d);
  TEST_ASSERT_TRUE(umidadeSoloDecimos(n, 1066, d));  // 334 / 745 = 44,83 %
  TEST_ASSERT_EQUAL_INT16(448, d);
  TEST_ASSERT_TRUE(umidadeSoloDecimos(n, 1028, d));  // 372 / 745 = 49,93 %
  TEST_ASSERT_EQUAL_INT16(499, d);
  TEST_ASSERT_TRUE(umidadeSoloDecimos(n, 1500, d));  // mais seco que o ponto seco
  TEST_ASSERT_EQUAL_INT16(0, d);
  TEST_ASSERT_TRUE(umidadeSoloDecimos(n, 500, d));  // mais úmido que o ponto úmido
  TEST_ASSERT_EQUAL_INT16(1000, d);
  TEST_ASSERT_TRUE(umidadeSoloDecimos(n, 0, d));
  TEST_ASSERT_EQUAL_INT16(1000, d);
}

void test_sem_calibracao() {
  int16_t d = 7;
  TEST_ASSERT_FALSE(umidadeSoloDecimos(calibrado(0, 0), 1000, d));
  TEST_ASSERT_FALSE(umidadeSoloDecimos(calibrado(1400, 0), 1000, d));  // só o seco
  TEST_ASSERT_FALSE(umidadeSoloDecimos(calibrado(0, 655), 1000, d));   // só o úmido
  TEST_ASSERT_EQUAL_INT16(7, d);
}

// ---------------------------------------------------------------- Tabela
void test_definir_nome_cadastra_e_renomeia() {
  Tabela t;
  TEST_ASSERT_EQUAL(Resultado::OK, t.definirNome(MAC1, "no1"));
  TEST_ASSERT_EQUAL(1, t.quantidade());
  TEST_ASSERT_EQUAL(Resultado::OK, t.definirNome(MAC1, "canteiro1"));
  TEST_ASSERT_EQUAL(1, t.quantidade());
  TEST_ASSERT_EQUAL_STRING("canteiro1", t.buscar(MAC1)->nome);
  TEST_ASSERT_EQUAL(Resultado::NOME_INVALIDO, t.definirNome(MAC2, "x y"));
  TEST_ASSERT_EQUAL(1, t.quantidade());
}

void test_tabela_cheia() {
  Tabela t;
  uint8_t mac[6] = {2, 0, 0, 0, 0, 0};
  for (uint8_t i = 0; i < MAX_NOS; i++) {
    mac[5] = i;
    TEST_ASSERT_EQUAL(Resultado::OK, t.definirNome(mac, "n"));
  }
  mac[5] = 99;
  TEST_ASSERT_EQUAL(Resultado::TABELA_CHEIA, t.definirNome(mac, "n"));
  mac[5] = 3;
  TEST_ASSERT_EQUAL(Resultado::OK, t.definirNome(mac, "renomeado"));  // existente: cabe
}

void test_calibracao() {
  Tabela t;
  TEST_ASSERT_EQUAL(Resultado::NAO_ENCONTRADO, t.definirCalibracao(MAC1, 1400, 655));
  t.definirNome(MAC1, "no1");
  TEST_ASSERT_EQUAL(Resultado::OK, t.definirCalibracao(MAC1, 1400, 655));
  TEST_ASSERT_TRUE(temCalibracao(*t.buscar(MAC1)));
  TEST_ASSERT_EQUAL(Resultado::ORDEM_INVALIDA, t.definirCalibracao(MAC1, 655, 1400));
  TEST_ASSERT_EQUAL(Resultado::ORDEM_INVALIDA, t.definirCalibracao(MAC1, 1000, 1000));
  TEST_ASSERT_EQUAL(Resultado::MV_INVALIDO, t.definirCalibracao(MAC1, 3301, 655));
  TEST_ASSERT_EQUAL(Resultado::MV_INVALIDO, t.definirCalibracao(MAC1, 1400, 0));
  TEST_ASSERT_EQUAL_UINT16(1400, t.buscar(MAC1)->mvSeco);  // erros não mudam nada
  TEST_ASSERT_EQUAL(Resultado::OK, t.removerCalibracao(MAC1));
  TEST_ASSERT_FALSE(temCalibracao(*t.buscar(MAC1)));
}

// Calibração em campo: um ponto de cada vez, em qualquer ordem.
void test_definir_ponto() {
  Tabela t;
  t.definirNome(MAC1, "no1");
  TEST_ASSERT_EQUAL(Resultado::OK, t.definirPonto(MAC1, Ponto::UMIDO, 700));
  TEST_ASSERT_FALSE(temCalibracao(*t.buscar(MAC1)));
  TEST_ASSERT_EQUAL(Resultado::ORDEM_INVALIDA, t.definirPonto(MAC1, Ponto::SECO, 650));  // seco < úmido
  TEST_ASSERT_EQUAL(Resultado::OK, t.definirPonto(MAC1, Ponto::SECO, 1450));
  TEST_ASSERT_TRUE(temCalibracao(*t.buscar(MAC1)));
  TEST_ASSERT_EQUAL(Resultado::OK, t.definirPonto(MAC1, Ponto::UMIDO, 680));  // recalibrar um ponto
  TEST_ASSERT_EQUAL_UINT16(680, t.buscar(MAC1)->mvUmido);
  TEST_ASSERT_EQUAL(Resultado::MV_INVALIDO, t.definirPonto(MAC1, Ponto::SECO, 0));
  TEST_ASSERT_EQUAL(Resultado::NAO_ENCONTRADO, t.definirPonto(MAC2, Ponto::SECO, 1000));
}

void test_remover() {
  Tabela t;
  t.definirNome(MAC1, "no1");
  t.definirNome(MAC2, "no2");
  TEST_ASSERT_EQUAL(Resultado::OK, t.remover(MAC1));
  TEST_ASSERT_EQUAL(1, t.quantidade());
  TEST_ASSERT_NULL(t.buscar(MAC1));
  TEST_ASSERT_EQUAL_STRING("no2", t.buscar(MAC2)->nome);
  TEST_ASSERT_EQUAL(Resultado::NAO_ENCONTRADO, t.remover(MAC1));
}

// ---------------------------------------------------------------- Blob
void test_blob_ida_e_volta() {
  Tabela t;
  t.definirNome(MAC1, "no1");
  t.definirCalibracao(MAC1, 1400, 655);
  t.definirNome(MAC2, "canteiro_B");
  t.definirPonto(MAC2, Ponto::SECO, 1500);  // calibração parcial também é guardada
  uint8_t blob[Tabela::TAM_BLOB];
  t.serializar(blob);
  TEST_ASSERT_EQUAL(212, Tabela::TAM_BLOB);
  TEST_ASSERT_EQUAL_UINT8(1, blob[0]);
  TEST_ASSERT_EQUAL_UINT8(2, blob[1]);
  // Primeira entrada: mac no byte 4, nome no 10, seco no 26 (little-endian).
  TEST_ASSERT_EQUAL_HEX8_ARRAY(MAC1, blob + 4, 6);
  TEST_ASSERT_EQUAL_STRING("no1", reinterpret_cast<const char*>(blob + 10));
  TEST_ASSERT_EQUAL_HEX8(0x78, blob[26]);  // 1400 = 0x0578
  TEST_ASSERT_EQUAL_HEX8(0x05, blob[27]);

  Tabela u;
  TEST_ASSERT_TRUE(u.desserializar(blob, sizeof(blob)));
  TEST_ASSERT_EQUAL(2, u.quantidade());
  TEST_ASSERT_EQUAL_UINT16(655, u.buscar(MAC1)->mvUmido);
  TEST_ASSERT_EQUAL_STRING("canteiro_B", u.buscar(MAC2)->nome);
  TEST_ASSERT_EQUAL_UINT16(1500, u.buscar(MAC2)->mvSeco);
  TEST_ASSERT_EQUAL_UINT16(0, u.buscar(MAC2)->mvUmido);
}

// Blob inválido: devolve false e a tabela atual fica como estava.
void test_blob_invalido_nao_muda_nada() {
  Tabela base;
  base.definirNome(MAC1, "no1");
  uint8_t blob[Tabela::TAM_BLOB];
  base.serializar(blob);

  Tabela t;
  t.definirNome(MAC2, "existente");
  uint8_t ruim[Tabela::TAM_BLOB];

  TEST_ASSERT_FALSE(t.desserializar(blob, sizeof(blob) - 1));  // tamanho
  memcpy(ruim, blob, sizeof(ruim));
  ruim[0] = 2;  // versão
  TEST_ASSERT_FALSE(t.desserializar(ruim, sizeof(ruim)));
  memcpy(ruim, blob, sizeof(ruim));
  ruim[1] = MAX_NOS + 1;  // quantidade
  TEST_ASSERT_FALSE(t.desserializar(ruim, sizeof(ruim)));
  memcpy(ruim, blob, sizeof(ruim));
  ruim[10] = ' ';  // nome com espaço
  TEST_ASSERT_FALSE(t.desserializar(ruim, sizeof(ruim)));
  memcpy(ruim, blob, sizeof(ruim));
  memset(ruim + 10, 'a', TAM_NOME);  // nome sem '\0'
  TEST_ASSERT_FALSE(t.desserializar(ruim, sizeof(ruim)));
  memcpy(ruim, blob, sizeof(ruim));
  ruim[26] = 0x10;  // seco = 0x0010 = 16 mV
  ruim[27] = 0x00;
  ruim[28] = 0xE8;  // úmido = 0x03E8 = 1000 mV > seco
  ruim[29] = 0x03;
  TEST_ASSERT_FALSE(t.desserializar(ruim, sizeof(ruim)));
  memcpy(ruim, blob, sizeof(ruim));
  ruim[1] = 2;  // segunda entrada idêntica à primeira: MAC repetido
  memcpy(ruim + 4 + Tabela::TAM_ENTRADA, ruim + 4, Tabela::TAM_ENTRADA);
  TEST_ASSERT_FALSE(t.desserializar(ruim, sizeof(ruim)));
  TEST_ASSERT_FALSE(t.desserializar(nullptr, sizeof(blob)));

  TEST_ASSERT_EQUAL(1, t.quantidade());
  TEST_ASSERT_EQUAL_STRING("existente", t.buscar(MAC2)->nome);
}

void test_blob_tabela_vazia() {
  Tabela t;
  uint8_t blob[Tabela::TAM_BLOB];
  t.serializar(blob);
  Tabela u;
  u.definirNome(MAC1, "no1");
  TEST_ASSERT_TRUE(u.desserializar(blob, sizeof(blob)));
  TEST_ASSERT_EQUAL(0, u.quantidade());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_nome_valido);
  RUN_TEST(test_interpretar_mac);
  RUN_TEST(test_umidade_solo);
  RUN_TEST(test_sem_calibracao);
  RUN_TEST(test_definir_nome_cadastra_e_renomeia);
  RUN_TEST(test_tabela_cheia);
  RUN_TEST(test_calibracao);
  RUN_TEST(test_definir_ponto);
  RUN_TEST(test_remover);
  RUN_TEST(test_blob_ida_e_volta);
  RUN_TEST(test_blob_invalido_nao_muda_nada);
  RUN_TEST(test_blob_tabela_vazia);
  return UNITY_END();
}
