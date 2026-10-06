// Testes unitários da lógica de consulta (coordenador/lib/filtro), no PC:
//   cd coordenador && pio test -e native
#include <unity.h>

#include <cstring>
#include <initializer_list>
#include <vector>

#include <filtro.h>

using namespace filtro;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t K = 5;

// Índice com segmentos (número, posições), K = 5. Ids: número × 5 + posição.
anel::Indice indiceCom(std::initializer_list<std::pair<uint32_t, uint32_t>> segs) {
  anel::Indice ind({K, 56});
  for (auto& p : segs) {
    anel::Resumo s;
    s.numero = p.first;
    s.posicoes = s.validos = p.second;
    ind.adicionar(s);
  }
  return ind;
}

std::vector<uint64_t> percorrer(const anel::Indice& ind, uint64_t cursor, bool dec) {
  std::vector<uint64_t> ids;
  for (Percurso p(ind, cursor, dec); p.valido(); p.avancar()) ids.push_back(p.id());
  return ids;
}

// Páginas de `tam` itens, cada uma começando no cursor devolvido pela anterior.
std::vector<uint64_t> paginar(const anel::Indice& ind, bool dec, size_t tam) {
  std::vector<uint64_t> ids;
  uint64_t cursor = 0;
  for (int pagina = 0; pagina < 100; pagina++) {
    Percurso p(ind, cursor, dec);
    size_t n = 0;
    for (; p.valido() && n < tam; p.avancar(), n++) ids.push_back(p.id());
    if (!p.valido()) break;
    cursor = p.id();
  }
  return ids;
}

void assertIds(std::initializer_list<uint64_t> esperado, const std::vector<uint64_t>& obtido) {
  TEST_ASSERT_EQUAL(esperado.size(), obtido.size());
  size_t i = 0;
  for (uint64_t e : esperado) TEST_ASSERT_EQUAL_UINT64(e, obtido[i++]);
}

registro::Registro reg(bool valida, uint32_t utc, uint16_t boot, uint32_t desdeBoot_s, uint8_t ultimoMac = 1) {
  registro::Metadados m{};
  m.horaValida = valida;
  m.utc_s = utc;
  m.bootCoord = boot;
  m.desdeBoot_ms = static_cast<uint64_t>(desdeBoot_s) * 1000;
  m.mac[5] = ultimoMac;
  m.classe = protocolo::Classe::NOVO;
  protocolo::PacoteLeitura p{};
  p.versao = protocolo::VERSAO;
  return registro::montar(m, p);
}

}  // namespace

// ---------------------------------------------------------------- Âncoras e hora efetiva
void test_ancora_do_primeiro_registro_com_hora() {
  Ancoras a;
  a.registrar(reg(false, 0, 7, 10));       // sem hora: o boot 7 passa a precisar de âncora
  a.registrar(reg(true, 5000, 7, 100));    // desvio 4900
  a.registrar(reg(true, 9000, 7, 200));    // mesmo boot: ignorado (vale o primeiro)
  int64_t d = 0;
  TEST_ASSERT_TRUE(a.desvio(7, d));
  TEST_ASSERT_EQUAL_INT64(4900, d);
  TEST_ASSERT_FALSE(a.desvio(8, d));
}

// Boot com todos os registros com hora não ocupa lugar (não precisa reconstruir).
void test_boot_so_com_hora_nao_ocupa() {
  Ancoras a;
  for (uint16_t b = 1; b <= 100; b++) a.registrar(reg(true, 1000u + b, b, 0));
  TEST_ASSERT_EQUAL(0, a.quantidade());
}

// Boot só sem hora: ocupa, mas sem desvio (registros ficam SEM_HORA).
void test_boot_sem_nenhuma_hora() {
  Ancoras a;
  a.registrar(reg(false, 0, 4, 10));
  a.registrar(reg(false, 0, 4, 20));
  TEST_ASSERT_EQUAL(1, a.quantidade());
  int64_t d = 0;
  TEST_ASSERT_FALSE(a.desvio(4, d));
}

void test_ancoras_capacidade() {
  Ancoras a;
  for (uint16_t b = 1; b <= Ancoras::CAPACIDADE + 3; b++) {
    a.registrar(reg(false, 0, b, 0));
    a.registrar(reg(true, 1000u + b, b, 0));
    a.registrar(reg(true, 2000u + b, static_cast<uint16_t>(b + 1000), 0));  // boots só com hora: não contam
  }
  TEST_ASSERT_EQUAL(Ancoras::CAPACIDADE, a.quantidade());
  int64_t d = 0;
  TEST_ASSERT_FALSE(a.desvio(1, d));  // os mais antigos saíram
  TEST_ASSERT_FALSE(a.desvio(3, d));
  TEST_ASSERT_TRUE(a.desvio(4, d));
  TEST_ASSERT_TRUE(a.desvio(Ancoras::CAPACIDADE + 3, d));
}

void test_hora_efetiva() {
  Ancoras a;
  a.registrar(reg(false, 0, 7, 60));            // gravado antes do acerto
  a.registrar(reg(true, 1791300000, 7, 3600));  // desvio = 1791296400
  uint32_t utc = 0;
  TEST_ASSERT_EQUAL(Hora::VALIDA, horaEfetiva(reg(true, 1791300500, 7, 4100), a, utc));
  TEST_ASSERT_EQUAL_UINT32(1791300500, utc);
  // Registro do mesmo boot gravado antes do acerto (desde_boot 60 s).
  TEST_ASSERT_EQUAL(Hora::RECONSTRUIDA, horaEfetiva(reg(false, 0, 7, 60), a, utc));
  TEST_ASSERT_EQUAL_UINT32(1791296460, utc);
  // Boot sem nenhum registro com hora.
  TEST_ASSERT_EQUAL(Hora::SEM_HORA, horaEfetiva(reg(false, 0, 9, 60), a, utc));
  TEST_ASSERT_EQUAL_UINT32(0, utc);
  // Reconstrução negativa (inconsistente): sem hora.
  Ancoras b;
  b.registrar(reg(false, 0, 3, 5));
  b.registrar(reg(true, 10, 3, 100));  // desvio −90
  TEST_ASSERT_EQUAL(Hora::SEM_HORA, horaEfetiva(reg(false, 0, 3, 5), b, utc));
}

// ---------------------------------------------------------------- Filtro
void test_passa() {
  Filtro f;
  registro::Registro r = reg(true, 2000, 1, 0, 1);
  TEST_ASSERT_TRUE(passa(f, r, Hora::VALIDA, 2000));  // sem filtro
  TEST_ASSERT_TRUE(passa(f, r, Hora::SEM_HORA, 0));   // sem filtro de hora: entra

  f.porNo = true;
  f.mac[5] = 1;
  TEST_ASSERT_TRUE(passa(f, r, Hora::VALIDA, 2000));
  f.mac[5] = 2;
  TEST_ASSERT_FALSE(passa(f, r, Hora::VALIDA, 2000));

  f.porNo = false;
  f.porHora = true;
  f.de = 1000;
  f.ate = 2000;
  TEST_ASSERT_TRUE(passa(f, r, Hora::VALIDA, 2000));        // limite inclusive
  TEST_ASSERT_TRUE(passa(f, r, Hora::RECONSTRUIDA, 1000));
  TEST_ASSERT_FALSE(passa(f, r, Hora::VALIDA, 2001));
  TEST_ASSERT_FALSE(passa(f, r, Hora::SEM_HORA, 1500));     // sem hora não entra em período
}

void test_pode_pular() {
  Filtro f;
  anel::Resumo s;
  TEST_ASSERT_TRUE(podePular(f, s));  // vazio
  anel::acumular(s, reg(true, 1000, 1, 0), true);
  anel::acumular(s, reg(true, 2000, 1, 0), true);
  TEST_ASSERT_FALSE(podePular(f, s));  // sem filtro de hora: nunca pula
  f.porHora = true;
  f.de = 2001;
  f.ate = 3000;
  TEST_ASSERT_TRUE(podePular(f, s));
  f.de = 1500;
  TEST_ASSERT_FALSE(podePular(f, s));
  f.de = 2001;
  anel::acumular(s, reg(false, 0, 1, 0), true);  // um sem hora: pode ter hora reconstruída no período
  TEST_ASSERT_FALSE(podePular(f, s));
}

// ---------------------------------------------------------------- Percurso
// Segmentos 3 (2 reg.), 4 (3 reg.) e 6 (1 reg.): o 5 foi apagado (lacuna).
void test_percurso_completo() {
  anel::Indice ind = indiceCom({{3, 2}, {4, 3}, {6, 1}});
  assertIds({15, 16, 20, 21, 22, 30}, percorrer(ind, 0, false));
  assertIds({30, 22, 21, 20, 16, 15}, percorrer(ind, 0, true));
}

void test_percurso_vazio() {
  anel::Indice ind({K, 56});
  TEST_ASSERT_FALSE(Percurso(ind, 0, false).valido());
  TEST_ASSERT_FALSE(Percurso(ind, 0, true).valido());
  anel::Indice soVazio = indiceCom({{1, 0}});
  TEST_ASSERT_FALSE(Percurso(soVazio, 0, false).valido());
}

void test_cursor_crescente() {
  anel::Indice ind = indiceCom({{3, 2}, {4, 3}, {6, 1}});
  assertIds({21, 22, 30}, percorrer(ind, 21, false));
  assertIds({30}, percorrer(ind, 25, false));  // na lacuna: segue para o próximo existente
  assertIds({20, 21, 22, 30}, percorrer(ind, 17, false));  // depois do fim do 3

  Percurso fim(ind, 31, false);  // depois do último: nada, sem rotação
  TEST_ASSERT_FALSE(fim.valido());
  TEST_ASSERT_FALSE(fim.cursorRotacionado());

  Percurso velho(ind, 10, false);  // já apagado: começa do mais antigo
  TEST_ASSERT_TRUE(velho.cursorRotacionado());
  TEST_ASSERT_EQUAL_UINT64(15, velho.id());
}

void test_cursor_decrescente() {
  anel::Indice ind = indiceCom({{3, 2}, {4, 3}, {6, 1}});
  assertIds({21, 20, 16, 15}, percorrer(ind, 21, true));
  assertIds({22, 21, 20, 16, 15}, percorrer(ind, 25, true));  // na lacuna: o anterior existente
  assertIds({30, 22, 21, 20, 16, 15}, percorrer(ind, 100, true));  // depois do fim: do último
  assertIds({16, 15}, percorrer(ind, 19, true));  // posição além do fim do segmento 3

  Percurso velho(ind, 10, true);  // mais antigo que tudo: nada, e avisa
  TEST_ASSERT_FALSE(velho.valido());
  TEST_ASSERT_TRUE(velho.cursorRotacionado());
}

// Paginando de 2 em 2 (e de 4 em 4) sai a mesma sequência do percurso inteiro.
void test_paginacao_igual_ao_percurso() {
  anel::Indice ind = indiceCom({{3, 2}, {4, 3}, {6, 1}, {7, 5}});
  for (bool dec : {false, true})
    for (size_t tam : {1u, 2u, 4u, 100u}) {
      std::vector<uint64_t> a = percorrer(ind, 0, dec), b = paginar(ind, dec, tam);
      TEST_ASSERT_EQUAL(a.size(), b.size());
      for (size_t i = 0; i < a.size(); i++) TEST_ASSERT_EQUAL_UINT64(a[i], b[i]);
    }
}

void test_segmentos_vazios_sao_pulados() {
  anel::Indice ind = indiceCom({{1, 0}, {2, 2}, {3, 0}, {4, 1}, {5, 0}});
  assertIds({10, 11, 20}, percorrer(ind, 0, false));
  assertIds({20, 11, 10}, percorrer(ind, 0, true));
}

void test_pular_segmento() {
  anel::Indice ind = indiceCom({{3, 2}, {4, 3}, {6, 1}});
  Percurso p(ind, 0, false);
  p.pularSegmento();
  TEST_ASSERT_EQUAL_UINT64(20, p.id());
  p.pularSegmento();
  TEST_ASSERT_EQUAL_UINT64(30, p.id());
  p.pularSegmento();
  TEST_ASSERT_FALSE(p.valido());

  Percurso d(ind, 0, true);
  d.pularSegmento();
  TEST_ASSERT_EQUAL_UINT64(22, d.id());  // último do segmento 4
  d.pularSegmento();
  TEST_ASSERT_EQUAL_UINT64(16, d.id());
  d.pularSegmento();
  TEST_ASSERT_FALSE(d.valido());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_ancora_do_primeiro_registro_com_hora);
  RUN_TEST(test_boot_so_com_hora_nao_ocupa);
  RUN_TEST(test_boot_sem_nenhuma_hora);
  RUN_TEST(test_ancoras_capacidade);
  RUN_TEST(test_hora_efetiva);
  RUN_TEST(test_passa);
  RUN_TEST(test_pode_pular);
  RUN_TEST(test_percurso_completo);
  RUN_TEST(test_percurso_vazio);
  RUN_TEST(test_cursor_crescente);
  RUN_TEST(test_cursor_decrescente);
  RUN_TEST(test_paginacao_igual_ao_percurso);
  RUN_TEST(test_segmentos_vazios_sao_pulados);
  RUN_TEST(test_pular_segmento);
  return UNITY_END();
}
