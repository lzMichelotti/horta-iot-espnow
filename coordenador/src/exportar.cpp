#include "exportar.h"

#include <cstring>

#include <exportacao.h>

#include "configuracao.h"
#include "consulta.h"
#include "nos.h"

namespace exportar {

namespace {

constexpr uint16_t PAGINA = 50;          // ~8 KB na serial: ~0,7 s a 115200 baud
constexpr uint32_t MAX_LIDOS = 2000;     // por página, num filtro que descarta quase tudo

bool emAndamento = false;
filtro::Filtro filtroAtual;
uint64_t cursor = 0;
uint32_t linhas = 0;
uint32_t crc = 0;
uint32_t inicioMs = 0;

void enviarLinha(const char* l, size_t n) {
  Serial.print("[EXP] ");
  Serial.write(reinterpret_cast<const uint8_t*>(l), n);
  Serial.write('\n');
  crc = exportacao::crc32(crc, reinterpret_cast<const uint8_t*>(l), n);
  crc = exportacao::crc32(crc, reinterpret_cast<const uint8_t*>("\n"), 1);
}

bool visitar(const consulta::Item& it, void*) {
  uint8_t mac[6];
  memcpy(mac, it.r.mac, 6);
  const nos::No* n = nos::encontrar(mac);
  const cadastro::No* c = configuracao::tabela().buscar(mac);
  const char* nome = c ? c->nome : (n ? n->nome : "desconhecido");
  int16_t pct = 0;
  bool temPct = configuracao::umidadeSolo(it.r, pct);
  char l[exportacao::TAM_LINHA];
  size_t tam = exportacao::formatarLinha(l, it.id, it.r, nome, it.hora, it.utcEfetivo, temPct, pct);
  enviarLinha(l, tam);
  linhas++;
  return true;
}

}  // namespace

bool ativa() { return emAndamento; }

void comando(const char* argumento) {
  if (argumento != nullptr && strcmp(argumento, "parar") == 0) {
    if (emAndamento) Serial.printf("[EXPORT] interrompida em linhas=%lu\n", (unsigned long)linhas);
    emAndamento = false;
    return;
  }
  if (emAndamento) {
    Serial.println("[EXPORT] ERRO: ja existe uma exportacao em andamento ('exportar parar')");
    return;
  }
  filtro::Filtro f;
  if (!consulta::interpretarFiltro(argumento, f, nullptr, "[EXPORT] ERRO:")) return;
  f.decrescente = false;  // o CSV sai em ordem de gravação
  f.limite = PAGINA;
  f.maxLidos = MAX_LIDOS;
  filtroAtual = f;
  cursor = f.cursor;
  linhas = 0;
  crc = 0;
  inicioMs = millis();
  emAndamento = true;
  Serial.println("[EXPORT] inicio");
  const char* cab = exportacao::cabecalho();
  enviarLinha(cab, strlen(cab));
}

void passo() {
  if (!emAndamento) return;
  filtroAtual.cursor = cursor;
  consulta::Pagina pg = consulta::executar(filtroAtual, visitar, nullptr);
  if (pg.fim) {
    emAndamento = false;
    Serial.printf("[EXPORT] fim linhas=%lu crc32=%08lX ms=%lu\n", (unsigned long)linhas, (unsigned long)crc,
                  (unsigned long)(millis() - inicioMs));
    return;
  }
  cursor = pg.proximoCursor;
}

}  // namespace exportar
