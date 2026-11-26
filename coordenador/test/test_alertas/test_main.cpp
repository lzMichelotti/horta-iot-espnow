// Testes unitários das regras e alertas (coordenador/lib/alertas), no PC:
//   cd coordenador && pio test -e native -f test_alertas
#include <unity.h>

#include <initializer_list>

#include <alertas.h>

using namespace alertas;
using protocolo::Estado;
using protocolo::Grandeza;
using protocolo::ModoBateria;

void setUp() {}
void tearDown() {}

namespace {

const uint8_t MAC1[6] = {0x88, 0x57, 0x21, 0x70, 0xa6, 0xec};
const uint8_t MAC2[6] = {0x02, 0, 0, 0, 0, 0x02};

// Limiares só para os testes (os de produção ficam em coordenador/src/config.h).
Limiares lim() {
  Limiares l{};
  l.soloSecoDecimos = 200;        // 20 %
  l.soloMuitoUmidoDecimos = 900;  // 90 %
  l.soloEncharcadoDecimos = 1050; // 105 %
  l.soloBandaDecimos = 50;        // 5 %
  l.temperaturaAltaC100 = 3500;
  l.temperaturaBaixaC100 = 500;
  l.temperaturaBandaC100 = 100;
  l.leiturasParaAtivar = 2;
  l.leiturasEncharcado = 3;
  l.errosParaAtivar = 2;
  l.erroAlimentacao = false;
  return l;
}

uint32_t relogioTeste = 0;  // desde_boot_s da próxima leitura

// Leitura com tudo ok (solo em 50 %, 25 °C, modo normal); cada chamada avança 10 s.
Leitura ok(int32_t soloDecimos = 500) {
  Leitura l{};
  relogioTeste += 10;
  l.ref.desdeBoot_s = relogioTeste;
  l.ref.bootCoord = 1;
  l.ref.definida = true;
  l.estados = 0;
  l.temperaturaC100 = 2500;
  l.alimentacaoMv = 3800;
  l.temPct = true;
  l.soloDecimos = soloDecimos;
  l.modo = ModoBateria::NORMAL;
  return l;
}

Leitura comEstado(Leitura l, Grandeza g, Estado e) {
  l.estados = protocolo::gravarEstado(l.estados, g, e);
  return l;
}

int ativacoes = 0, resolucoes = 0;
void contar(const No&, Tipo, Mudanca m, void*) { (m == Mudanca::ATIVOU ? ativacoes : resolucoes)++; }

}  // namespace

// ---------------------------------------------------------------- Classificação do solo
void test_classificacao_do_solo() {
  Limiares l = lim();
  TEST_ASSERT_EQUAL(ClasseSolo::DESCONHECIDA, classificarSolo(false, 500, l));
  TEST_ASSERT_EQUAL(ClasseSolo::SECO, classificarSolo(true, -1031, l));  // ar (22/11)
  TEST_ASSERT_EQUAL(ClasseSolo::SECO, classificarSolo(true, 199, l));
  TEST_ASSERT_EQUAL(ClasseSolo::UMIDO, classificarSolo(true, 200, l));
  TEST_ASSERT_EQUAL(ClasseSolo::UMIDO, classificarSolo(true, 900, l));
  TEST_ASSERT_EQUAL(ClasseSolo::MUITO_UMIDO, classificarSolo(true, 901, l));
  TEST_ASSERT_EQUAL(ClasseSolo::MUITO_UMIDO, classificarSolo(true, 1095, l));  // logo após a rega
  TEST_ASSERT_EQUAL_STRING("muito_umido", nomeClasseSolo(ClasseSolo::MUITO_UMIDO));
  TEST_ASSERT_EQUAL_STRING("seco", nomeClasseSolo(ClasseSolo::SECO));
  TEST_ASSERT_NULL(nomeClasseSolo(ClasseSolo::DESCONHECIDA));
}

// ---------------------------------------------------------------- Solo seco
void test_solo_seco_ativa_depois_de_n_leituras_com_o_inicio_da_primeira() {
  Tabela t;
  No& n = *t.buscar(MAC1, true);
  ativacoes = resolucoes = 0;
  t.avaliar(n, ok(500), lim(), contar);
  Leitura primeira = ok(150);
  t.avaliar(n, primeira, lim(), contar);
  TEST_ASSERT_FALSE(n.estado[0].ativo);  // 1 leitura só: ainda não
  t.avaliar(n, ok(120), lim(), contar);
  const auto& s = n.estado[static_cast<int>(Tipo::SOLO_SECO)];
  TEST_ASSERT_TRUE(s.ativo);
  TEST_ASSERT_EQUAL_UINT32(primeira.ref.desdeBoot_s, s.inicio.desdeBoot_s);  // começou na 1ª abaixo
  TEST_ASSERT_EQUAL_INT32(120, s.valor);
  TEST_ASSERT_EQUAL(1, ativacoes);
  TEST_ASSERT_EQUAL(ClasseSolo::SECO, n.classe);
}

void test_solo_seco_uma_leitura_isolada_nao_ativa() {
  Tabela t;
  No& n = *t.buscar(MAC1, true);
  t.avaliar(n, ok(150), lim());
  t.avaliar(n, ok(500), lim());  // a sequência quebra
  t.avaliar(n, ok(150), lim());
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::SOLO_SECO)].ativo);
}

void test_solo_seco_nao_oscila_dentro_da_banda() {
  Tabela t;
  No& n = *t.buscar(MAC1, true);
  ativacoes = resolucoes = 0;
  t.avaliar(n, ok(100), lim(), contar);
  t.avaliar(n, ok(100), lim(), contar);  // ativo
  // Entre o limiar (200) e o limiar + banda (250): continua ativo.
  for (int32_t v : {210, 190, 240, 205, 249}) t.avaliar(n, ok(v), lim(), contar);
  const auto& s = n.estado[static_cast<int>(Tipo::SOLO_SECO)];
  TEST_ASSERT_TRUE(s.ativo);
  TEST_ASSERT_EQUAL(1, ativacoes);
  TEST_ASSERT_EQUAL(0, resolucoes);
  Leitura volta = ok(250);  // limiar + banda: resolve
  t.avaliar(n, volta, lim(), contar);
  TEST_ASSERT_FALSE(s.ativo);
  TEST_ASSERT_TRUE(s.resolvido);
  TEST_ASSERT_EQUAL_UINT32(volta.ref.desdeBoot_s, s.fim.desdeBoot_s);
  TEST_ASSERT_EQUAL(1, resolucoes);
}

void test_solo_sem_calibracao_ou_em_erro_nao_mexe_no_alerta() {
  Tabela t;
  No& n = *t.buscar(MAC1, true);
  t.avaliar(n, ok(100), lim());
  t.avaliar(n, ok(100), lim());
  Leitura semCal = ok(900);
  semCal.temPct = false;
  t.avaliar(n, semCal, lim());
  t.avaliar(n, comEstado(ok(900), Grandeza::SOLO, Estado::ERRO), lim());
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::SOLO_SECO)].ativo);
  TEST_ASSERT_EQUAL(ClasseSolo::DESCONHECIDA, n.classe);
}

// ---------------------------------------------------------------- Encharcado
void test_encharcado_precisa_de_mais_leituras() {
  Tabela t;
  No& n = *t.buscar(MAC1, true);
  t.avaliar(n, ok(1100), lim());
  t.avaliar(n, ok(1100), lim());
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::SOLO_ENCHARCADO)].ativo);  // logo após a rega: ainda não
  t.avaliar(n, ok(1100), lim());
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::SOLO_ENCHARCADO)].ativo);
  t.avaliar(n, ok(1010), lim());  // 101 %: dentro da banda (> 100 %)
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::SOLO_ENCHARCADO)].ativo);
  t.avaliar(n, ok(1000), lim());  // 105 − 5 = 100 %: resolve
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::SOLO_ENCHARCADO)].ativo);
}

// ---------------------------------------------------------------- Temperatura
void test_temperatura_alta_e_baixa_com_banda() {
  Tabela t;
  No& n = *t.buscar(MAC1, true);
  auto temp = [](int16_t c100) {
    Leitura l = ok();
    l.temperaturaC100 = c100;
    return l;
  };
  t.avaliar(n, temp(3600), lim());
  t.avaliar(n, temp(3550), lim());
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::TEMPERATURA_ALTA)].ativo);
  t.avaliar(n, temp(3450), lim());  // 34,5 °C: acima de 35 − 1: continua
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::TEMPERATURA_ALTA)].ativo);
  t.avaliar(n, temp(3400), lim());
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::TEMPERATURA_ALTA)].ativo);
  t.avaliar(n, temp(400), lim());
  t.avaliar(n, temp(300), lim());
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::TEMPERATURA_BAIXA)].ativo);
  // Temperatura em erro: não resolve nem ativa (o erro tem o seu próprio alerta).
  t.avaliar(n, comEstado(temp(2500), Grandeza::TEMPERATURA, Estado::ERRO), lim());
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::TEMPERATURA_BAIXA)].ativo);
}

// ---------------------------------------------------------------- Bateria
void test_bateria_segue_o_modo_do_no() {
  Tabela t;
  No& n = *t.buscar(MAC1, true);
  auto modo = [](ModoBateria m, uint16_t mv) {
    Leitura l = ok();
    l.modo = m;
    l.alimentacaoMv = mv;
    return l;
  };
  t.avaliar(n, modo(ModoBateria::ECONOMIA, 3450), lim());  // 1 leitura basta (a histerese é do nó)
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::BATERIA_BAIXA)].ativo);
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::BATERIA_CRITICA)].ativo);
  t.avaliar(n, modo(ModoBateria::CRITICO, 3250), lim());  // último aviso antes de silenciar
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::BATERIA_BAIXA)].ativo);
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::BATERIA_CRITICA)].ativo);
  TEST_ASSERT_EQUAL_INT32(3250, n.estado[static_cast<int>(Tipo::BATERIA_CRITICA)].valor);
  TEST_ASSERT_EQUAL(ModoBateria::CRITICO, n.modo);
  t.avaliar(n, modo(ModoBateria::ECONOMIA, 3400), lim());
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::BATERIA_CRITICA)].ativo);
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::BATERIA_BAIXA)].ativo);
  t.avaliar(n, modo(ModoBateria::NORMAL, 3700), lim());
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::BATERIA_BAIXA)].ativo);
}

void test_modo_desconhecido_nao_muda_a_bateria() {
  Tabela t;
  No& n = *t.buscar(MAC1, true);
  Leitura e = ok();
  e.modo = ModoBateria::ECONOMIA;
  t.avaliar(n, e, lim());
  Leitura antiga = ok();  // registro anterior à Etapa 9
  antiga.modo = ModoBateria::DESCONHECIDO;
  t.avaliar(n, antiga, lim());
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::BATERIA_BAIXA)].ativo);
  TEST_ASSERT_EQUAL(ModoBateria::ECONOMIA, n.modo);
}

// ---------------------------------------------------------------- Sem comunicar
void test_sem_comunicar_ativa_por_tempo_e_resolve_com_leitura_atrasada() {
  Tabela t;
  No& n = *t.buscar(MAC1, true);
  t.verificarSilencio(n, 99999, 100);  // nunca recebeu nada: não ativa
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::SEM_COMUNICAR)].ativo);
  Leitura ultima = ok();
  t.avaliar(n, ultima, lim());
  t.verificarSilencio(n, 100, 100);  // no limite: ainda não
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::SEM_COMUNICAR)].ativo);
  t.verificarSilencio(n, 101, 100);
  const auto& s = n.estado[static_cast<int>(Tipo::SEM_COMUNICAR)];
  TEST_ASSERT_TRUE(s.ativo);
  TEST_ASSERT_EQUAL_UINT32(ultima.ref.desdeBoot_s, s.inicio.desdeBoot_s);  // calado desde a última
  t.verificarSilencio(n, 500, 100);
  TEST_ASSERT_EQUAL_INT32(500, s.valor);  // o valor acompanha o tempo calado
  // Volta com uma leitura que esperou na fila (idade > 0): resolve igual.
  Leitura atrasada = ok();
  atrasada.ref.idade_s = 400;
  t.avaliar(n, atrasada, lim());
  TEST_ASSERT_FALSE(s.ativo);
  TEST_ASSERT_TRUE(s.resolvido);
  TEST_ASSERT_EQUAL_UINT32(400, s.fim.idade_s);
}

// O limite segue o modo da última leitura, com o período do próprio nó
// (comum/protocolo): em economia o nó manda a cada período × 4 e não pode
// virar sem_comunicar com o limite do modo normal.
void test_limite_sem_comunicar_segue_o_modo_do_no() {
  using protocolo::periodoMs;
  TEST_ASSERT_EQUAL_UINT32(3 * 15 * 60, limiteSemComunicarS(ModoBateria::NORMAL, 3));       // 45 min
  TEST_ASSERT_EQUAL_UINT32(3 * 15 * 60, limiteSemComunicarS(ModoBateria::DESCONHECIDO, 3)); // firmware antigo
  TEST_ASSERT_EQUAL_UINT32(3 * 3600, limiteSemComunicarS(ModoBateria::ECONOMIA, 3));        // 3 × 1 h
  TEST_ASSERT_EQUAL_UINT32(3 * 3600, limiteSemComunicarS(ModoBateria::CRITICO, 3));
  TEST_ASSERT_EQUAL_UINT32(periodoMs(ModoBateria::ECONOMIA) / 1000 * 3, limiteSemComunicarS(ModoBateria::ECONOMIA, 3));

  // Nó em economia calado por 2 h (menos que 3 × 1 h): sem alerta.
  Tabela t;
  No& n = *t.buscar(MAC1, true);
  Leitura e = ok();
  e.modo = ModoBateria::ECONOMIA;
  t.avaliar(n, e, lim());
  t.verificarSilencio(n, 2 * 3600, limiteSemComunicarS(n.modo, 3));
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::SEM_COMUNICAR)].ativo);
  // Com o limite do modo normal (45 min) teria ativado: era o alarme falso.
  TEST_ASSERT_TRUE(2 * 3600 > limiteSemComunicarS(ModoBateria::NORMAL, 3));
  t.verificarSilencio(n, 3 * 3600 + 1, limiteSemComunicarS(n.modo, 3));
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::SEM_COMUNICAR)].ativo);
}

// Modo crítico: o nó manda o último pacote e silencia de propósito. O silêncio
// vira no_sem_bateria ("desligou por bateria fraca"), não sem_comunicar.
void test_calado_depois_do_critico_vira_no_sem_bateria() {
  Tabela t;
  No& n = *t.buscar(MAC1, true);
  Leitura ultima = ok();
  ultima.modo = ModoBateria::CRITICO;
  ultima.alimentacaoMv = 3250;
  t.avaliar(n, ultima, lim());
  uint32_t limite = limiteSemComunicarS(n.modo, 3);
  t.verificarSilencio(n, limite, limite);
  const auto& sb = n.estado[static_cast<int>(Tipo::SEM_BATERIA)];
  const auto& sc = n.estado[static_cast<int>(Tipo::SEM_COMUNICAR)];
  TEST_ASSERT_FALSE(sb.ativo);  // no limite: ainda não
  t.verificarSilencio(n, limite + 1, limite);
  TEST_ASSERT_TRUE(sb.ativo);
  TEST_ASSERT_FALSE(sc.ativo);
  TEST_ASSERT_EQUAL_UINT32(ultima.ref.desdeBoot_s, sb.inicio.desdeBoot_s);  // calado desde o último pacote
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(limite + 1), sb.valor);
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::BATERIA_CRITICA)].ativo);  // continua junto
  TEST_ASSERT_EQUAL_STRING("no_sem_bateria", nomeTipo(Tipo::SEM_BATERIA));
  TEST_ASSERT_EQUAL(Unidade::SEGUNDOS, unidade(Tipo::SEM_BATERIA));

  // Bateria recarregada: o nó volta a falar (economia) e tudo se resolve sozinho.
  Leitura volta = ok();
  volta.modo = ModoBateria::ECONOMIA;
  t.avaliar(n, volta, lim());
  TEST_ASSERT_FALSE(sb.ativo);
  TEST_ASSERT_TRUE(sb.resolvido);
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::BATERIA_CRITICA)].ativo);
  TEST_ASSERT_FALSE(sc.ativo);
}

// Último pacote em economia (o pacote do crítico se perdeu): causa desconhecida,
// continua sem_comunicar.
void test_calado_fora_do_critico_continua_sem_comunicar() {
  Tabela t;
  No& n = *t.buscar(MAC1, true);
  Leitura e = ok();
  e.modo = ModoBateria::ECONOMIA;
  t.avaliar(n, e, lim());
  t.verificarSilencio(n, 99999, limiteSemComunicarS(n.modo, 3));
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::SEM_COMUNICAR)].ativo);
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::SEM_BATERIA)].ativo);
}

// ---------------------------------------------------------------- Erros de sensor
void test_erro_de_sensor_ativa_com_n_e_resolve_com_um_ok() {
  Tabela t;
  No& n = *t.buscar(MAC1, true);
  t.avaliar(n, comEstado(ok(), Grandeza::UMIDADE_AR, Estado::ERRO), lim());
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::ERRO_AR)].ativo);
  t.avaliar(n, comEstado(ok(), Grandeza::TEMPERATURA, Estado::FORA_DE_FAIXA), lim());
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::ERRO_AR)].ativo);
  t.avaliar(n, ok(), lim());
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::ERRO_AR)].ativo);

  t.avaliar(n, comEstado(ok(), Grandeza::SOLO, Estado::ERRO), lim());
  t.avaliar(n, comEstado(ok(), Grandeza::SOLO, Estado::ERRO), lim());
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::ERRO_SOLO)].ativo);
}

void test_erro_da_alimentacao_so_com_a_regra_ligada() {
  Tabela t;
  No& n = *t.buscar(MAC1, true);
  Leitura semDivisor = comEstado(ok(), Grandeza::ALIMENTACAO, Estado::FORA_DE_FAIXA);
  t.avaliar(n, semDivisor, lim());
  t.avaliar(n, semDivisor, lim());
  TEST_ASSERT_FALSE(n.estado[static_cast<int>(Tipo::ERRO_ALIMENTACAO)].ativo);
  Limiares l = lim();
  l.erroAlimentacao = true;
  t.avaliar(n, semDivisor, l);
  t.avaliar(n, semDivisor, l);
  TEST_ASSERT_TRUE(n.estado[static_cast<int>(Tipo::ERRO_ALIMENTACAO)].ativo);
}

// ---------------------------------------------------------------- Tabela
void test_tabela_separa_os_nos_e_conta_os_ativos() {
  Tabela t;
  No& a = *t.buscar(MAC1, true);
  No& b = *t.buscar(MAC2, true);
  TEST_ASSERT_EQUAL_PTR(&a, t.buscar(MAC1, false));
  const uint8_t outro[6] = {1, 2, 3, 4, 5, 6};
  TEST_ASSERT_NULL(t.buscar(outro, false));
  t.avaliar(a, ok(100), lim());
  t.avaliar(a, ok(100), lim());
  TEST_ASSERT_EQUAL(1, Tabela::ativos(a));
  TEST_ASSERT_EQUAL(0, Tabela::ativos(b));
  TEST_ASSERT_EQUAL(1, t.ativos());
  t.limpar();
  TEST_ASSERT_EQUAL(0, t.ativos());
  TEST_ASSERT_NULL(t.buscar(MAC1, false));
}

void test_tabela_cheia() {
  Tabela t;
  for (uint8_t i = 0; i < MAX_NOS; i++) {
    uint8_t mac[6] = {2, 0, 0, 0, 0, i};
    TEST_ASSERT_NOT_NULL(t.buscar(mac, true));
  }
  uint8_t outro[6] = {2, 0, 0, 0, 1, 0};
  TEST_ASSERT_NULL(t.buscar(outro, true));
}

int runUnityTests() {
  UNITY_BEGIN();
  RUN_TEST(test_classificacao_do_solo);
  RUN_TEST(test_solo_seco_ativa_depois_de_n_leituras_com_o_inicio_da_primeira);
  RUN_TEST(test_solo_seco_uma_leitura_isolada_nao_ativa);
  RUN_TEST(test_solo_seco_nao_oscila_dentro_da_banda);
  RUN_TEST(test_solo_sem_calibracao_ou_em_erro_nao_mexe_no_alerta);
  RUN_TEST(test_encharcado_precisa_de_mais_leituras);
  RUN_TEST(test_temperatura_alta_e_baixa_com_banda);
  RUN_TEST(test_bateria_segue_o_modo_do_no);
  RUN_TEST(test_modo_desconhecido_nao_muda_a_bateria);
  RUN_TEST(test_sem_comunicar_ativa_por_tempo_e_resolve_com_leitura_atrasada);
  RUN_TEST(test_limite_sem_comunicar_segue_o_modo_do_no);
  RUN_TEST(test_calado_depois_do_critico_vira_no_sem_bateria);
  RUN_TEST(test_calado_fora_do_critico_continua_sem_comunicar);
  RUN_TEST(test_erro_de_sensor_ativa_com_n_e_resolve_com_um_ok);
  RUN_TEST(test_erro_da_alimentacao_so_com_a_regra_ligada);
  RUN_TEST(test_tabela_separa_os_nos_e_conta_os_ativos);
  RUN_TEST(test_tabela_cheia);
  return UNITY_END();
}

int main() { return runUnityTests(); }
