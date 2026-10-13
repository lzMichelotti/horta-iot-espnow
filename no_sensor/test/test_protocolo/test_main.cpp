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

// HMAC substituto para os testes no PC: determinístico e dependente de todos os
// bytes da chave e da mensagem (FNV-1a por byte de saída). Testa o formato e a
// lógica; o HMAC-SHA256 real (mbedTLS) é testado na placa com os vetores da RFC 4231.
bool hmacDeTeste(const uint8_t* chave, size_t nChave, const uint8_t* msg, size_t nMsg, uint8_t saida[TAMANHO_HMAC]) {
  for (size_t i = 0; i < TAMANHO_HMAC; i++) {
    uint32_t h = 2166136261u ^ static_cast<uint32_t>(i);
    for (size_t j = 0; j < nChave; j++) h = (h ^ chave[j]) * 16777619u;
    for (size_t j = 0; j < nMsg; j++) h = (h ^ msg[j]) * 16777619u;
    saida[i] = static_cast<uint8_t>(h >> 24);
  }
  return true;
}

bool hmacQueFalha(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*) { return false; }

const uint8_t CHAVE[TAMANHO_CHAVE] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                                      17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

// Bytes no ar (38 B) de um pacote assinado com CHAVE.
void assinadoBytes(const PacoteLeitura& p, uint8_t* b) {
  PacoteAssinado a{};
  TEST_ASSERT_TRUE(assinar(p, CHAVE, hmacDeTeste, a));
  std::memcpy(b, &a, sizeof(a));
}

Rejeicao validarComChave(const uint8_t* b, size_t n, PacoteLeitura& p) {
  return validar(b, n, CHAVE, hmacDeTeste, p);
}

}  // namespace

void setUp() {}
void tearDown() {}

// ------------------------------------------------------------------ Layout
void test_tamanho_do_pacote() {
  TEST_ASSERT_EQUAL_UINT(22, sizeof(PacoteLeitura));
  TEST_ASSERT_EQUAL_UINT(38, sizeof(PacoteAssinado));
}

void test_bytes_little_endian_nos_offsets_da_especificacao() {
  uint8_t b[sizeof(PacoteLeitura)];
  paraBytes(montarLeitura(dadosValidos()), b);
  const uint8_t esperado[22] = {
      0x02, 0x01,              // versao = 2, tipo
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
void test_ida_e_volta_montar_assinar_validar() {
  uint8_t b[sizeof(PacoteAssinado)];
  PacoteLeitura original = montarLeitura(dadosValidos());
  assinadoBytes(original, b);
  PacoteLeitura recebido{};
  TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validarComChave(b, sizeof(b), recebido));
  TEST_ASSERT_EQUAL_MEMORY(&original, &recebido, sizeof(original));
}

void test_validar_em_endereco_impar() {
  // O rádio pode entregar o buffer em qualquer endereço; validar() copia com memcpy.
  uint8_t buffer[sizeof(PacoteAssinado) + 1];
  assinadoBytes(montarLeitura(dadosValidos()), buffer + 1);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validarComChave(buffer + 1, sizeof(PacoteAssinado), p));
  TEST_ASSERT_EQUAL_UINT32(42, p.seq);
}

void test_tamanho_curto() {
  uint8_t b[1] = {VERSAO};
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::TAMANHO_CURTO, validarComChave(b, 0, p));
  TEST_ASSERT_EQUAL(Rejeicao::TAMANHO_CURTO, validarComChave(b, 1, p));
  TEST_ASSERT_EQUAL(Rejeicao::TAMANHO_CURTO, validarComChave(nullptr, 38, p));
}

void test_versao_desconhecida_mesmo_com_outro_tamanho() {
  uint8_t b[30] = {0};
  b[0] = 3;  // versão futura, com outro tamanho
  b[1] = static_cast<uint8_t>(Tipo::LEITURA);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::VERSAO_DESCONHECIDA, validarComChave(b, sizeof(b), p));
  b[0] = 0;
  TEST_ASSERT_EQUAL(Rejeicao::VERSAO_DESCONHECIDA, validarComChave(b, sizeof(b), p));
}

void test_pacote_v1_sem_assinatura_e_recusado() {
  // Firmware antigo (v1, 22 B): versão desconhecida, nunca aceito sem assinatura.
  PacoteLeitura v1 = montarLeitura(dadosValidos());
  v1.versao = 1;
  uint8_t b[sizeof(PacoteLeitura)];
  paraBytes(v1, b);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::VERSAO_DESCONHECIDA, validarComChave(b, sizeof(b), p));
}

void test_tipo_desconhecido() {
  uint8_t b[sizeof(PacoteAssinado)];
  assinadoBytes(montarLeitura(dadosValidos()), b);
  b[1] = 9;
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::TIPO_DESCONHECIDO, validarComChave(b, sizeof(b), p));
}

void test_tamanho_errado_com_versao_conhecida() {
  uint8_t b[sizeof(PacoteAssinado) + 1];
  assinadoBytes(montarLeitura(dadosValidos()), b);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::TAMANHO_ERRADO, validarComChave(b, sizeof(PacoteAssinado) - 1, p));
  TEST_ASSERT_EQUAL(Rejeicao::TAMANHO_ERRADO, validarComChave(b, sizeof(PacoteAssinado) + 1, p));
  // v2 sem os 16 B de assinatura (o ataque E4 da auditoria: pacote "sem cifra")
  TEST_ASSERT_EQUAL(Rejeicao::TAMANHO_ERRADO, validarComChave(b, sizeof(PacoteLeitura), p));
}

void test_estado_reservado_rejeitado() {
  PacoteLeitura original = montarLeitura(dadosValidos());
  original.estados = gravarEstado(original.estados, Grandeza::SOLO, Estado::RESERVADO);
  uint8_t b[sizeof(PacoteAssinado)];
  assinadoBytes(original, b);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::ESTADO_RESERVADO, validarComChave(b, sizeof(b), p));
}

// ------------------------------------------------------------------ Assinatura
void test_assinatura_sao_os_16_primeiros_bytes_do_hmac() {
  PacoteLeitura p = montarLeitura(dadosValidos());
  uint8_t b[sizeof(PacoteAssinado)];
  assinadoBytes(p, b);
  uint8_t esperado[TAMANHO_HMAC];
  hmacDeTeste(CHAVE, sizeof(CHAVE), b, sizeof(PacoteLeitura), esperado);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(esperado, b + 22, TAMANHO_ASSINATURA);
  TEST_ASSERT_EQUAL_MEMORY(&p, b, sizeof(p));  // os 22 B da leitura vão intactos
}

void test_chave_errada_e_rejeitada() {
  uint8_t b[sizeof(PacoteAssinado)];
  assinadoBytes(montarLeitura(dadosValidos()), b);
  uint8_t outra[TAMANHO_CHAVE];
  std::memcpy(outra, CHAVE, sizeof(outra));
  outra[31] ^= 0x01;
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::ASSINATURA_INVALIDA, validar(b, sizeof(b), outra, hmacDeTeste, p));
}

void test_qualquer_bit_alterado_e_rejeitado() {
  // Bytes 0 e 1 (versão e tipo) têm rejeição própria; do 2 ao 37, só a assinatura pega.
  uint8_t original[sizeof(PacoteAssinado)];
  assinadoBytes(montarLeitura(dadosValidos()), original);
  for (size_t i = 2; i < sizeof(original); i++) {
    for (uint8_t bit = 0; bit < 8; bit++) {
      uint8_t b[sizeof(original)];
      std::memcpy(b, original, sizeof(b));
      b[i] ^= static_cast<uint8_t>(1u << bit);
      PacoteLeitura p{};
      TEST_ASSERT_EQUAL_MESSAGE(Rejeicao::ASSINATURA_INVALIDA, validarComChave(b, sizeof(b), p), "byte alterado");
    }
  }
}

void test_assinatura_conferida_antes_do_conteudo() {
  // Pacote forjado com valor absurdo e assinatura qualquer: não chega a ser interpretado.
  PacoteLeitura forjado = montarLeitura(dadosValidos());
  forjado.temperatura_c100 = 30000;
  uint8_t b[sizeof(PacoteAssinado)] = {0};
  paraBytes(forjado, b);
  PacoteLeitura p{};
  p.seq = 0xABCD;
  TEST_ASSERT_EQUAL(Rejeicao::ASSINATURA_INVALIDA, validarComChave(b, sizeof(b), p));
  TEST_ASSERT_EQUAL_UINT32(0xABCD, p.seq);  // saida não é tocada
}

void test_falha_do_hmac_nao_aceita_nem_assina() {
  uint8_t b[sizeof(PacoteAssinado)];
  assinadoBytes(montarLeitura(dadosValidos()), b);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::ASSINATURA_INVALIDA, validar(b, sizeof(b), CHAVE, hmacQueFalha, p));
  TEST_ASSERT_EQUAL(Rejeicao::ASSINATURA_INVALIDA, validar(b, sizeof(b), CHAVE, nullptr, p));
  TEST_ASSERT_EQUAL(Rejeicao::ASSINATURA_INVALIDA, validar(b, sizeof(b), nullptr, hmacDeTeste, p));
  PacoteAssinado a{};
  TEST_ASSERT_FALSE(assinar(montarLeitura(dadosValidos()), CHAVE, hmacQueFalha, a));
}

void test_chave_derivada_depende_do_mac_e_da_mestra() {
  const uint8_t mac1[TAMANHO_MAC] = {0x88, 0x57, 0x21, 0x70, 0x93, 0x70};
  const uint8_t mac2[TAMANHO_MAC] = {0x88, 0x57, 0x21, 0x70, 0x93, 0x71};
  uint8_t k1[TAMANHO_CHAVE], k1b[TAMANHO_CHAVE], k2[TAMANHO_CHAVE], esperado[TAMANHO_HMAC];
  TEST_ASSERT_TRUE(derivarChaveNo(CHAVE, mac1, hmacDeTeste, k1));
  TEST_ASSERT_TRUE(derivarChaveNo(CHAVE, mac1, hmacDeTeste, k1b));
  TEST_ASSERT_TRUE(derivarChaveNo(CHAVE, mac2, hmacDeTeste, k2));
  hmacDeTeste(CHAVE, sizeof(CHAVE), mac1, sizeof(mac1), esperado);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(esperado, k1, TAMANHO_CHAVE);  // HMAC(mestra, MAC), inteiro
  TEST_ASSERT_EQUAL_HEX8_ARRAY(k1, k1b, TAMANHO_CHAVE);       // determinística
  TEST_ASSERT_FALSE(std::memcmp(k1, k2, TAMANHO_CHAVE) == 0);  // um bit no MAC muda a chave

  // Um nó assina com a sua chave; com a chave de outro nó, não valida.
  PacoteAssinado a{};
  TEST_ASSERT_TRUE(assinar(montarLeitura(dadosValidos()), k1, hmacDeTeste, a));
  uint8_t b[sizeof(a)];
  std::memcpy(b, &a, sizeof(b));
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validar(b, sizeof(b), k1, hmacDeTeste, p));
  TEST_ASSERT_EQUAL(Rejeicao::ASSINATURA_INVALIDA, validar(b, sizeof(b), k2, hmacDeTeste, p));
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
    uint8_t b[sizeof(PacoteAssinado)];
    assinadoBytes(original, b);
    PacoteLeitura p{};
    TEST_ASSERT_EQUAL(c.esperada, validarComChave(b, sizeof(b), p));
  }
}

void test_valor_fora_aceito_quando_estado_diz_fora_ou_erro() {
  PacoteLeitura original = montarLeitura(dadosValidos());
  original.temperatura_c100 = 9000;
  original.estados = gravarEstado(original.estados, Grandeza::TEMPERATURA, Estado::FORA_DE_FAIXA);
  original.solo_mv = 60000;  // lixo, mas o estado diz ERRO: o valor é ignorado
  original.estados = gravarEstado(original.estados, Grandeza::SOLO, Estado::ERRO);
  uint8_t b[sizeof(PacoteAssinado)];
  assinadoBytes(original, b);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validarComChave(b, sizeof(b), p));
}

void test_flags_reservadas_e_motivo_desconhecido_sao_aceitos() {
  PacoteLeitura original = montarLeitura(dadosValidos());
  original.flags = 0xFF;        // bits reservados ligados (compatibilidade futura)
  original.motivo_boot = 200;   // motivo que o ESP-IDF atual não define
  uint8_t b[sizeof(PacoteAssinado)];
  assinadoBytes(original, b);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validarComChave(b, sizeof(b), p));
}

void test_flag_anterior_sem_ack_chega_ao_coordenador() {
  DadosLeitura d = dadosValidos();
  d.tentativasAnt = 3;
  d.flags = flag::ANTERIOR_SEM_ACK;
  uint8_t b[sizeof(PacoteAssinado)];
  assinadoBytes(montarLeitura(d), b);
  TEST_ASSERT_EQUAL_HEX8(0x02, b[21]);  // bit 1 do byte de flags
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validarComChave(b, sizeof(b), p));
  TEST_ASSERT_EQUAL_UINT8(3, p.tentativas_ant);
  TEST_ASSERT_TRUE(p.flags & flag::ANTERIOR_SEM_ACK);
  TEST_ASSERT_FALSE(p.flags & flag::AHT20_NOVA_TENTATIVA);
}

void test_flag_ciclo_anterior_abortado_chega_ao_coordenador() {
  DadosLeitura d = dadosValidos();
  d.flags = flag::CICLO_ANTERIOR_ABORTADO;
  uint8_t b[sizeof(PacoteAssinado)];
  assinadoBytes(montarLeitura(d), b);
  TEST_ASSERT_EQUAL_HEX8(0x04, b[21]);  // bit 2 do byte de flags
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validarComChave(b, sizeof(b), p));
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

void test_boot_que_da_a_volta_e_reinicio() {
  RastreadorSequencia r;
  r.registrar(65535, 7);
  TEST_ASSERT_EQUAL(Classe::REINICIO, r.registrar(0, 0));  // contador de boots deu a volta
  TEST_ASSERT_EQUAL(Classe::REINICIO, r.registrar(0 + 5, 0));
}

void test_boot_antigo_e_descartado() {
  // Repetição de um pacote (com assinatura válida) de um boot anterior.
  RastreadorSequencia r;
  r.registrar(5, 10);
  TEST_ASSERT_EQUAL(Classe::BOOT_ANTIGO, r.registrar(4, 99));
  TEST_ASSERT_FALSE(aceitar(Classe::BOOT_ANTIGO));
  TEST_ASSERT_EQUAL_UINT32(1, r.bootsAntigos());
  TEST_ASSERT_EQUAL_UINT32(1, r.descartados());
  TEST_ASSERT_EQUAL_UINT32(0, r.reinicios());
  TEST_ASSERT_EQUAL(Classe::NOVO, r.registrar(5, 11));  // estado não foi alterado
  TEST_ASSERT_EQUAL_UINT32(0, r.perdidos());
}

void test_restaurar_impede_repeticao_depois_de_religar() {
  // Coordenador religou e recuperou (boot 5, seq 10) do histórico.
  RastreadorSequencia r;
  r.restaurar(5, 10);
  TEST_ASSERT_EQUAL(Classe::DUPLICADO, r.registrar(5, 10));
  TEST_ASSERT_EQUAL(Classe::ANTIGO, r.registrar(5, 3));
  TEST_ASSERT_EQUAL(Classe::BOOT_ANTIGO, r.registrar(4, 0));
  TEST_ASSERT_EQUAL(Classe::NOVO, r.registrar(5, 12));  // o 11 se perdeu com o coordenador fora
  TEST_ASSERT_EQUAL_UINT32(1, r.ultimaLacuna());
  TEST_ASSERT_EQUAL_UINT32(1, r.aceitos());
}

void test_esquecer_libera_no_com_nvs_apagada() {
  RastreadorSequencia r;
  r.registrar(9, 0);
  TEST_ASSERT_EQUAL(Classe::BOOT_ANTIGO, r.registrar(1, 0));  // NVS do nó apagada
  r.esquecer();
  TEST_ASSERT_EQUAL(Classe::PRIMEIRO, r.registrar(1, 0));
  TEST_ASSERT_EQUAL(Classe::NOVO, r.registrar(1, 1));
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
  RUN_TEST(test_ida_e_volta_montar_assinar_validar);
  RUN_TEST(test_validar_em_endereco_impar);
  RUN_TEST(test_tamanho_curto);
  RUN_TEST(test_versao_desconhecida_mesmo_com_outro_tamanho);
  RUN_TEST(test_pacote_v1_sem_assinatura_e_recusado);
  RUN_TEST(test_tipo_desconhecido);
  RUN_TEST(test_tamanho_errado_com_versao_conhecida);
  RUN_TEST(test_estado_reservado_rejeitado);
  RUN_TEST(test_assinatura_sao_os_16_primeiros_bytes_do_hmac);
  RUN_TEST(test_chave_errada_e_rejeitada);
  RUN_TEST(test_qualquer_bit_alterado_e_rejeitado);
  RUN_TEST(test_assinatura_conferida_antes_do_conteudo);
  RUN_TEST(test_falha_do_hmac_nao_aceita_nem_assina);
  RUN_TEST(test_chave_derivada_depende_do_mac_e_da_mestra);
  RUN_TEST(test_faixas_com_estado_ok_sao_rejeitadas);
  RUN_TEST(test_valor_fora_aceito_quando_estado_diz_fora_ou_erro);
  RUN_TEST(test_flags_reservadas_e_motivo_desconhecido_sao_aceitos);
  RUN_TEST(test_flag_anterior_sem_ack_chega_ao_coordenador);
  RUN_TEST(test_flag_ciclo_anterior_abortado_chega_ao_coordenador);
  RUN_TEST(test_sequencia_normal_e_lacuna);
  RUN_TEST(test_primeiro_pacote_nao_conta_perdas);
  RUN_TEST(test_duplicado_e_antigo_sao_descartados);
  RUN_TEST(test_reinicio_nao_e_confundido_com_duplicata);
  RUN_TEST(test_boot_que_da_a_volta_e_reinicio);
  RUN_TEST(test_boot_antigo_e_descartado);
  RUN_TEST(test_restaurar_impede_repeticao_depois_de_religar);
  RUN_TEST(test_esquecer_libera_no_com_nvs_apagada);
  RUN_TEST(test_estouro_do_numero_de_sequencia);
  RUN_TEST(test_estouro_com_lacuna);
  return UNITY_END();
}

int main() { return runUnityTests(); }
