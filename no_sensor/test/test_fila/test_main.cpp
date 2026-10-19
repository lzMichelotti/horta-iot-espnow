// Testes da fila de pendentes do nó (lib/fila), rodados no PC:
//   cd no_sensor && pio test -e native
#include <unity.h>

#include <cstring>

#include <fila.h>

using namespace fila;
using protocolo::PacoteLeitura;

namespace {

constexpr uint16_t CAP = 4;
uint32_t controle;
Entrada itens[CAP];

PacoteLeitura pacote(uint16_t boot, uint32_t seq) {
  PacoteLeitura p{};
  p.versao = protocolo::VERSAO;
  p.tipo = static_cast<uint8_t>(protocolo::Tipo::LEITURA);
  p.boot = boot;
  p.seq = seq;
  p.solo_mv = static_cast<uint16_t>(1000 + seq);
  p.flags = protocolo::flag::AHT20_NOVA_TENTATIVA;
  return p;
}

// Enfileira (boot, seq) lida no instante 100 × seq.
bool por(Fila& f, uint16_t boot, uint32_t seq) { return f.enfileirar(criar(pacote(boot, seq), 100 * seq)); }

}  // namespace

void setUp() {
  controle = 0;
  std::memset(itens, 0xAA, sizeof(itens));  // lixo: a fila não pode depender do conteúdo inicial
}
void tearDown() {}

void test_entrada_tem_26_bytes_e_guarda_o_pacote_sem_a_idade() {
  TEST_ASSERT_EQUAL_UINT(26, sizeof(Entrada));
  PacoteLeitura p = pacote(7, 42);
  p.idade_s = 999;  // não é guardada
  Entrada e = criar(p, 1234);
  TEST_ASSERT_EQUAL_UINT16(7, bootDe(e));
  TEST_ASSERT_EQUAL_UINT32(42, seqDe(e));
  TEST_ASSERT_EQUAL_MEMORY(&p, e.pacote, protocolo::TAMANHO_SEM_IDADE);
}

void test_vazia_no_inicio_e_ordem_de_chegada() {
  Fila f(controle, itens, CAP);
  TEST_ASSERT_TRUE(f.vazia());
  TEST_ASSERT_TRUE(f.coerente());
  por(f, 1, 0);
  por(f, 1, 1);
  por(f, 1, 2);
  TEST_ASSERT_EQUAL_UINT16(3, f.quantidade());
  TEST_ASSERT_EQUAL_UINT32(0, seqDe(f.em(0)));  // a mais antiga primeiro
  TEST_ASSERT_EQUAL_UINT32(2, seqDe(f.em(2)));
}

void test_cheia_descarta_a_mais_antiga() {
  Fila f(controle, itens, CAP);
  for (uint32_t s = 0; s < CAP; s++) TEST_ASSERT_FALSE(por(f, 1, s));
  TEST_ASSERT_TRUE(por(f, 1, 4));  // descartou o 0
  TEST_ASSERT_TRUE(por(f, 1, 5));  // descartou o 1
  TEST_ASSERT_EQUAL_UINT16(CAP, f.quantidade());
  TEST_ASSERT_EQUAL_UINT32(2, seqDe(f.em(0)));
  TEST_ASSERT_EQUAL_UINT32(5, seqDe(f.em(CAP - 1)));
}

void test_liberar_ate_a_confirmacao_cumulativa() {
  Fila f(controle, itens, CAP);
  for (uint32_t s = 10; s < 14; s++) por(f, 2, s);
  TEST_ASSERT_EQUAL_UINT16(0, f.liberarAte(2, 9));   // confirmação antiga: nada muda
  TEST_ASSERT_EQUAL_UINT16(2, f.liberarAte(2, 11));  // 10 e 11
  TEST_ASSERT_EQUAL_UINT32(12, seqDe(f.em(0)));
  TEST_ASSERT_EQUAL_UINT16(0, f.liberarAte(2, 11));  // repetida: nada muda
  TEST_ASSERT_EQUAL_UINT16(2, f.liberarAte(2, 50));  // além do fim: libera tudo o que há
  TEST_ASSERT_TRUE(f.vazia());
}

void test_liberar_atravessa_boots() {
  Fila f(controle, itens, CAP);
  por(f, 3, 98);
  por(f, 3, 99);
  por(f, 4, 0);  // reiniciou com a fila preservada
  TEST_ASSERT_EQUAL_UINT16(2, f.liberarAte(3, 99));
  TEST_ASSERT_EQUAL_UINT16(4, bootDe(f.em(0)));
}

void test_anel_da_a_volta_sem_perder_a_ordem() {
  Fila f(controle, itens, CAP);
  uint32_t proximo = 0;
  for (int rodada = 0; rodada < 10; rodada++) {
    por(f, 1, proximo++);
    por(f, 1, proximo++);
    f.liberarAte(1, proximo - 2);  // fica sempre a última
    TEST_ASSERT_EQUAL_UINT16(1, f.quantidade());
    TEST_ASSERT_EQUAL_UINT32(proximo - 1, seqDe(f.em(0)));
  }
}

void test_estado_persiste_so_no_controle_e_nos_itens() {
  // Simula o deep sleep: um objeto novo sobre a mesma memória vê a mesma fila.
  {
    Fila f(controle, itens, CAP);
    por(f, 1, 7);
    por(f, 1, 8);
  }
  Fila depois(controle, itens, CAP);
  TEST_ASSERT_EQUAL_UINT16(2, depois.quantidade());
  TEST_ASSERT_EQUAL_UINT32(7, seqDe(depois.em(0)));
}

void test_controle_incoerente_e_detectado() {
  controle = (5u << 16) | 0;  // quantidade 5 numa fila de 4
  Fila f(controle, itens, CAP);
  TEST_ASSERT_FALSE(f.coerente());
  f.limpar();
  TEST_ASSERT_TRUE(f.coerente());
  TEST_ASSERT_TRUE(f.vazia());
}

void test_para_envio_calcula_idade_e_marca_sem_anteriores() {
  Entrada e = criar(pacote(1, 5), 1000);
  PacoteLeitura p = paraEnvio(e, 1000 + 21600, true);
  TEST_ASSERT_EQUAL_UINT32(21600, p.idade_s);
  TEST_ASSERT_TRUE(p.flags & protocolo::flag::SEM_ANTERIORES);
  TEST_ASSERT_TRUE(p.flags & protocolo::flag::AHT20_NOVA_TENTATIVA);  // as outras flags ficam
  TEST_ASSERT_EQUAL_UINT16(1005, p.solo_mv);

  p = paraEnvio(e, 1000, false);  // enviada no mesmo ciclo
  TEST_ASSERT_EQUAL_UINT32(0, p.idade_s);
  TEST_ASSERT_FALSE(p.flags & protocolo::flag::SEM_ANTERIORES);

  p = paraEnvio(e, 10, false);  // relógio "voltou" (não deveria): idade 0, não um número enorme
  TEST_ASSERT_EQUAL_UINT32(0, p.idade_s);
}

int runUnityTests() {
  UNITY_BEGIN();
  RUN_TEST(test_entrada_tem_26_bytes_e_guarda_o_pacote_sem_a_idade);
  RUN_TEST(test_vazia_no_inicio_e_ordem_de_chegada);
  RUN_TEST(test_cheia_descarta_a_mais_antiga);
  RUN_TEST(test_liberar_ate_a_confirmacao_cumulativa);
  RUN_TEST(test_liberar_atravessa_boots);
  RUN_TEST(test_anel_da_a_volta_sem_perder_a_ordem);
  RUN_TEST(test_estado_persiste_so_no_controle_e_nos_itens);
  RUN_TEST(test_controle_incoerente_e_detectado);
  RUN_TEST(test_para_envio_calcula_idade_e_marca_sem_anteriores);
  return UNITY_END();
}

int main() { return runUnityTests(); }
