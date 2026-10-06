// Testes unitários da lógica de segmentos em anel (coordenador/lib/anel), no PC:
//   cd coordenador && pio test -e native
#include <unity.h>

#include <cstring>
#include <initializer_list>

#include <anel.h>

using namespace anel;

void setUp() {}
void tearDown() {}

namespace {

registro::Registro registroCom(bool horaValida, uint32_t utc, uint16_t boot) {
  registro::Metadados m{};
  m.horaValida = horaValida;
  m.fonteRelogio = horaValida ? 1 : 0;
  m.bootCoord = boot;
  m.utc_s = utc;
  m.classe = protocolo::Classe::NOVO;
  protocolo::PacoteLeitura p{};
  p.versao = protocolo::VERSAO;
  return registro::montar(m, p);
}

// Executa o plano como o historico.cpp faria, num "sistema de arquivos" fictício
// que só registra quais segmentos existem (os números ficam no índice).
void gravarSimulado(Indice& ind, const registro::Registro& r, uint32_t& apagados) {
  Plano p = ind.planejar();
  for (uint32_t i = 0; i < p.apagar; i++) {
    ind.removerMaisAntigo();
    apagados++;
  }
  if (p.abrirNovo) ind.abrirNovo(p.numeroNovo);
  ind.registrarGravacao(r);
}

}  // namespace

// ---------------------------------------------------------------- Nomes
void test_nome_segmento() {
  char s[TAM_NOME];
  nomeSegmento(123, s);
  TEST_ASSERT_EQUAL_STRING("00000123.seg", s);
  nomeSegmento(0, s);
  TEST_ASSERT_EQUAL_STRING("00000000.seg", s);
  nomeSegmento(99999999, s);
  TEST_ASSERT_EQUAL_STRING("99999999.seg", s);
}

void test_interpretar_nome() {
  uint32_t n = 0;
  TEST_ASSERT_TRUE(interpretarNome("00000123.seg", n));
  TEST_ASSERT_EQUAL_UINT32(123, n);
  const char* invalidos[] = {"", "123.seg", "0000012.seg", "000001234.seg", "00000123.se", "00000123.segx",
                             "0000012a.seg", "00000123.bin", "00000123"};
  for (const char* s : invalidos) TEST_ASSERT_FALSE_MESSAGE(interpretarNome(s, n), s);
  TEST_ASSERT_FALSE(interpretarNome(nullptr, n));
}

void test_nome_ida_e_volta() {
  char s[TAM_NOME];
  for (uint32_t v : {1u, 42u, 500u, 12345678u}) {
    nomeSegmento(v, s);
    uint32_t n = 0;
    TEST_ASSERT_TRUE(interpretarNome(s, n));
    TEST_ASSERT_EQUAL_UINT32(v, n);
  }
}

// ---------------------------------------------------------------- Resumo
void test_acumular() {
  Resumo s;
  acumular(s, registroCom(false, 0, 7), true);          // sem hora
  acumular(s, registroCom(true, 2000, 7), true);
  acumular(s, registroCom(true, 1000, 8), true);        // hora voltou (acerto)
  acumular(s, registroCom(true, 3000, 8), false);       // inválido: só ocupa posição
  TEST_ASSERT_EQUAL_UINT32(4, s.posicoes);
  TEST_ASSERT_EQUAL_UINT32(3, s.validos);
  TEST_ASSERT_EQUAL_UINT32(1, s.semHora);
  TEST_ASSERT_EQUAL_UINT32(2, s.comHora());
  TEST_ASSERT_EQUAL_UINT32(1000, s.utcMin);
  TEST_ASSERT_EQUAL_UINT32(2000, s.utcMax);
  TEST_ASSERT_EQUAL_UINT16(7, s.bootPrimeiro);
  TEST_ASSERT_EQUAL_UINT16(8, s.bootUltimo);
}

void test_cruza() {
  Resumo s;
  acumular(s, registroCom(true, 1000, 1), true);
  acumular(s, registroCom(true, 2000, 1), true);
  TEST_ASSERT_TRUE(cruza(s, 1500, 1600));   // dentro
  TEST_ASSERT_TRUE(cruza(s, 0, 1000));      // toca o início
  TEST_ASSERT_TRUE(cruza(s, 2000, 9000));   // toca o fim
  TEST_ASSERT_TRUE(cruza(s, 0, 9000));      // contém
  TEST_ASSERT_FALSE(cruza(s, 0, 999));
  TEST_ASSERT_FALSE(cruza(s, 2001, 9000));
  TEST_ASSERT_FALSE(cruza(s, 1600, 1500));  // período invertido

  Resumo semHora;
  acumular(semHora, registroCom(false, 0, 1), true);
  TEST_ASSERT_FALSE(cruza(semHora, 0, UINT32_MAX));  // nada com hora para cruzar
}

// ---------------------------------------------------------------- Id global
void test_id_global() {
  TEST_ASSERT_EQUAL_UINT64(0, idGlobal(0, 0, 500));
  TEST_ASSERT_EQUAL_UINT64(61999, idGlobal(123, 499, 500));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(49999999999), idGlobal(99999999, 499, 500));
  uint32_t seg = 0, pos = 0;
  deIdGlobal(61999, 500, seg, pos);
  TEST_ASSERT_EQUAL_UINT32(123, seg);
  TEST_ASSERT_EQUAL_UINT32(499, pos);
  // Ordem dos ids = ordem de gravação.
  TEST_ASSERT_TRUE(idGlobal(5, 499, 500) < idGlobal(6, 0, 500));
}

// ---------------------------------------------------------------- Índice
void test_politica_limitada() {
  Indice a({500, 1000});
  TEST_ASSERT_EQUAL_UINT32(CAPACIDADE, a.politica().maxSegmentos);
  Indice b({0, 0});
  TEST_ASSERT_EQUAL_UINT32(1, b.politica().maxSegmentos);
  TEST_ASSERT_EQUAL_UINT32(1, b.politica().registrosPorSegmento);
}

void test_adicionar_em_ordem() {
  Indice ind({500, 56});
  Resumo s;
  s.numero = 5;
  TEST_ASSERT_TRUE(ind.adicionar(s));
  s.numero = 5;
  TEST_ASSERT_FALSE(ind.adicionar(s));  // repetido
  s.numero = 3;
  TEST_ASSERT_FALSE(ind.adicionar(s));  // fora de ordem
  s.numero = 9;                         // lacuna na numeração é aceita
  TEST_ASSERT_TRUE(ind.adicionar(s));
  TEST_ASSERT_EQUAL(2, ind.quantidade());
  TEST_ASSERT_EQUAL_UINT32(9, ind.atual()->numero);
}

void test_planejar_vazio_abre_o_primeiro() {
  Indice ind({5, 3});
  Plano p = ind.planejar();
  TEST_ASSERT_TRUE(p.abrirNovo);
  TEST_ASSERT_EQUAL_UINT32(1, p.numeroNovo);
  TEST_ASSERT_EQUAL_UINT32(0, p.apagar);
}

// Muitas gravações com K = 5 e no máximo 3 segmentos.
void test_rotacao_simulada() {
  Indice ind({5, 3});
  uint32_t apagados = 0;
  for (uint32_t i = 0; i < 1000; i++) {
    gravarSimulado(ind, registroCom(true, 1000 + i, 1), apagados);
    TEST_ASSERT_TRUE(ind.quantidade() <= 3);
    TEST_ASSERT_TRUE(ind.atual()->posicoes >= 1 && ind.atual()->posicoes <= 5);
    for (size_t k = 1; k < ind.quantidade(); k++)  // numeração estritamente crescente
      TEST_ASSERT_TRUE(ind.em(k).numero == ind.em(k - 1).numero + 1);
  }
  // 1000 registros / 5 = 200 segmentos criados; ficam 3, apagados 197.
  TEST_ASSERT_EQUAL_UINT32(200, ind.atual()->numero);
  TEST_ASSERT_EQUAL_UINT32(198, ind.em(0).numero);
  TEST_ASSERT_EQUAL_UINT32(197, apagados);
  TEST_ASSERT_EQUAL_UINT32(15, ind.totalValidos());
  // Os mais novos sobrevivem: 985..999 → utc 1985..1999.
  TEST_ASSERT_EQUAL_UINT32(1985, ind.em(0).utcMin);
  TEST_ASSERT_EQUAL_UINT32(1999, ind.atual()->utcMax);
}

// Depois de um boot, a tabela é recarregada dos arquivos e a numeração continua.
void test_continua_depois_do_boot() {
  Indice ind({5, 3});
  Resumo s;
  s.numero = 41;
  s.posicoes = s.validos = 5;
  ind.adicionar(s);
  s.numero = 42;
  s.posicoes = s.validos = 2;  // atual, incompleto
  ind.adicionar(s);
  Plano p = ind.planejar();
  TEST_ASSERT_FALSE(p.abrirNovo);  // ainda cabe no 42
  TEST_ASSERT_EQUAL_UINT32(0, p.apagar);

  uint32_t apagados = 0;
  for (int i = 0; i < 3; i++) gravarSimulado(ind, registroCom(true, 1, 1), apagados);  // 42 fica cheio
  p = ind.planejar();
  TEST_ASSERT_TRUE(p.abrirNovo);
  TEST_ASSERT_EQUAL_UINT32(43, p.numeroNovo);
  TEST_ASSERT_EQUAL_UINT32(0, p.apagar);  // 41, 42, 43 = 3: ainda no limite
}

// Segmento com registros inválidos (CRC) também conta posições: o próximo
// registro vai para o byte posicoes × 48, não sobrescreve nada.
void test_posicoes_incluem_invalidos() {
  Indice ind({5, 3});
  Resumo s;
  s.numero = 1;
  s.posicoes = 5;
  s.validos = 3;
  ind.adicionar(s);
  TEST_ASSERT_TRUE(ind.planejar().abrirNovo);
}

// Segmento com sobra de bytes no fim (fechado): o próximo vai para um novo.
void test_segmento_fechado() {
  Indice ind({5, 3});
  Resumo s;
  s.numero = 7;
  s.posicoes = s.validos = 2;
  s.fechado = true;
  ind.adicionar(s);
  Plano p = ind.planejar();
  TEST_ASSERT_TRUE(p.abrirNovo);
  TEST_ASSERT_EQUAL_UINT32(8, p.numeroNovo);

  ind.abrirNovo(8);
  TEST_ASSERT_FALSE(ind.planejar().abrirNovo);
  ind.fecharAtual();  // escrita parcial durante a gravação
  TEST_ASSERT_TRUE(ind.planejar().abrirNovo);
}

// Limite reduzido entre versões do firmware: apaga o excesso, nunca o atual.
void test_limite_reduzido() {
  Indice ind({5, 2});
  Resumo s;
  for (uint32_t n = 1; n <= 5; n++) {
    s.numero = n;
    s.posicoes = s.validos = (n == 5) ? 1 : 5;
    ind.adicionar(s);
  }
  Plano p = ind.planejar();
  TEST_ASSERT_FALSE(p.abrirNovo);
  TEST_ASSERT_EQUAL_UINT32(3, p.apagar);  // fica 4 (anterior) e 5 (atual)
  for (uint32_t i = 0; i < p.apagar; i++) ind.removerMaisAntigo();
  TEST_ASSERT_EQUAL_UINT32(4, ind.em(0).numero);
  TEST_ASSERT_EQUAL_UINT32(5, ind.atual()->numero);
}

void test_limite_de_um_segmento() {
  Indice ind({5, 1});
  uint32_t apagados = 0;
  for (int i = 0; i < 12; i++) gravarSimulado(ind, registroCom(true, 1, 1), apagados);
  TEST_ASSERT_EQUAL(1, ind.quantidade());
  TEST_ASSERT_EQUAL_UINT32(3, ind.atual()->numero);
  TEST_ASSERT_EQUAL_UINT32(2, ind.atual()->posicoes);
  TEST_ASSERT_EQUAL_UINT32(2, apagados);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_nome_segmento);
  RUN_TEST(test_interpretar_nome);
  RUN_TEST(test_nome_ida_e_volta);
  RUN_TEST(test_acumular);
  RUN_TEST(test_cruza);
  RUN_TEST(test_id_global);
  RUN_TEST(test_politica_limitada);
  RUN_TEST(test_adicionar_em_ordem);
  RUN_TEST(test_planejar_vazio_abre_o_primeiro);
  RUN_TEST(test_rotacao_simulada);
  RUN_TEST(test_continua_depois_do_boot);
  RUN_TEST(test_posicoes_incluem_invalidos);
  RUN_TEST(test_segmento_fechado);
  RUN_TEST(test_limite_reduzido);
  RUN_TEST(test_limite_de_um_segmento);
  return UNITY_END();
}
