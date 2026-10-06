// Testes unitários do registro do histórico (coordenador/lib/registro), no PC:
//   cd coordenador && pio test -e native
// Os bytes de referência foram gerados à parte, em Python (struct.pack +
// binascii.crc_hqx), para conferir o layout sem depender do próprio C++.
#include <unity.h>

#include <cstring>

#include <registro.h>

using namespace registro;

void setUp() {}
void tearDown() {}

namespace {

// Pacote real do NÓ 1 (boot 38, seq 3), capturado no passo 3.
protocolo::PacoteLeitura pacoteExemplo() {
  protocolo::PacoteLeitura p{};
  p.versao = protocolo::VERSAO;
  p.tipo = static_cast<uint8_t>(protocolo::Tipo::LEITURA);
  p.boot = 38;
  p.seq = 3;
  p.temperatura_c100 = 2166;
  p.umidade_ar_c100 = 7514;
  p.solo_mv = 1066;
  p.alimentacao_mv = 3386;
  p.estados = 0;
  p.motivo_boot = 8;
  p.acordado_ant_ms = 133;
  p.tentativas_ant = 1;
  p.flags = 0;
  return p;
}

Metadados metadadosExemplo() {
  Metadados m{};
  m.horaValida = true;
  m.fonteRelogio = 1;  // manual
  m.bootCoord = 8;
  m.utc_s = 1791297000;  // 2026-10-06T14:30:00Z
  m.desdeBoot_ms = 3600999;
  const uint8_t mac[6] = {0x88, 0x57, 0x21, 0x70, 0x93, 0x70};
  memcpy(m.mac, mac, 6);
  m.rssi = -22;
  m.ruido = -96;
  m.classe = protocolo::Classe::NOVO;
  m.perdidos = 2;
  return m;
}

const uint8_t REFERENCIA[48] = {
    0x01, 0x03, 0x08, 0x00, 0xE8, 0x05, 0xC5, 0x6A, 0x10, 0x0E, 0x00, 0x00, 0x88, 0x57, 0x21, 0x70,
    0x93, 0x70, 0xEA, 0xA0, 0x01, 0x00, 0x02, 0x00, 0x01, 0x01, 0x26, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x76, 0x08, 0x5A, 0x1D, 0x2A, 0x04, 0x3A, 0x0D, 0x00, 0x08, 0x85, 0x00, 0x01, 0x00, 0x8F, 0x3F,
};

const uint8_t* bytesDe(const Registro& r) { return reinterpret_cast<const uint8_t*>(&r); }

}  // namespace

// ---------------------------------------------------------------- CRC
void test_crc_valor_de_verificacao() {
  const char* s = "123456789";
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16(reinterpret_cast<const uint8_t*>(s), 9));
}

void test_crc_vazio_e_valor_inicial() { TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16(nullptr, 0)); }

// ---------------------------------------------------------------- Montagem
void test_montar_igual_a_referencia_python() {
  Registro r = montar(metadadosExemplo(), pacoteExemplo());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(REFERENCIA, bytesDe(r), sizeof(REFERENCIA));
}

void test_montar_hora_invalida_zera_utc() {
  Metadados m = metadadosExemplo();
  m.horaValida = false;
  m.fonteRelogio = 0;
  Registro r = montar(m, pacoteExemplo());
  TEST_ASSERT_FALSE(horaValida(r));
  TEST_ASSERT_EQUAL_UINT32(0, r.utc_s);
  TEST_ASSERT_EQUAL_UINT8(0, fonteRelogio(r));
  TEST_ASSERT_EQUAL_UINT32(3600, r.desde_boot_s);  // continua disponível para reconstruir
}

void test_montar_hora_fora_do_uint32() {
  Metadados m = metadadosExemplo();
  m.utc_s = INT64_C(4294967296);  // 2106-02-07T06:28:16Z
  Registro r = montar(m, pacoteExemplo());
  TEST_ASSERT_FALSE(horaValida(r));
  TEST_ASSERT_EQUAL_UINT32(0, r.utc_s);
  m.utc_s = -1;
  r = montar(m, pacoteExemplo());
  TEST_ASSERT_FALSE(horaValida(r));
}

void test_montar_satura() {
  Metadados m = metadadosExemplo();
  m.perdidos = 70000;
  m.desdeBoot_ms = UINT64_C(5000000000000);  // ~158 anos
  Registro r = montar(m, pacoteExemplo());
  TEST_ASSERT_EQUAL_UINT16(65535, r.perdidos);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, r.desde_boot_s);
}

void test_montar_fonte_e_bits_reservados() {
  Metadados m = metadadosExemplo();
  for (uint8_t f = 0; f < 4; f++) {
    m.fonteRelogio = f;
    Registro r = montar(m, pacoteExemplo());
    TEST_ASSERT_EQUAL_UINT8(f, fonteRelogio(r));
    TEST_ASSERT_TRUE(horaValida(r));
    TEST_ASSERT_EQUAL_HEX8(0, r.flags & 0xF8);
  }
  m.fonteRelogio = 0xFF;  // fora da faixa: não pode vazar para os bits reservados
  Registro r = montar(m, pacoteExemplo());
  TEST_ASSERT_EQUAL_HEX8(0, r.flags & 0xF8);
  TEST_ASSERT_EQUAL_UINT8(0, r.reservado);
}

// ---------------------------------------------------------------- Leitura
void test_ler_ida_e_volta() {
  Registro r{};
  TEST_ASSERT_EQUAL(Erro::NENHUM, ler(REFERENCIA, sizeof(REFERENCIA), r));
  TEST_ASSERT_EQUAL_UINT16(8, r.boot_coord);
  TEST_ASSERT_EQUAL_UINT32(1791297000, r.utc_s);
  TEST_ASSERT_EQUAL_UINT32(3600, r.desde_boot_s);
  TEST_ASSERT_EQUAL_INT8(-22, r.rssi);
  TEST_ASSERT_EQUAL_INT8(-96, r.ruido);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(protocolo::Classe::NOVO), r.classe);
  TEST_ASSERT_EQUAL_UINT16(2, r.perdidos);
  protocolo::PacoteLeitura p = r.pacote;  // cópia: nunca ponteiro para campo packed
  TEST_ASSERT_EQUAL_UINT32(3, p.seq);
  TEST_ASSERT_EQUAL_UINT16(1066, p.solo_mv);
  TEST_ASSERT_EQUAL_UINT16(133, p.acordado_ant_ms);
}

// Qualquer bit invertido é detectado (CRC-16 detecta todo erro de 1 bit).
void test_ler_detecta_cada_bit_invertido() {
  for (size_t i = 0; i < sizeof(REFERENCIA); i++) {
    for (int b = 0; b < 8; b++) {
      uint8_t copia[48];
      memcpy(copia, REFERENCIA, sizeof(copia));
      copia[i] ^= static_cast<uint8_t>(1u << b);
      Registro r{};
      Erro e = ler(copia, sizeof(copia), r);
      // No byte 0 a versão muda; nos demais, o CRC falha.
      TEST_ASSERT_EQUAL(i == 0 ? Erro::VERSAO_DESCONHECIDA : Erro::CRC, e);
    }
  }
}

void test_ler_rejeita_tamanho_e_versao() {
  Registro r{};
  TEST_ASSERT_EQUAL(Erro::TAMANHO_CURTO, ler(nullptr, 48, r));
  TEST_ASSERT_EQUAL(Erro::TAMANHO_CURTO, ler(REFERENCIA, 0, r));
  TEST_ASSERT_EQUAL(Erro::TAMANHO_ERRADO, ler(REFERENCIA, 47, r));

  uint8_t maior[49] = {};
  memcpy(maior, REFERENCIA, 48);
  TEST_ASSERT_EQUAL(Erro::TAMANHO_ERRADO, ler(maior, sizeof(maior), r));

  uint8_t v2[48];
  memcpy(v2, REFERENCIA, 48);
  v2[0] = 2;
  TEST_ASSERT_EQUAL(Erro::VERSAO_DESCONHECIDA, ler(v2, 48, r));

  uint8_t apagado[48];  // flash apagada (0xFF): não é registro
  memset(apagado, 0xFF, sizeof(apagado));
  TEST_ASSERT_EQUAL(Erro::VERSAO_DESCONHECIDA, ler(apagado, 48, r));
}

// Bits reservados em flags (versão futura compatível) não impedem a leitura.
void test_ler_ignora_bits_reservados() {
  Registro r = montar(metadadosExemplo(), pacoteExemplo());
  r.flags |= 0x80;
  r.crc = crc16(bytesDe(r), offsetof(Registro, crc));
  Registro lido{};
  TEST_ASSERT_EQUAL(Erro::NENHUM, ler(bytesDe(r), sizeof(r), lido));
  TEST_ASSERT_TRUE(horaValida(lido));
  TEST_ASSERT_EQUAL_UINT8(1, fonteRelogio(lido));
}

void test_nome_erro() {
  TEST_ASSERT_EQUAL_STRING("nenhum", nomeErro(Erro::NENHUM));
  TEST_ASSERT_EQUAL_STRING("crc", nomeErro(Erro::CRC));
  TEST_ASSERT_EQUAL_STRING("versao_desconhecida", nomeErro(Erro::VERSAO_DESCONHECIDA));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc_valor_de_verificacao);
  RUN_TEST(test_crc_vazio_e_valor_inicial);
  RUN_TEST(test_montar_igual_a_referencia_python);
  RUN_TEST(test_montar_hora_invalida_zera_utc);
  RUN_TEST(test_montar_hora_fora_do_uint32);
  RUN_TEST(test_montar_satura);
  RUN_TEST(test_montar_fonte_e_bits_reservados);
  RUN_TEST(test_ler_ida_e_volta);
  RUN_TEST(test_ler_detecta_cada_bit_invertido);
  RUN_TEST(test_ler_rejeita_tamanho_e_versao);
  RUN_TEST(test_ler_ignora_bits_reservados);
  RUN_TEST(test_nome_erro);
  return UNITY_END();
}
