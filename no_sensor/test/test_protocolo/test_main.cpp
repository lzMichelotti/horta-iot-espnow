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
  TEST_ASSERT_EQUAL_UINT(26, sizeof(PacoteLeitura));
  TEST_ASSERT_EQUAL_UINT(42, sizeof(PacoteAssinado));
  TEST_ASSERT_EQUAL_UINT(9, sizeof(Confirmacao));
  TEST_ASSERT_EQUAL_UINT(25, sizeof(ConfirmacaoAssinada));
}

void test_bytes_little_endian_nos_offsets_da_especificacao() {
  uint8_t b[sizeof(PacoteLeitura)];
  PacoteLeitura p = montarLeitura(dadosValidos());
  p.idade_s = 0x01020304;  // escrito pelo nó no envio
  paraBytes(p, b);
  const uint8_t esperado[26] = {
      0x03, 0x01,              // versao = 3, tipo
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
      0x04, 0x03, 0x02, 0x01,  // idade_s = 0x01020304 (v3)
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
  b[0] = 4;  // versão futura, com outro tamanho
  b[1] = static_cast<uint8_t>(Tipo::LEITURA);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::VERSAO_DESCONHECIDA, validarComChave(b, sizeof(b), p));
  b[0] = 0;
  TEST_ASSERT_EQUAL(Rejeicao::VERSAO_DESCONHECIDA, validarComChave(b, sizeof(b), p));
}

void test_pacotes_v1_e_v2_sao_recusados() {
  // Firmwares antigos: v1 (22 B, sem assinatura) e v2 (38 B, sem idade_s).
  PacoteLeitura antigo = montarLeitura(dadosValidos());
  uint8_t b[sizeof(PacoteAssinado)];
  paraBytes(antigo, b);
  PacoteLeitura p{};
  b[0] = 1;
  TEST_ASSERT_EQUAL(Rejeicao::VERSAO_DESCONHECIDA, validarComChave(b, 22, p));
  b[0] = 2;
  TEST_ASSERT_EQUAL(Rejeicao::VERSAO_DESCONHECIDA, validarComChave(b, 38, p));
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
  TEST_ASSERT_EQUAL_HEX8_ARRAY(esperado, b + sizeof(PacoteLeitura), TAMANHO_ASSINATURA);
  TEST_ASSERT_EQUAL_MEMORY(&p, b, sizeof(p));  // os 26 B da leitura vão intactos
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
  // Bytes 0 e 1 (versão e tipo) têm rejeição própria; do 2 ao 41, só a assinatura pega.
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

// ------------------------------------------------------------------ Idade (v3)
void test_idade_vai_assinada_no_fim_do_pacote() {
  PacoteLeitura original = montarLeitura(dadosValidos());
  TEST_ASSERT_EQUAL_UINT32(0, original.idade_s);  // montarLeitura: enviada no ciclo em que foi lida
  original.idade_s = 21600;                        // reenviada 6 h depois
  uint8_t b[sizeof(PacoteAssinado)];
  assinadoBytes(original, b);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::NENHUMA, validarComChave(b, sizeof(b), p));
  TEST_ASSERT_EQUAL_UINT32(21600, p.idade_s);
  b[TAMANHO_SEM_IDADE] ^= 0x01;  // idade adulterada no ar
  TEST_ASSERT_EQUAL(Rejeicao::ASSINATURA_INVALIDA, validarComChave(b, sizeof(b), p));
}

void test_flag_sem_anteriores_e_o_bit_3() {
  DadosLeitura d = dadosValidos();
  d.flags = flag::SEM_ANTERIORES;
  uint8_t b[sizeof(PacoteAssinado)];
  assinadoBytes(montarLeitura(d), b);
  TEST_ASSERT_EQUAL_HEX8(0x08, b[21]);
}

// ------------------------------------------------------------------ Confirmação (v3)
namespace {

Confirmacao confirmacao(uint16_t boot, uint32_t seq, uint8_t flags = 0) {
  Confirmacao c{};
  c.versao = VERSAO;
  c.tipo = static_cast<uint8_t>(Tipo::CONFIRMACAO);
  c.boot = boot;
  c.seq = seq;
  c.flags = flags;
  return c;
}

void confirmacaoBytes(const Confirmacao& c, uint8_t* b) {
  ConfirmacaoAssinada a{};
  TEST_ASSERT_TRUE(assinarConfirmacao(c, CHAVE, hmacDeTeste, a));
  std::memcpy(b, &a, sizeof(a));
}

// O nó já gerou até (boot 7, seq 50).
RejeicaoConfirmacao validarConf(const uint8_t* b, size_t n, Confirmacao& c) {
  return validarConfirmacao(b, n, CHAVE, hmacDeTeste, 7, 50, c);
}

}  // namespace

void test_confirmacao_ida_e_volta() {
  uint8_t b[sizeof(ConfirmacaoAssinada)];
  confirmacaoBytes(confirmacao(7, 49), b);
  Confirmacao c{};
  TEST_ASSERT_EQUAL(RejeicaoConfirmacao::NENHUMA, validarConf(b, sizeof(b), c));
  TEST_ASSERT_EQUAL_UINT16(7, c.boot);
  TEST_ASSERT_EQUAL_UINT32(49, c.seq);
  TEST_ASSERT_EQUAL_UINT8(0, c.flags);
}

void test_confirmacao_forjada_com_chave_errada() {
  ConfirmacaoAssinada a{};
  uint8_t outra[TAMANHO_CHAVE] = {0};
  TEST_ASSERT_TRUE(assinarConfirmacao(confirmacao(7, 49), outra, hmacDeTeste, a));
  uint8_t b[sizeof(a)];
  std::memcpy(b, &a, sizeof(b));
  Confirmacao c{};
  TEST_ASSERT_EQUAL(RejeicaoConfirmacao::ASSINATURA_INVALIDA, validarConf(b, sizeof(b), c));
}

void test_confirmacao_qualquer_bit_alterado() {
  uint8_t original[sizeof(ConfirmacaoAssinada)];
  confirmacaoBytes(confirmacao(7, 49), original);
  for (size_t i = 2; i < sizeof(original); i++)
    for (uint8_t bit = 0; bit < 8; bit++) {
      uint8_t b[sizeof(original)];
      std::memcpy(b, original, sizeof(b));
      b[i] ^= static_cast<uint8_t>(1u << bit);
      Confirmacao c{};
      TEST_ASSERT_EQUAL_MESSAGE(RejeicaoConfirmacao::ASSINATURA_INVALIDA, validarConf(b, sizeof(b), c),
                                "byte alterado");
    }
}

void test_reflexao_nos_dois_sentidos_e_recusada() {
  // Leitura autêntica devolvida ao nó como se fosse confirmação.
  uint8_t leitura[sizeof(PacoteAssinado)];
  assinadoBytes(montarLeitura(dadosValidos()), leitura);
  Confirmacao c{};
  TEST_ASSERT_EQUAL(RejeicaoConfirmacao::TAMANHO_ERRADO, validarConf(leitura, sizeof(leitura), c));
  // Mesmo com o tamanho certo, o tipo (assinado) é o de leitura.
  Confirmacao comTipoLeitura = confirmacao(7, 49);
  comTipoLeitura.tipo = static_cast<uint8_t>(Tipo::LEITURA);
  uint8_t b[sizeof(ConfirmacaoAssinada)];
  confirmacaoBytes(comTipoLeitura, b);
  TEST_ASSERT_EQUAL(RejeicaoConfirmacao::TIPO_DESCONHECIDO, validarConf(b, sizeof(b), c));
  // Confirmação autêntica enviada ao coordenador como se fosse leitura.
  confirmacaoBytes(confirmacao(7, 49), b);
  PacoteLeitura p{};
  TEST_ASSERT_EQUAL(Rejeicao::TIPO_DESCONHECIDO, validarComChave(b, sizeof(b), p));
}

void test_confirmacao_versao_e_tamanho() {
  uint8_t b[sizeof(ConfirmacaoAssinada) + 1];
  confirmacaoBytes(confirmacao(7, 49), b);
  Confirmacao c{};
  TEST_ASSERT_EQUAL(RejeicaoConfirmacao::TAMANHO_ERRADO, validarConf(b, sizeof(ConfirmacaoAssinada) - 1, c));
  TEST_ASSERT_EQUAL(RejeicaoConfirmacao::TAMANHO_ERRADO, validarConf(b, sizeof(ConfirmacaoAssinada) + 1, c));
  TEST_ASSERT_EQUAL(RejeicaoConfirmacao::TAMANHO_ERRADO, validarConf(nullptr, sizeof(ConfirmacaoAssinada), c));
  b[0] = 2;
  TEST_ASSERT_EQUAL(RejeicaoConfirmacao::VERSAO_DESCONHECIDA, validarConf(b, sizeof(ConfirmacaoAssinada), c));
}

void test_confirmacao_alem_do_gerado_e_recusada() {
  // Ninguém confirma o que o nó ainda não enviou (ex.: confirmação antiga
  // repetida depois de a NVS do nó ser apagada e o boot recomeçar).
  Confirmacao c{};
  uint8_t b[sizeof(ConfirmacaoAssinada)];
  confirmacaoBytes(confirmacao(7, 50), b);  // exatamente o último gerado: vale
  TEST_ASSERT_EQUAL(RejeicaoConfirmacao::NENHUMA, validarConf(b, sizeof(b), c));
  confirmacaoBytes(confirmacao(7, 51), b);
  TEST_ASSERT_EQUAL(RejeicaoConfirmacao::ALEM_DO_GERADO, validarConf(b, sizeof(b), c));
  confirmacaoBytes(confirmacao(8, 0), b);  // boot que o nó ainda não teve
  TEST_ASSERT_EQUAL(RejeicaoConfirmacao::ALEM_DO_GERADO, validarConf(b, sizeof(b), c));
  confirmacaoBytes(confirmacao(6, 900), b);  // boot anterior: atrás do gerado, vale
  TEST_ASSERT_EQUAL(RejeicaoConfirmacao::NENHUMA, validarConf(b, sizeof(b), c));
}

void test_nada_gravado_nao_confere_o_par() {
  uint8_t b[sizeof(ConfirmacaoAssinada)];
  confirmacaoBytes(confirmacao(0, 0, flag_conf::NADA_GRAVADO), b);
  Confirmacao c{};
  TEST_ASSERT_EQUAL(RejeicaoConfirmacao::NENHUMA, validarConf(b, sizeof(b), c));
  TEST_ASSERT_TRUE(c.flags & flag_conf::NADA_GRAVADO);
}

void test_comparar_par_modular() {
  TEST_ASSERT_EQUAL_INT(0, compararPar(3, 10, 3, 10));
  TEST_ASSERT_TRUE(compararPar(3, 9, 3, 10) < 0);
  TEST_ASSERT_TRUE(compararPar(3, 11, 3, 10) > 0);
  TEST_ASSERT_TRUE(compararPar(4, 0, 3, 99999) > 0);         // boot novo vem depois
  TEST_ASSERT_TRUE(compararPar(3, 0xFFFFFFFFu, 3, 0) < 0);   // seq dá a volta
  TEST_ASSERT_TRUE(compararPar(0, 0, 65535, 5) > 0);         // boot dá a volta
}

// ------------------------------------------------------------------ Sequência (v3)
namespace {

// Classifica e, se a classe for aceita, confirma como se a gravação tivesse dado certo.
Classe registrar(RastreadorSequencia& r, uint16_t boot, uint32_t seq, bool semAnteriores, uint32_t* lacuna = nullptr) {
  uint32_t l = 0;
  Classe c = r.classificar(boot, seq, semAnteriores, l);
  r.confirmar(boot, seq, c, l);
  if (lacuna != nullptr) *lacuna = l;
  return c;
}

}  // namespace

void test_sequencia_normal_so_aceita_o_proximo() {
  RastreadorSequencia r;
  TEST_ASSERT_EQUAL(Classe::PRIMEIRO, registrar(r, 1, 0, true));
  TEST_ASSERT_EQUAL(Classe::NOVO, registrar(r, 1, 1, false));
  TEST_ASSERT_EQUAL(Classe::FORA_DE_ORDEM, registrar(r, 1, 5, false));  // o nó ainda tem 2, 3 e 4
  TEST_ASSERT_EQUAL_UINT32(1, r.bordaSeq());
  TEST_ASSERT_EQUAL(Classe::NOVO, registrar(r, 1, 2, false));
  TEST_ASSERT_EQUAL_UINT32(3, r.aceitos());
  TEST_ASSERT_EQUAL_UINT32(0, r.perdidos());
  TEST_ASSERT_EQUAL_UINT32(1, r.foraDeOrdem());
}

void test_buraco_declarado_pelo_no_e_aceito_e_contado() {
  RastreadorSequencia r;
  registrar(r, 1, 0, true);
  uint32_t lacuna = 0;
  TEST_ASSERT_EQUAL(Classe::NOVO, registrar(r, 1, 5, true, &lacuna));  // 1–4 descartados na fila cheia
  TEST_ASSERT_EQUAL_UINT32(4, lacuna);
  TEST_ASSERT_EQUAL_UINT32(4, r.perdidos());
}

void test_sem_borda_so_aceita_a_mais_antiga_do_no() {
  RastreadorSequencia r;  // histórico formatado ou nó esquecido
  TEST_ASSERT_EQUAL(Classe::FORA_DE_ORDEM, registrar(r, 3, 101, false));  // o nó tem leituras anteriores
  TEST_ASSERT_FALSE(r.temBorda());
  TEST_ASSERT_EQUAL(Classe::PRIMEIRO, registrar(r, 3, 100, true));
  TEST_ASSERT_EQUAL(Classe::NOVO, registrar(r, 3, 101, false));
  TEST_ASSERT_EQUAL_UINT32(0, r.perdidos());
}

void test_duplicado_e_antigo_nao_sao_gravados() {
  RastreadorSequencia r;
  registrar(r, 1, 10, true);
  TEST_ASSERT_EQUAL(Classe::DUPLICADO, registrar(r, 1, 10, false));  // ACK MAC perdido: o nó repetiu
  TEST_ASSERT_EQUAL(Classe::ANTIGO, registrar(r, 1, 9, true));
  TEST_ASSERT_FALSE(aceitar(Classe::DUPLICADO));
  TEST_ASSERT_FALSE(aceitar(Classe::ANTIGO));
  TEST_ASSERT_FALSE(aceitar(Classe::FORA_DE_ORDEM));
  TEST_ASSERT_EQUAL_UINT32(2, r.descartados());
  TEST_ASSERT_EQUAL_UINT32(1, r.aceitos());
  TEST_ASSERT_EQUAL(Classe::NOVO, registrar(r, 1, 11, false));  // borda não foi alterada
}

void test_gravacao_que_falhou_nao_move_a_borda() {
  // Recebido, mas o close() falhou: o reenvio do mesmo pacote tem que ser aceito.
  RastreadorSequencia r;
  registrar(r, 1, 10, true);
  uint32_t l = 0;
  TEST_ASSERT_EQUAL(Classe::NOVO, r.classificar(1, 11, false, l));  // sem confirmar()
  TEST_ASSERT_EQUAL_UINT32(10, r.bordaSeq());
  TEST_ASSERT_EQUAL(Classe::FORA_DE_ORDEM, r.classificar(1, 12, false, l));  // 11 não está gravado
  TEST_ASSERT_EQUAL(Classe::NOVO, registrar(r, 1, 11, false));               // reenvio aceito
  TEST_ASSERT_EQUAL(Classe::NOVO, registrar(r, 1, 12, false));
}

void test_confirmar_classe_recusada_nao_faz_nada() {
  RastreadorSequencia r;
  registrar(r, 1, 10, true);
  r.confirmar(1, 50, Classe::FORA_DE_ORDEM, 0);
  r.confirmar(1, 50, Classe::DUPLICADO, 0);
  TEST_ASSERT_EQUAL_UINT32(10, r.bordaSeq());
  TEST_ASSERT_EQUAL_UINT32(1, r.aceitos());
}

void test_reinicio_do_no_exige_sem_anteriores() {
  RastreadorSequencia r;
  registrar(r, 1, 500, true);
  // Fila do nó ainda com leituras do boot 1: o boot 2 espera.
  TEST_ASSERT_EQUAL(Classe::FORA_DE_ORDEM, registrar(r, 2, 0, false));
  TEST_ASSERT_EQUAL(Classe::REINICIO, registrar(r, 2, 0, true));
  TEST_ASSERT_EQUAL_UINT32(1, r.reinicios());
  TEST_ASSERT_EQUAL_UINT32(0, r.perdidos());
  uint32_t lacuna = 0;
  TEST_ASSERT_EQUAL(Classe::REINICIO, registrar(r, 3, 2, true, &lacuna));  // seq 0 e 1 do boot 3 perdidos
  TEST_ASSERT_EQUAL_UINT32(2, lacuna);
}

void test_boot_que_da_a_volta_e_reinicio() {
  RastreadorSequencia r;
  registrar(r, 65535, 7, true);
  TEST_ASSERT_EQUAL(Classe::REINICIO, registrar(r, 0, 0, true));  // contador de boots deu a volta
}

void test_boot_antigo_e_descartado() {
  // Repetição de um pacote (com assinatura válida) de um boot anterior.
  RastreadorSequencia r;
  registrar(r, 5, 10, true);
  TEST_ASSERT_EQUAL(Classe::BOOT_ANTIGO, registrar(r, 4, 99, true));
  TEST_ASSERT_FALSE(aceitar(Classe::BOOT_ANTIGO));
  TEST_ASSERT_EQUAL_UINT32(1, r.bootsAntigos());
  TEST_ASSERT_EQUAL_UINT32(1, r.descartados());
  TEST_ASSERT_EQUAL(Classe::NOVO, registrar(r, 5, 11, false));  // borda não foi alterada
}

void test_restaurar_retoma_a_borda_do_historico() {
  // Coordenador religou e leu (boot 5, seq 10) do histórico; o 11 tinha sido
  // recebido mas não gravado (reiniciou antes do close()).
  RastreadorSequencia r;
  r.restaurar(5, 10);
  TEST_ASSERT_EQUAL(Classe::DUPLICADO, registrar(r, 5, 10, false));
  TEST_ASSERT_EQUAL(Classe::ANTIGO, registrar(r, 5, 3, false));
  TEST_ASSERT_EQUAL(Classe::BOOT_ANTIGO, registrar(r, 4, 0, true));
  TEST_ASSERT_EQUAL(Classe::FORA_DE_ORDEM, registrar(r, 5, 12, false));  // a nova chega primeiro
  TEST_ASSERT_EQUAL(Classe::NOVO, registrar(r, 5, 11, true));            // o nó reenvia o 11
  TEST_ASSERT_EQUAL(Classe::NOVO, registrar(r, 5, 12, false));
  TEST_ASSERT_EQUAL_UINT32(0, r.perdidos());
}

void test_esquecer_libera_no_com_nvs_apagada() {
  RastreadorSequencia r;
  registrar(r, 9, 0, true);
  TEST_ASSERT_EQUAL(Classe::BOOT_ANTIGO, registrar(r, 1, 0, true));  // NVS do nó apagada
  r.esquecer();
  TEST_ASSERT_EQUAL(Classe::PRIMEIRO, registrar(r, 1, 0, true));
  TEST_ASSERT_EQUAL(Classe::NOVO, registrar(r, 1, 1, false));
}

void test_estouro_do_numero_de_sequencia() {
  RastreadorSequencia r;
  registrar(r, 1, 0xFFFFFFFEu, true);
  TEST_ASSERT_EQUAL(Classe::NOVO, registrar(r, 1, 0xFFFFFFFFu, false));
  TEST_ASSERT_EQUAL(Classe::NOVO, registrar(r, 1, 0, false));  // 0xFFFFFFFF + 1 = 0
  TEST_ASSERT_EQUAL(Classe::FORA_DE_ORDEM, registrar(r, 1, 3, false));
  uint32_t lacuna = 0;
  TEST_ASSERT_EQUAL(Classe::NOVO, registrar(r, 1, 3, true, &lacuna));
  TEST_ASSERT_EQUAL_UINT32(2, lacuna);
  TEST_ASSERT_EQUAL(Classe::ANTIGO, registrar(r, 1, 0xFFFFFFFFu, false));  // ficou para trás
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
  RUN_TEST(test_pacotes_v1_e_v2_sao_recusados);
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
  RUN_TEST(test_idade_vai_assinada_no_fim_do_pacote);
  RUN_TEST(test_flag_sem_anteriores_e_o_bit_3);
  RUN_TEST(test_confirmacao_ida_e_volta);
  RUN_TEST(test_confirmacao_forjada_com_chave_errada);
  RUN_TEST(test_confirmacao_qualquer_bit_alterado);
  RUN_TEST(test_reflexao_nos_dois_sentidos_e_recusada);
  RUN_TEST(test_confirmacao_versao_e_tamanho);
  RUN_TEST(test_confirmacao_alem_do_gerado_e_recusada);
  RUN_TEST(test_nada_gravado_nao_confere_o_par);
  RUN_TEST(test_comparar_par_modular);
  RUN_TEST(test_sequencia_normal_so_aceita_o_proximo);
  RUN_TEST(test_buraco_declarado_pelo_no_e_aceito_e_contado);
  RUN_TEST(test_sem_borda_so_aceita_a_mais_antiga_do_no);
  RUN_TEST(test_duplicado_e_antigo_nao_sao_gravados);
  RUN_TEST(test_gravacao_que_falhou_nao_move_a_borda);
  RUN_TEST(test_confirmar_classe_recusada_nao_faz_nada);
  RUN_TEST(test_reinicio_do_no_exige_sem_anteriores);
  RUN_TEST(test_boot_que_da_a_volta_e_reinicio);
  RUN_TEST(test_boot_antigo_e_descartado);
  RUN_TEST(test_restaurar_retoma_a_borda_do_historico);
  RUN_TEST(test_esquecer_libera_no_com_nvs_apagada);
  RUN_TEST(test_estouro_do_numero_de_sequencia);
  return UNITY_END();
}

int main() { return runUnityTests(); }
