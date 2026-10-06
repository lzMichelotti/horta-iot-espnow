// Testes unitários do protocolo (comum/protocolo), rodados no PC:
//   cd no_sensor && pio test -e native
// Unity no PlatformIO: https://docs.platformio.org/en/latest/advanced/unit-testing/frameworks/unity.html
#include <unity.h>

#include <cmath>
#include <cstring>

#include <protocolo.h>

using namespace protocolo;

namespace {

DadosLeitura dadosValidos() {
  DadosLeitura d{};
  d.boot = 7;
  d.seq = 42;
  d.temperaturaC = 23.66f;
  d.estadoTemperatura = Estado::OK;
  d.umidadeArPct = 80.62f;
  d.estadoUmidadeAr = Estado::OK;
  d.soloMv = 997.4f;
  d.estadoSolo = Estado::OK;
  d.alimentacaoMv = 3380.0f;
  d.estadoAlimentacao = Estado::OK;
  d.motivoBoot = 1;
  d.acordadoAntMs = 1234;
  d.tentativasAnt = 2;
  d.flags = flag::AHT20_NOVA_TENTATIVA;
  return d;
}

void paraBytes(const PacoteLeitura& p, uint8_t* b) { std::memcpy(b, &p, sizeof(p)); }

}  // namespace

void setUp() {}
void tearDown() {}

// ------------------------------------------------------------------ Layout
void test_tamanho_do_pacote() { TEST_ASSERT_EQUAL_UINT(22, sizeof(PacoteLeitura)); }

void test_bytes_little_endian_nos_offsets_da_especificacao() {
  uint8_t b[sizeof(PacoteLeitura)];
  paraBytes(montarLeitura(dadosValidos()), b);
  const uint8_t esperado[22] = {
      0x01, 0x01,              // versao, tipo
      0x07, 0x00,              // boot = 7
      0x2A, 0x00, 0x00, 0x00,  // seq = 42
      0x3E, 0x09,              // temperatura = 2366 (23,66 °C)
      0x7E, 0x1F,              // umidade = 8062 (80,62 %)
      0xE5, 0x03,              // solo = 997 mV (997,4 arredondado)
      0x34, 0x0D,              // alimentacao = 3380 mV
      0x00,                    // estados: tudo OK
      0x01,                    // motivo_boot = POWERON
      0xD2, 0x04,              // acordado_ant_ms = 1234
      0x02,                    // tentativas_ant = 2
      0x01,                    // flags: AHT20_NOVA_TENTATIVA
  };
  TEST_ASSERT_EQUAL_HEX8_ARRAY(esperado, b, sizeof(esperado));
}

// ------------------------------------------------------------------ Montagem
void test_estados_ficam_nos_bits_da_grandeza() {
  uint8_t e = 0;
  e = gravarEstado(e, Grandeza::TEMPERATURA, Estado::FORA_DE_FAIXA);
  e = gravarEstado(e, Grandeza::ALIMENTACAO, Estado::ERRO);
  TEST_ASSERT_EQUAL_HEX8(0x42, e);  // 01 00 00 10
  TEST_ASSERT_EQUAL(Estado::FORA_DE_FAIXA, lerEstado(e, Grandeza::TEMPERATURA));
  TEST_ASSERT_EQUAL(Estado::OK, lerEstado(e, Grandeza::UMIDADE_AR));
  TEST_ASSERT_EQUAL(Estado::OK, lerEstado(e, Grandeza::SOLO));
  TEST_ASSERT_EQUAL(Estado::ERRO, lerEstado(e, Grandeza::ALIMENTACAO));
}

void test_temperatura_nos_limites_da_faixa() {
  DadosLeitura d = dadosValidos();
  d.temperaturaC = -40.0f;
  PacoteLeitura p = montarLeitura(d);
  TEST_ASSERT_EQUAL_INT16(-4000, p.temperatura_c100);
  TEST_ASSERT_EQUAL(Estado::OK, lerEstado(p.estados, Grandeza::TEMPERATURA));

  d.temperaturaC = 85.0f;
  p = montarLeitura(d);
  TEST_ASSERT_EQUAL_INT16(8500, p.temperatura_c100);
  TEST_ASSERT_EQUAL(Estado::OK, lerEstado(p.estados, Grandeza::TEMPERATURA));
}

void test_temperatura_um_passo_alem_da_faixa_vira_fora() {
  DadosLeitura d = dadosValidos();
  d.temperaturaC = 85.01f;
  PacoteLeitura p = montarLeitura(d);
  TEST_ASSERT_EQUAL_INT16(8501, p.temperatura_c100);
  TEST_ASSERT_EQUAL(Estado::FORA_DE_FAIXA, lerEstado(p.estados, Grandeza::TEMPERATURA));

  d.temperaturaC = -40.01f;
  p = montarLeitura(d);
  TEST_ASSERT_EQUAL_INT16(-4001, p.temperatura_c100);
  TEST_ASSERT_EQUAL(Estado::FORA_DE_FAIXA, lerEstado(p.estados, Grandeza::TEMPERATURA));
}

void test_valor_absurdo_satura_no_limite_do_tipo() {
  DadosLeitura d = dadosValidos();
  d.temperaturaC = 400.0f;  // 40000 não cabe em int16
  d.alimentacaoMv = 1e9f;
  PacoteLeitura p = montarLeitura(d);
  TEST_ASSERT_EQUAL_INT16(INT16_MAX, p.temperatura_c100);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, p.alimentacao_mv);
  TEST_ASSERT_EQUAL(Estado::FORA_DE_FAIXA, lerEstado(p.estados, Grandeza::TEMPERATURA));
  TEST_ASSERT_EQUAL(Estado::FORA_DE_FAIXA, lerEstado(p.estados, Grandeza::ALIMENTACAO));
}

void test_umidade_negativa_satura_em_zero_mas_fica_fora() {
  DadosLeitura d = dadosValidos();
  d.umidadeArPct = -1.0f;
  PacoteLeitura p = montarLeitura(d);
  TEST_ASSERT_EQUAL_UINT16(0, p.umidade_ar_c100);
  TEST_ASSERT_EQUAL(Estado::FORA_DE_FAIXA, lerEstado(p.estados, Grandeza::UMIDADE_AR));
}

void test_erro_nan_e_reservado_viram_erro_com_valor_zero() {
  DadosLeitura d = dadosValidos();
  d.estadoTemperatura = Estado::ERRO;   // valor presente, mas estado de erro
  d.umidadeArPct = NAN;                 // estado OK, mas sem valor
  d.estadoSolo = Estado::RESERVADO;     // estado inválido na entrada
  d.alimentacaoMv = INFINITY;           // infinito não é erro: satura e fica fora
  PacoteLeitura p = montarLeitura(d);
  TEST_ASSERT_EQUAL_INT16(0, p.temperatura_c100);
  TEST_ASSERT_EQUAL_UINT16(0, p.umidade_ar_c100);
  TEST_ASSERT_EQUAL_UINT16(0, p.solo_mv);
  TEST_ASSERT_EQUAL(Estado::ERRO, lerEstado(p.estados, Grandeza::TEMPERATURA));
  TEST_ASSERT_EQUAL(Estado::ERRO, lerEstado(p.estados, Grandeza::UMIDADE_AR));
  TEST_ASSERT_EQUAL(Estado::ERRO, lerEstado(p.estados, Grandeza::SOLO));
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, p.alimentacao_mv);
  TEST_ASSERT_EQUAL(Estado::FORA_DE_FAIXA, lerEstado(p.estados, Grandeza::ALIMENTACAO));
}

// ------------------------------------------------------------------ Validação
void test_ida_e_volta_montar_validar() {
  uint8_t b[sizeof(PacoteLeitura)];
  PacoteLeitura original = montarLeitura(dadosValidos());
  paraBytes(original, b);
  PacoteLeitura recebido{};
  TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validar(b, sizeof(b), recebido));
  TEST_ASSERT_EQUAL_MEMORY(&original, &recebido, sizeof(original));
}

void test_validar_em_endereco_impar() {
  // O rádio pode entregar o buffer em qualquer endereço; validar() copia com memcpy.
  uint8_t buffer[sizeof(PacoteLeitura) + 1];
  paraBytes(montarLeitura(dadosValidos()), buffer + 1);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validar(buffer + 1, sizeof(PacoteLeitura), p));
  TEST_ASSERT_EQUAL_UINT32(42, p.seq);
}

void test_tamanho_curto() {
  uint8_t b[1] = {VERSAO};
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::TAMANHO_CURTO, validar(b, 0, p));
  TEST_ASSERT_EQUAL(Rejeicao::TAMANHO_CURTO, validar(b, 1, p));
  TEST_ASSERT_EQUAL(Rejeicao::TAMANHO_CURTO, validar(nullptr, 22, p));
}

void test_versao_desconhecida_mesmo_com_outro_tamanho() {
  uint8_t b[30] = {0};
  b[0] = 2;  // versão futura, com outro tamanho
  b[1] = static_cast<uint8_t>(Tipo::LEITURA);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::VERSAO_DESCONHECIDA, validar(b, sizeof(b), p));
  b[0] = 0;
  TEST_ASSERT_EQUAL(Rejeicao::VERSAO_DESCONHECIDA, validar(b, sizeof(b), p));
}

void test_tipo_desconhecido() {
  uint8_t b[sizeof(PacoteLeitura)];
  paraBytes(montarLeitura(dadosValidos()), b);
  b[1] = 9;
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::TIPO_DESCONHECIDO, validar(b, sizeof(b), p));
}

void test_tamanho_errado_com_versao_conhecida() {
  uint8_t b[sizeof(PacoteLeitura) + 1];
  paraBytes(montarLeitura(dadosValidos()), b);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::TAMANHO_ERRADO, validar(b, sizeof(PacoteLeitura) - 1, p));
  TEST_ASSERT_EQUAL(Rejeicao::TAMANHO_ERRADO, validar(b, sizeof(PacoteLeitura) + 1, p));
}

void test_estado_reservado_rejeitado() {
  PacoteLeitura original = montarLeitura(dadosValidos());
  original.estados = gravarEstado(original.estados, Grandeza::SOLO, Estado::RESERVADO);
  uint8_t b[sizeof(PacoteLeitura)];
  paraBytes(original, b);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::ESTADO_RESERVADO, validar(b, sizeof(b), p));
}

void test_faixas_com_estado_ok_sao_rejeitadas() {
  struct Caso {
    void (*alterar)(PacoteLeitura&);
    Rejeicao esperada;
  };
  const Caso casos[] = {
      {[](PacoteLeitura& p) { p.temperatura_c100 = 8501; }, Rejeicao::TEMPERATURA_FORA},
      {[](PacoteLeitura& p) { p.temperatura_c100 = -4001; }, Rejeicao::TEMPERATURA_FORA},
      {[](PacoteLeitura& p) { p.umidade_ar_c100 = 10001; }, Rejeicao::UMIDADE_AR_FORA},
      {[](PacoteLeitura& p) { p.solo_mv = SOLO_MAX_MV + 1; }, Rejeicao::SOLO_FORA},
      {[](PacoteLeitura& p) { p.alimentacao_mv = ALIMENTACAO_MAX_MV + 1; }, Rejeicao::ALIMENTACAO_FORA},
  };
  for (const Caso& c : casos) {
    PacoteLeitura original = montarLeitura(dadosValidos());
    c.alterar(original);
    uint8_t b[sizeof(PacoteLeitura)];
    paraBytes(original, b);
    PacoteLeitura p{};
    TEST_ASSERT_EQUAL(c.esperada, validar(b, sizeof(b), p));
  }
}

void test_valor_fora_aceito_quando_estado_diz_fora_ou_erro() {
  PacoteLeitura original = montarLeitura(dadosValidos());
  original.temperatura_c100 = 9000;
  original.estados = gravarEstado(original.estados, Grandeza::TEMPERATURA, Estado::FORA_DE_FAIXA);
  original.solo_mv = 60000;  // lixo, mas o estado diz ERRO: o valor é ignorado
  original.estados = gravarEstado(original.estados, Grandeza::SOLO, Estado::ERRO);
  uint8_t b[sizeof(PacoteLeitura)];
  paraBytes(original, b);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validar(b, sizeof(b), p));
}

void test_flags_reservadas_e_motivo_desconhecido_sao_aceitos() {
  PacoteLeitura original = montarLeitura(dadosValidos());
  original.flags = 0xFF;        // bits reservados ligados (compatibilidade futura)
  original.motivo_boot = 200;   // motivo que o ESP-IDF atual não define
  uint8_t b[sizeof(PacoteLeitura)];
  paraBytes(original, b);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validar(b, sizeof(b), p));
}

void test_flag_anterior_sem_ack_chega_ao_coordenador() {
  DadosLeitura d = dadosValidos();
  d.tentativasAnt = 3;
  d.flags = flag::ANTERIOR_SEM_ACK;
  uint8_t b[sizeof(PacoteLeitura)];
  paraBytes(montarLeitura(d), b);
  TEST_ASSERT_EQUAL_HEX8(0x02, b[21]);  // bit 1 do byte de flags
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validar(b, sizeof(b), p));
  TEST_ASSERT_EQUAL_UINT8(3, p.tentativas_ant);
  TEST_ASSERT_TRUE(p.flags & flag::ANTERIOR_SEM_ACK);
  TEST_ASSERT_FALSE(p.flags & flag::AHT20_NOVA_TENTATIVA);
}

void test_flag_ciclo_anterior_abortado_chega_ao_coordenador() {
  DadosLeitura d = dadosValidos();
  d.flags = flag::CICLO_ANTERIOR_ABORTADO;
  uint8_t b[sizeof(PacoteLeitura)];
  paraBytes(montarLeitura(d), b);
  TEST_ASSERT_EQUAL_HEX8(0x04, b[21]);  // bit 2 do byte de flags
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validar(b, sizeof(b), p));
  TEST_ASSERT_TRUE(p.flags & flag::CICLO_ANTERIOR_ABORTADO);
}

// ------------------------------------------------------------------ Sequência
void test_sequencia_normal_e_lacuna() {
  RastreadorSequencia r;
  TEST_ASSERT_EQUAL(Classe::PRIMEIRO, r.registrar(1, 0));
  TEST_ASSERT_EQUAL(Classe::NOVO, r.registrar(1, 1));
  TEST_ASSERT_EQUAL(Classe::NOVO, r.registrar(1, 5));  // 2, 3 e 4 perdidos
  TEST_ASSERT_EQUAL_UINT32(3, r.ultimaLacuna());
  TEST_ASSERT_EQUAL_UINT32(3, r.perdidos());
  TEST_ASSERT_EQUAL_UINT32(3, r.aceitos());
}

void test_primeiro_pacote_nao_conta_perdas() {
  RastreadorSequencia r;  // coordenador ligou com o nó já no seq 100
  TEST_ASSERT_EQUAL(Classe::PRIMEIRO, r.registrar(3, 100));
  TEST_ASSERT_EQUAL_UINT32(0, r.perdidos());
}

void test_duplicado_e_antigo_sao_descartados() {
  RastreadorSequencia r;
  r.registrar(1, 10);
  TEST_ASSERT_EQUAL(Classe::DUPLICADO, r.registrar(1, 10));
  TEST_ASSERT_EQUAL(Classe::ANTIGO, r.registrar(1, 9));
  TEST_ASSERT_FALSE(aceitar(Classe::DUPLICADO));
  TEST_ASSERT_FALSE(aceitar(Classe::ANTIGO));
  TEST_ASSERT_EQUAL_UINT32(2, r.descartados());
  TEST_ASSERT_EQUAL_UINT32(1, r.aceitos());
  TEST_ASSERT_EQUAL(Classe::NOVO, r.registrar(1, 11));  // estado não foi alterado
  TEST_ASSERT_EQUAL_UINT32(0, r.perdidos());
}

void test_reinicio_nao_e_confundido_com_duplicata() {
  RastreadorSequencia r;
  r.registrar(1, 500);
  TEST_ASSERT_EQUAL(Classe::REINICIO, r.registrar(2, 0));  // seq voltou a 0, mas o boot mudou
  TEST_ASSERT_EQUAL_UINT32(1, r.reinicios());
  TEST_ASSERT_EQUAL_UINT32(0, r.perdidos());
  TEST_ASSERT_EQUAL(Classe::REINICIO, r.registrar(3, 2));  // novo boot começando no seq 2
  TEST_ASSERT_EQUAL_UINT32(2, r.ultimaLacuna());            // seq 0 e 1 do boot 3 perdidos
}

void test_boot_menor_tambem_e_reinicio() {
  RastreadorSequencia r;
  r.registrar(65535, 7);
  TEST_ASSERT_EQUAL(Classe::REINICIO, r.registrar(0, 0));  // contador de boots deu a volta
  TEST_ASSERT_EQUAL(Classe::REINICIO, r.registrar(0 + 5, 0));
  r.registrar(9, 0);
  TEST_ASSERT_EQUAL(Classe::REINICIO, r.registrar(1, 0));  // NVS apagada: boot recomeçou
}

void test_estouro_do_numero_de_sequencia() {
  RastreadorSequencia r;
  r.registrar(1, 0xFFFFFFFEu);
  TEST_ASSERT_EQUAL(Classe::NOVO, r.registrar(1, 0xFFFFFFFFu));
  TEST_ASSERT_EQUAL(Classe::NOVO, r.registrar(1, 0));  // 0xFFFFFFFF + 1 = 0
  TEST_ASSERT_EQUAL_UINT32(0, r.perdidos());
  TEST_ASSERT_EQUAL(Classe::NOVO, r.registrar(1, 3));
  TEST_ASSERT_EQUAL_UINT32(2, r.perdidos());
  TEST_ASSERT_EQUAL(Classe::ANTIGO, r.registrar(1, 0xFFFFFFFFu));  // ficou para trás
}

void test_estouro_com_lacuna() {
  RastreadorSequencia r;
  r.registrar(1, 0xFFFFFFFFu);
  TEST_ASSERT_EQUAL(Classe::NOVO, r.registrar(1, 1));  // perdeu o 0
  TEST_ASSERT_EQUAL_UINT32(1, r.ultimaLacuna());
}

int runUnityTests() {
  UNITY_BEGIN();
  RUN_TEST(test_tamanho_do_pacote);
  RUN_TEST(test_bytes_little_endian_nos_offsets_da_especificacao);
  RUN_TEST(test_estados_ficam_nos_bits_da_grandeza);
  RUN_TEST(test_temperatura_nos_limites_da_faixa);
  RUN_TEST(test_temperatura_um_passo_alem_da_faixa_vira_fora);
  RUN_TEST(test_valor_absurdo_satura_no_limite_do_tipo);
  RUN_TEST(test_umidade_negativa_satura_em_zero_mas_fica_fora);
  RUN_TEST(test_erro_nan_e_reservado_viram_erro_com_valor_zero);
  RUN_TEST(test_ida_e_volta_montar_validar);
  RUN_TEST(test_validar_em_endereco_impar);
  RUN_TEST(test_tamanho_curto);
  RUN_TEST(test_versao_desconhecida_mesmo_com_outro_tamanho);
  RUN_TEST(test_tipo_desconhecido);
  RUN_TEST(test_tamanho_errado_com_versao_conhecida);
  RUN_TEST(test_estado_reservado_rejeitado);
  RUN_TEST(test_faixas_com_estado_ok_sao_rejeitadas);
  RUN_TEST(test_valor_fora_aceito_quando_estado_diz_fora_ou_erro);
  RUN_TEST(test_flags_reservadas_e_motivo_desconhecido_sao_aceitos);
  RUN_TEST(test_flag_anterior_sem_ack_chega_ao_coordenador);
  RUN_TEST(test_flag_ciclo_anterior_abortado_chega_ao_coordenador);
  RUN_TEST(test_sequencia_normal_e_lacuna);
  RUN_TEST(test_primeiro_pacote_nao_conta_perdas);
  RUN_TEST(test_duplicado_e_antigo_sao_descartados);
  RUN_TEST(test_reinicio_nao_e_confundido_com_duplicata);
  RUN_TEST(test_boot_menor_tambem_e_reinicio);
  RUN_TEST(test_estouro_do_numero_de_sequencia);
  RUN_TEST(test_estouro_com_lacuna);
  return UNITY_END();
}

int main() { return runUnityTests(); }
