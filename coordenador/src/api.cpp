#include "api.h"

#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cstring>
#include <esp_timer.h>
#include <uri/UriBraces.h>

#include <apijson.h>
#include <calendario.h>
#include <cadastro.h>
#include <exportacao.h>
#include <json.h>

#include "autorizacao.h"
#include "config.h"
#include "configuracao.h"
#include "consulta.h"
#include "historico.h"
#include "nos.h"
#include "radio.h"
#include "relogio.h"

namespace api {

namespace {

WebServer servidor(80);
Ocioso ocioso = nullptr;

constexpr const char* TIPO_JSON = "application/json; charset=utf-8";
constexpr const char* TIPO_CSV = "text/csv; charset=utf-8";
constexpr size_t MAX_CORPO = 1024;  // corpos de PUT/PATCH/POST (413 acima disso)

// ---------------------------------------------------------------- Saída
// Bloco do tamanho do MSS do TCP (CONFIG_LWIP_TCP_MSS = 1436). Até caber num
// bloco, a resposta sai inteira com Content-Length (qualquer código); passou
// disso (só com 200), vira chunked e cada bloco cheio vai para a rede.
struct Saida {
  char buf[1436];
  size_t n = 0;
  size_t total = 0;  // bytes do corpo enviados
  bool chunked = false;
  const char* tipo = TIPO_JSON;
};
Saida saida;

void despejar() {
  if (saida.n == 0) return;
  if (!saida.chunked) {
    servidor.chunkResponseBegin(saida.tipo);  // 200, Transfer-Encoding: chunked
    saida.chunked = true;
  }
  servidor.chunkWrite(saida.buf, saida.n);
  saida.total += saida.n;
  saida.n = 0;
}

void destino(const char* d, size_t n, void*) {
  while (n > 0) {
    if (saida.n == sizeof(saida.buf)) despejar();
    size_t k = sizeof(saida.buf) - saida.n;
    if (k > n) k = n;
    memcpy(saida.buf + saida.n, d, k);
    saida.n += k;
    d += k;
    n -= k;
  }
}

void comecar(const char* tipo = TIPO_JSON) {
  saida.n = 0;
  saida.total = 0;
  saida.chunked = false;
  saida.tipo = tipo;
}

int codigoFinal = 0;

void terminar(int codigo) {
  if (saida.chunked) {
    despejar();
    servidor.chunkResponseEnd();
    codigoFinal = 200;
  } else {
    servidor.send_P(codigo, saida.tipo, saida.buf, saida.n);
    saida.total = saida.n;
    codigoFinal = codigo;
  }
}

bool clienteConectado() { return servidor.client().connected(); }

// ---------------------------------------------------------------- Erros
void erro(int codigo, const char* tipo, const char* campo, const char* mensagem) {
  comecar();
  json::Escritor e(destino, nullptr);
  e.objeto().chave("erro").objeto().campo("codigo", tipo);
  if (campo != nullptr) e.campo("campo", campo);
  e.campo("mensagem", mensagem).fimObjeto().fimObjeto();
  if (codigo == 401) servidor.sendHeader("WWW-Authenticate", "Bearer realm=\"horta\"");
  terminar(codigo);
}

void erro400(const char* campo, const char* mensagem) { erro(400, "parametro_invalido", campo, mensagem); }

// Rotas de escrita: token + tamanho do corpo. false = já respondeu o erro.
bool exigirToken() {
  if (autorizacao::autorizado(servidor.header("Authorization").c_str())) return true;
  erro(401, "nao_autorizado", nullptr,
       autorizacao::configurado() ? "token ausente ou invalido (Authorization: Bearer <token>)"
                                  : "escrita desabilitada: token nao configurado no coordenador");
  return false;
}

// Corpo JSON da requisição (servidor.arg("plain")) analisado em tokens.
struct Corpo {
  String texto;
  json::Token t[24];
  int n = -1;
};

bool lerCorpo(Corpo& c) {
  c.texto = servidor.arg("plain");
  if (c.texto.length() > MAX_CORPO) {
    erro(413, "corpo_grande", nullptr, "corpo maior que 1024 bytes");
    return false;
  }
  c.n = json::analisar(c.texto.c_str(), c.texto.length(), c.t, sizeof(c.t) / sizeof(c.t[0]));
  if (c.n <= 0 || c.t[0].tipo != json::Tipo::OBJETO) {
    erro(400, "json_invalido", nullptr, "o corpo precisa ser um objeto JSON valido");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------- Pedaços comuns
const char* nomeDoNo(const uint8_t mac[6]) {
  const cadastro::No* c = configuracao::tabela().buscar(mac);
  return c ? c->nome : "desconhecido";
}

void horaAgora(json::Escritor& e) {
  relogio::Instante a = relogio::agora();
  e.objeto().chave("utc");
  if (a.valida) {
    char iso[calendario::TAM_ISO];
    calendario::formatarIso(a.utc_s, iso);
    e.texto(iso);
  } else {
    e.nulo();
  }
  e.campoBool("valida", a.valida).campo("fonte", relogio::nomeFonte(a.fonte)).fimObjeto();
}

void leituraDoRegistro(json::Escritor& e, int64_t id, const registro::Registro& r) {
  uint8_t mac[6];
  memcpy(mac, r.mac, 6);
  uint32_t utc = 0;
  filtro::Hora h = filtro::horaEfetiva(r, consulta::ancoras(), utc);
  int16_t pct = 0;
  bool temPct = configuracao::umidadeSolo(r, pct);
  apijson::leitura(e, id, r, nomeDoNo(mac), h, utc, temPct, pct);
}

// Segundos desde a recepção do registro, pelo relógio interno (vale sem hora
// válida) ou, se for de outro boot, pela hora UTC. false se não dá para saber.
bool haQuanto(const registro::Registro& r, uint32_t& segundos) {
  relogio::Instante a = relogio::agora();
  uint64_t agoraS = a.desdeBoot_ms / 1000;
  if (r.boot_coord == a.boot && agoraS >= r.desde_boot_s) {
    segundos = static_cast<uint32_t>(agoraS - r.desde_boot_s);
    return true;
  }
  if (a.valida && registro::horaValida(r) && a.utc_s >= static_cast<int64_t>(r.utc_s)) {
    segundos = static_cast<uint32_t>(a.utc_s - r.utc_s);
    return true;
  }
  return false;
}

// Um nó (GET /nos, /nos/{mac}, respostas do PATCH e do calibrar).
void noJson(json::Escritor& e, const uint8_t mac[6]) {
  char txtMac[apijson::TAM_MAC];
  apijson::mac(txtMac, mac);
  const cadastro::No* c = configuracao::tabela().buscar(mac);
  const nos::No* n = nos::encontrar(mac);
  e.objeto().campo("mac", txtMac).campo("nome", c ? c->nome : "desconhecido").campoBool("cadastrado", c != nullptr);
  e.chave("calibracao");
  if (c != nullptr && cadastro::temCalibracao(*c))
    e.objeto().campo("solo_mv_seco", int64_t(c->mvSeco)).campo("solo_mv_umido", int64_t(c->mvUmido)).fimObjeto();
  else
    e.nulo();

  const registro::Registro* u = (n != nullptr && n->temUltima) ? &n->ultima : nullptr;
  e.chave("visto");
  uint32_t ha = 0;
  if (u != nullptr && haQuanto(*u, ha)) e.objeto().campoNatural("ha_s", ha).fimObjeto();
  else e.nulo();
  e.chave("bateria_mv");
  if (u != nullptr) {
    protocolo::PacoteLeitura p = u->pacote;
    if (protocolo::lerEstado(p.estados, protocolo::Grandeza::ALIMENTACAO) != protocolo::Estado::ERRO)
      e.natural(p.alimentacao_mv);
    else
      e.nulo();
  } else {
    e.nulo();
  }
  e.chave("ultima_leitura");
  if (u != nullptr) leituraDoRegistro(e, -1, *u);
  else e.nulo();

  e.chave("estatisticas");
  if (n == nullptr) {
    e.nulo();
  } else {
    const auto& s = n->sequencia;
    uint32_t esperados = s.aceitos() + s.perdidos();
    e.objeto()
        .campoBool("desde_boot_do_coordenador", true)
        .campoNatural("recebidos", n->recebidos)
        .campoNatural("rejeitados", n->rejeitados)
        .campoNatural("aceitos", s.aceitos())
        .campoNatural("perdidos", s.perdidos())
        .chave("entrega_pct");
    if (esperados) e.decimal(static_cast<int32_t>((uint64_t)s.aceitos() * 10000 / esperados), 2);
    else e.nulo();
    e.campoNatural("duplicados_ou_antigos", s.descartados())
        .campoNatural("fora_de_ordem", s.foraDeOrdem())
        .campoNatural("sem_ack_no_no", n->semAck)
        .chave("rssi_dbm");
    if (n->recebidos)
      e.objeto()
          .chave("medio")
          .decimal(static_cast<int32_t>(n->somaRssi * 10 / static_cast<int32_t>(n->recebidos)), 1)
          .campo("min", int64_t(n->rssiMin))
          .campo("max", int64_t(n->rssiMax))
          .fimObjeto();
    else
      e.nulo();
    e.fimObjeto();
  }
  e.fimObjeto();
}

// ---------------------------------------------------------------- Parâmetros
bool lerMac(const String& texto, uint8_t mac[6]) { return cadastro::interpretarMac(texto.c_str(), mac); }

// Filtro do histórico a partir da query. false = já respondeu 400.
bool lerFiltro(filtro::Filtro& f, bool paginado) {
  if (servidor.hasArg("no")) {
    if (!lerMac(servidor.arg("no"), f.mac)) {
      erro400("no", "MAC invalido (ex.: 88:57:21:70:93:70)");
      return false;
    }
    f.porNo = true;
  }
  const char* horas[2] = {"de", "ate"};
  for (int k = 0; k < 2; k++) {
    if (!servidor.hasArg(horas[k])) continue;
    int64_t t = 0;
    if (!calendario::interpretar(servidor.arg(horas[k]).c_str(), t) || t < 0 || t > (int64_t)UINT32_MAX) {
      erro400(horas[k], "hora invalida (AAAA-MM-DDTHH:MM:SSZ ou epoch)");
      return false;
    }
    f.porHora = true;
    (k == 0 ? f.de : f.ate) = static_cast<uint32_t>(t);
  }
  if (f.porHora && !servidor.hasArg("ate")) f.ate = UINT32_MAX;
  if (f.porHora && f.de > f.ate) {
    erro400("de", "'de' depois de 'ate'");
    return false;
  }
  if (!paginado) return true;

  f.decrescente = true;  // padrão: mais recentes primeiro
  if (servidor.hasArg("ordem")) {
    String o = servidor.arg("ordem");
    if (o == "asc") f.decrescente = false;
    else if (o != "desc") {
      erro400("ordem", "use asc ou desc");
      return false;
    }
  }
  if (servidor.hasArg("cursor")) {
    String c = servidor.arg("cursor");
    char* fim = nullptr;
    unsigned long long v = strtoull(c.c_str(), &fim, 10);
    if (c.length() == 0 || *fim != '\0' || c[0] == '-') {
      erro400("cursor", "cursor invalido (use o proximo_cursor da pagina anterior)");
      return false;
    }
    f.cursor = v;
  }
  f.limite = config::API_PAGINA_PADRAO;
  if (servidor.hasArg("limite")) {
    long l = servidor.arg("limite").toInt();
    if (l < 1 || l > config::API_PAGINA_MAXIMA || servidor.arg("limite") != String(l)) {
      erro400("limite", "limite deve estar entre 1 e 200");
      return false;
    }
    f.limite = static_cast<uint16_t>(l);
  }
  return true;
}

// ---------------------------------------------------------------- Rotas GET
void rotaRaiz() {
  comecar();
  json::Escritor e(destino, nullptr);
  e.objeto().campo("servico", "horta-iot-espnow coordenador").campo("api", "/api/v1").chave("rotas").lista();
  for (const char* r : {"GET /api/v1/status", "GET /api/v1/nos", "GET /api/v1/nos/{mac}", "GET /api/v1/leituras/atuais",
                        "GET /api/v1/leituras", "GET /api/v1/exportar.csv", "GET /api/v1/alertas",
                        "PUT /api/v1/hora", "PATCH /api/v1/nos/{mac}", "POST /api/v1/nos/{mac}/calibrar"})
    e.texto(r);
  e.fimLista().campo("documentacao", "docs/api.md").fimObjeto();
  terminar(200);
}

void rotaStatus() {
  comecar();
  json::Escritor e(destino, nullptr);
  relogio::Instante a = relogio::agora();
  e.objeto()
      .campo("firmware", config::VERSAO_FIRMWARE)
      .campo("protocolo", int64_t(protocolo::VERSAO))
      .campo("api", int64_t(1))
      .campo("boot", int64_t(a.boot))
      .campoNatural("uptime_s", a.desdeBoot_ms / 1000)
      .chave("hora");
  horaAgora(e);

  const anel::Indice& ind = historico::indice();
  e.chave("armazenamento").objeto().campo("estado", historico::nomeEstado(historico::estado()));
  if (historico::montado())
    e.campoNatural("total_kb", LittleFS.totalBytes() / 1024).campoNatural("usado_kb", LittleFS.usedBytes() / 1024);
  else
    e.campoNulo("total_kb").campoNulo("usado_kb");
  e.campoNatural("registros", ind.totalValidos())
      .campoNatural("segmentos", ind.quantidade())
      .campoNatural("registros_por_segmento", ind.politica().registrosPorSegmento)
      .campoNatural("max_segmentos", ind.politica().maxSegmentos)
      .chave("mais_antigo_utc");
  // Hora de leitura mais antiga com hora válida (pelo resumo dos segmentos).
  bool achou = false;
  for (size_t i = 0; i < ind.quantidade() && !achou; i++)
    if (ind.em(i).comHora() > 0) {
      char iso[calendario::TAM_ISO];
      calendario::formatarIso(ind.em(i).utcMin, iso);
      e.texto(iso);
      achou = true;
    }
  if (!achou) e.nulo();
  e.fimObjeto();

  e.chave("rede")
      .objeto()
      .campo("canal", int64_t(config::CANAL_WIFI))
      .campo("clientes", int64_t(WiFi.softAPgetStationNum()))
      .campoNatural("espnow_descartes_fila", radio::descartadosFilaCheia())
      .fimObjeto();
  e.chave("memoria")
      .objeto()
      .campoNatural("heap_livre", ESP.getFreeHeap())
      .campoNatural("heap_minimo", ESP.getMinFreeHeap())
      .fimObjeto();
  e.campoBool("escrita_habilitada", autorizacao::configurado()).fimObjeto();
  terminar(200);
}

void rotaNos() {
  comecar();
  json::Escritor e(destino, nullptr);
  e.objeto().chave("nos").lista();
  const cadastro::Tabela& tab = configuracao::tabela();
  for (size_t i = 0; i < tab.quantidade(); i++) noJson(e, tab.em(i).mac);
  for (size_t i = 0; i < config::MAX_NOS; i++) {  // vistos, mas fora do cadastro
    const nos::No& n = nos::posicao(i);
    if (n.usado && tab.buscar(n.mac) == nullptr) noJson(e, n.mac);
  }
  e.fimLista().fimObjeto();
  terminar(200);
}

void rotaNo() {
  uint8_t mac[6];
  if (!lerMac(servidor.pathArg(0), mac)) {
    erro400("mac", "MAC invalido (ex.: 88:57:21:70:93:70)");
    return;
  }
  if (configuracao::tabela().buscar(mac) == nullptr && nos::encontrar(mac) == nullptr) {
    erro(404, "nao_encontrado", "mac", "no desconhecido");
    return;
  }
  comecar();
  json::Escritor e(destino, nullptr);
  noJson(e, mac);
  terminar(200);
}

void rotaAtuais() {
  comecar();
  json::Escritor e(destino, nullptr);
  e.objeto().chave("leituras").lista();
  for (size_t i = 0; i < config::MAX_NOS; i++) {
    const nos::No& n = nos::posicao(i);
    if (n.usado && n.temUltima) leituraDoRegistro(e, -1, n.ultima);
  }
  e.fimLista().fimObjeto();
  terminar(200);
}

bool visitarJson(const consulta::Item& it, void* ctx) {
  leituraDoRegistro(*static_cast<json::Escritor*>(ctx), static_cast<int64_t>(it.id), it.r);
  return clienteConectado();  // cliente desistiu: para de ler a flash
}

void rotaLeituras() {
  filtro::Filtro f;
  if (!lerFiltro(f, true)) return;
  f.maxLidos = config::API_MAX_LIDOS;  // uma página nunca lê a flash inteira
  comecar();
  json::Escritor e(destino, nullptr);
  e.objeto().chave("leituras").lista();
  consulta::Pagina pg = consulta::executar(f, visitarJson, &e);
  e.fimLista()
      .campoNatural("proximo_cursor", pg.proximoCursor)
      .campoBool("fim", pg.fim)
      .campoBool("cursor_rotacionado", pg.cursorRotacionado)
      .fimObjeto();
  terminar(200);
}

bool visitarCsv(const consulta::Item& it, void*) {
  uint8_t mac[6];
  memcpy(mac, it.r.mac, 6);
  int16_t pct = 0;
  bool temPct = configuracao::umidadeSolo(it.r, pct);
  char l[exportacao::TAM_LINHA];
  size_t n = exportacao::formatarLinha(l, it.id, it.r, nomeDoNo(mac), it.hora, it.utcEfetivo, temPct, pct);
  destino(l, n, nullptr);
  destino("\n", 1, nullptr);
  return clienteConectado();
}

void rotaCsv() {
  filtro::Filtro f;
  if (!lerFiltro(f, false)) return;
  f.decrescente = false;  // ordem de gravação, como na exportação pela serial
  f.limite = config::API_PAGINA_CSV;
  f.maxLidos = config::API_MAX_LIDOS;
  comecar(TIPO_CSV);
  servidor.sendHeader("Content-Disposition", "attachment; filename=\"historico.csv\"");
  const char* cab = exportacao::cabecalho();
  destino(cab, strlen(cab), nullptr);
  destino("\n", 1, nullptr);
  despejar();  // começa o chunked já: o cabeçalho HTTP sai antes das páginas
  for (;;) {
    consulta::Pagina pg = consulta::executar(f, visitarCsv, nullptr);
    if (pg.fim || !clienteConectado()) break;
    f.cursor = pg.proximoCursor;
    despejar();
    if (ocioso != nullptr) ocioso();  // entre páginas: ESP-NOW (nunca dentro de uma página)
  }
  terminar(200);
}

void rotaAlertas() {
  comecar();
  json::Escritor e(destino, nullptr);
  e.objeto().chave("alertas").lista().fimLista().campo("regras", "nao_implementadas").fimObjeto();
  terminar(200);
}

// ---------------------------------------------------------------- Rotas de escrita
void rotaHora() {
  if (!exigirToken()) return;
  Corpo c;
  if (!lerCorpo(c)) return;
  const char* s = c.texto.c_str();
  int ku = json::buscar(s, c.t, c.n, 0, "utc"), ke = json::buscar(s, c.t, c.n, 0, "epoch");
  int64_t utc = 0;
  char txt[32];
  if (ku >= 0) {
    if (!json::comoTexto(s, c.t[ku], txt, sizeof(txt)) || !calendario::interpretar(txt, utc)) {
      erro400("utc", "use AAAA-MM-DDTHH:MM:SSZ");
      return;
    }
  } else if (ke >= 0) {
    if (!json::comoInteiro(s, c.t[ke], utc)) {
      erro400("epoch", "use um inteiro (segundos desde 1970, UTC)");
      return;
    }
  } else {
    erro400("utc", "informe 'utc' ou 'epoch'");
    return;
  }
  if (!relogio::acertar(utc)) {
    erro400("utc", "fora da faixa plausivel (2026-01-01 a 2100-01-01)");
    return;
  }
  relogio::Acerto a{};
  relogio::ultimoAcerto(a);
  Serial.printf("[HTTP] hora acertada pela API; ajuste de %+lld s\n", static_cast<long long>(a.salto_s));
  comecar();
  json::Escritor e(destino, nullptr);
  e.objeto().chave("hora");
  horaAgora(e);
  e.chave("ajuste_s");
  if (a.havia) e.inteiro(a.salto_s);
  else e.nulo();
  e.fimObjeto();
  terminar(200);
}

void respostaEdicao(configuracao::Edicao ed, cadastro::Resultado motivo, const uint8_t mac[6], int codigoOk) {
  switch (ed) {
    case configuracao::Edicao::OK: {
      comecar();
      json::Escritor e(destino, nullptr);
      noJson(e, mac);
      terminar(codigoOk);
      return;
    }
    case configuracao::Edicao::NAO_ENCONTRADO:
      erro(404, "nao_encontrado", "mac", "no nao cadastrado (para cadastrar, envie o nome)");
      return;
    case configuracao::Edicao::SEM_LEITURA:
      erro(409, "conflito", "ponto", "o no ainda nao tem leitura do solo em estado ok");
      return;
    case configuracao::Edicao::FALHA_NVS:
      erro(500, "falha_nvs", nullptr, "nao foi possivel gravar o cadastro");
      return;
    case configuracao::Edicao::INVALIDA:
      break;
  }
  const char* campo = motivo == cadastro::Resultado::NOME_INVALIDO ? "nome"
                      : motivo == cadastro::Resultado::TABELA_CHEIA ? "mac"
                                                                    : "calibracao";
  int codigo = motivo == cadastro::Resultado::TABELA_CHEIA ? 409 : 400;
  erro(codigo, codigo == 409 ? "conflito" : "parametro_invalido", campo, cadastro::nomeResultado(motivo));
}

void rotaEditarNo() {
  if (!exigirToken()) return;
  uint8_t mac[6];
  if (!lerMac(servidor.pathArg(0), mac)) {
    erro400("mac", "MAC invalido (ex.: 88:57:21:70:93:70)");
    return;
  }
  Corpo c;
  if (!lerCorpo(c)) return;
  const char* s = c.texto.c_str();
  configuracao::Alteracao a;
  char nome[cadastro::TAM_NOME + 8];
  int k = json::buscar(s, c.t, c.n, 0, "nome");
  if (k >= 0) {
    if (!json::comoTexto(s, c.t[k], nome, sizeof(nome))) {
      erro400("nome", "nome deve ser texto de 1 a 15 caracteres [A-Za-z0-9_-]");
      return;
    }
    a.nome = nome;
  }
  k = json::buscar(s, c.t, c.n, 0, "calibracao");
  if (k >= 0) {
    if (json::ehNulo(s, c.t[k])) {
      a.removerCalibracao = true;
    } else {
      int ks = json::buscar(s, c.t, c.n, k, "solo_mv_seco"), ku = json::buscar(s, c.t, c.n, k, "solo_mv_umido");
      int64_t seco = 0, umido = 0;
      if (ks < 0 || ku < 0 || !json::comoInteiro(s, c.t[ks], seco) || !json::comoInteiro(s, c.t[ku], umido) ||
          seco < 0 || seco > 65535 || umido < 0 || umido > 65535) {
        erro400("calibracao", "use {\"solo_mv_seco\": N, \"solo_mv_umido\": N} (mV) ou null");
        return;
      }
      a.mudarCalibracao = true;
      a.mvSeco = static_cast<uint16_t>(seco);
      a.mvUmido = static_cast<uint16_t>(umido);
    }
  }
  if (a.nome == nullptr && !a.mudarCalibracao && !a.removerCalibracao) {
    erro400(nullptr, "nada a alterar (campos: nome, calibracao)");
    return;
  }
  cadastro::Resultado motivo;
  bool novo = false;
  configuracao::Edicao ed = configuracao::editar(mac, a, motivo, novo);
  if (ed == configuracao::Edicao::OK) Serial.printf("[HTTP] cadastro alterado pela API (%s)\n", novo ? "no novo" : "edicao");
  respostaEdicao(ed, motivo, mac, novo ? 201 : 200);
}

void rotaCalibrar() {
  if (!exigirToken()) return;
  uint8_t mac[6];
  if (!lerMac(servidor.pathArg(0), mac)) {
    erro400("mac", "MAC invalido (ex.: 88:57:21:70:93:70)");
    return;
  }
  Corpo c;
  if (!lerCorpo(c)) return;
  const char* s = c.texto.c_str();
  char ponto[8];
  int k = json::buscar(s, c.t, c.n, 0, "ponto");
  if (k < 0 || !json::comoTexto(s, c.t[k], ponto, sizeof(ponto)) ||
      (strcmp(ponto, "seco") != 0 && strcmp(ponto, "umido") != 0)) {
    erro400("ponto", "use {\"ponto\": \"seco\"} ou {\"ponto\": \"umido\"}");
    return;
  }
  cadastro::Resultado motivo;
  uint16_t mv = 0;
  configuracao::Edicao ed = configuracao::calibrarPelaUltima(
      mac, strcmp(ponto, "seco") == 0 ? cadastro::Ponto::SECO : cadastro::Ponto::UMIDO, motivo, mv);
  if (ed == configuracao::Edicao::OK) Serial.printf("[HTTP] calibracao pela API: ponto %s = %u mV\n", ponto, mv);
  if (ed == configuracao::Edicao::INVALIDA && motivo == cadastro::Resultado::ORDEM_INVALIDA) {
    erro(409, "conflito", "ponto", "com este ponto, o seco nao ficaria maior que o umido");
    return;
  }
  respostaEdicao(ed, motivo, mac, 200);
}

// ---------------------------------------------------------------- 404 e 405
// Métodos aceitos por um caminho conhecido (para o 405 com Allow), ou nullptr.
const char* permitidos(const String& uri) {
  static const struct {
    const char* caminho;
    const char* metodos;
  } FIXAS[] = {{"/", "GET"},
               {"/api/v1/status", "GET"},
               {"/api/v1/nos", "GET"},
               {"/api/v1/leituras/atuais", "GET"},
               {"/api/v1/leituras", "GET"},
               {"/api/v1/exportar.csv", "GET"},
               {"/api/v1/alertas", "GET"},
               {"/api/v1/hora", "PUT"}};
  for (const auto& f : FIXAS)
    if (uri == f.caminho) return f.metodos;
  static const char PREFIXO[] = "/api/v1/nos/";
  if (uri.startsWith(PREFIXO)) {
    String resto = uri.substring(sizeof(PREFIXO) - 1);
    int barra = resto.indexOf('/');
    if (barra < 0) return "GET, PATCH";
    if (resto.substring(barra) == "/calibrar") return "POST";
  }
  return nullptr;
}

void rotaNaoEncontrada() {
  const char* m = permitidos(servidor.uri());
  if (m != nullptr) {
    servidor.sendHeader("Allow", m);
    erro(405, "metodo_nao_permitido", nullptr, "metodo nao suportado nesta rota (veja o cabecalho Allow)");
  } else {
    erro(404, "nao_encontrado", nullptr, "rota inexistente (veja GET /)");
  }
}

// ---------------------------------------------------------------- Log
const char* nomeMetodo(HTTPMethod m) {
  switch (m) {
    case HTTP_GET: return "GET";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_POST: return "POST";
    case HTTP_DELETE: return "DELETE";
    case HTTP_OPTIONS: return "OPTIONS";
    case HTTP_HEAD: return "HEAD";
    default: return "?";
  }
}

// Envolve cada rota com a medição: [HTTP] metodo,uri,codigo,bytes,us.
// O tempo é o do atendimento no coordenador (da rota ao fim da resposta);
// o tempo completo, com a conexão TCP, é medido no cliente (passo 6).
template <void (*Rota)()>
void medida() {
  int64_t t0 = esp_timer_get_time();
  codigoFinal = 0;
  Rota();
  uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
  Serial.printf("[HTTP] %s,%s,%d,%u,%lu\n", nomeMetodo(servidor.method()), servidor.uri().c_str(), codigoFinal,
                (unsigned)saida.total, (unsigned long)us);
}

}  // namespace

void iniciar(Ocioso funcaoOciosa) {
  ocioso = funcaoOciosa;
  static const char* CABECALHOS[] = {"Authorization"};
  servidor.collectHeaders(CABECALHOS, 1);
  servidor.on("/", HTTP_GET, medida<rotaRaiz>);
  servidor.on("/api/v1/status", HTTP_GET, medida<rotaStatus>);
  servidor.on("/api/v1/nos", HTTP_GET, medida<rotaNos>);
  servidor.on("/api/v1/leituras/atuais", HTTP_GET, medida<rotaAtuais>);
  servidor.on("/api/v1/leituras", HTTP_GET, medida<rotaLeituras>);
  servidor.on("/api/v1/exportar.csv", HTTP_GET, medida<rotaCsv>);
  servidor.on("/api/v1/alertas", HTTP_GET, medida<rotaAlertas>);
  servidor.on("/api/v1/hora", HTTP_PUT, medida<rotaHora>);
  servidor.on(UriBraces("/api/v1/nos/{}"), HTTP_GET, medida<rotaNo>);
  servidor.on(UriBraces("/api/v1/nos/{}"), HTTP_PATCH, medida<rotaEditarNo>);
  servidor.on(UriBraces("/api/v1/nos/{}/calibrar"), HTTP_POST, medida<rotaCalibrar>);
  servidor.onNotFound(medida<rotaNaoEncontrada>);
  servidor.begin();
  Serial.printf("[HTTP] servidor na porta 80: http://%s/api/v1/status\n", WiFi.softAPIP().toString().c_str());
  Serial.println("[HTTP] metodo,uri,codigo,bytes,us");
}

void atender() { servidor.handleClient(); }

}  // namespace api
