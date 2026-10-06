#include "consulta.h"

#include <LittleFS.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_mac.h>

#include <calendario.h>

#include "historico.h"
#include "nos.h"

namespace consulta {

namespace {

constexpr size_t TAM_REGISTRO = sizeof(registro::Registro);
constexpr uint32_t REGISTROS_POR_LEITURA = 16;  // buffer de 768 B

filtro::Ancoras tabelaAncoras;

// Janela de registros do segmento aberto já lida para o buffer.
struct Leitor {
  File arquivo;
  size_t segmentoAberto = SIZE_MAX;
  uint32_t ini = 0, fim = 0;  // posições [ini, fim) no buffer
  uint8_t buf[REGISTROS_POR_LEITURA * TAM_REGISTRO];

  void fechar() {
    if (arquivo) arquivo.close();
    segmentoAberto = SIZE_MAX;
    ini = fim = 0;
  }

  bool abrir(size_t segmento, uint32_t numero) {
    fechar();
    char c[historico::TAM_CAMINHO];
    historico::caminhoSegmento(numero, c);
    arquivo = LittleFS.open(c, FILE_READ);
    if (!arquivo) return false;
    segmentoAberto = segmento;
    return true;
  }

  // Ponteiro para o registro `pos`; carrega a janela em volta dele na direção
  // do percurso (16 para frente ou 16 para trás). nullptr se a leitura falhar.
  const uint8_t* registro(uint32_t pos, uint32_t posicoes, bool decrescente) {
    if (pos < ini || pos >= fim) {
      uint32_t a, b;
      if (!decrescente) {
        a = pos;
        b = pos + REGISTROS_POR_LEITURA < posicoes ? pos + REGISTROS_POR_LEITURA : posicoes;
      } else {
        a = pos + 1 > REGISTROS_POR_LEITURA ? pos + 1 - REGISTROS_POR_LEITURA : 0;
        b = pos + 1;
      }
      size_t bytes = static_cast<size_t>(b - a) * TAM_REGISTRO;
      if (!arquivo.seek(a * TAM_REGISTRO) || arquivo.read(buf, bytes) != bytes) {
        ini = fim = 0;
        return nullptr;
      }
      ini = a;
      fim = b;
    }
    return buf + static_cast<size_t>(pos - ini) * TAM_REGISTRO;
  }
};

Leitor leitor;  // estático: 768 B fora da pilha do loop()

bool lerMac(const char* s, uint8_t mac[6]) {
  unsigned v[6];
  if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i = 0; i < 6; i++) {
    if (v[i] > 0xFF) return false;
    mac[i] = static_cast<uint8_t>(v[i]);
  }
  return true;
}

void imprimirItem(const Item& it) {
  char iso[calendario::TAM_ISO] = "-";
  if (it.hora != filtro::Hora::SEM_HORA) calendario::formatarIso(it.utcEfetivo, iso);
  protocolo::PacoteLeitura p = it.r.pacote;  // cópia: não ler campos packed por referência
  Serial.printf("[CONSULTA] %llu," MACSTR ",%s,%s,%u,%lu,%u,%lu,%d,%u,%u,%u,%d\n", (unsigned long long)it.id,
                MAC2STR(it.r.mac), filtro::nomeHora(it.hora), iso, it.r.boot_coord,
                (unsigned long)it.r.desde_boot_s, p.boot, (unsigned long)p.seq, p.temperatura_c100,
                p.umidade_ar_c100, p.solo_mv, p.alimentacao_mv, it.r.rssi);
}

struct ContextoSerial {
  bool mostrar;
};

bool visitarSerial(const Item& it, void* ctx) {
  if (static_cast<ContextoSerial*>(ctx)->mostrar) imprimirItem(it);
  return true;
}

void imprimirUltimas() {
  Serial.println("[CONSULTA] ultimas: mac,nome,hora,utc,boot_coord,seq,solo_mv,alim_mv,rssi");
  for (size_t i = 0; i < config::MAX_NOS; i++) {
    const nos::No& n = nos::posicao(i);
    if (!n.usado || !n.temUltima) continue;
    uint32_t utc = 0;
    filtro::Hora h = filtro::horaEfetiva(n.ultima, tabelaAncoras, utc);
    char iso[calendario::TAM_ISO] = "-";
    if (h != filtro::Hora::SEM_HORA) calendario::formatarIso(utc, iso);
    protocolo::PacoteLeitura p = n.ultima.pacote;
    Serial.printf("[CONSULTA] " MACSTR ",%s,%s,%s,%u,%lu,%u,%u,%d\n", MAC2STR(n.mac), n.nome, filtro::nomeHora(h), iso,
                  n.ultima.boot_coord, (unsigned long)p.seq, p.solo_mv, p.alimentacao_mv, n.ultima.rssi);
  }
}

}  // namespace

void observar(const registro::Registro& r) {
  tabelaAncoras.registrar(r);
  nos::atualizarUltima(r);
}

const filtro::Ancoras& ancoras() { return tabelaAncoras; }

const registro::Registro* ultimaLeitura(const uint8_t mac[6]) {
  const nos::No* n = nos::encontrar(mac);
  return (n != nullptr && n->temUltima) ? &n->ultima : nullptr;
}

Pagina executar(const filtro::Filtro& f, Visitante visitar, void* contexto) {
  uint32_t t0 = micros();
  Pagina pg;
  if (!historico::montado()) {
    pg.fim = true;
    return pg;
  }
  const anel::Indice& ind = historico::indice();
  const uint16_t limite = f.limite > 0 ? f.limite : 1;
  filtro::Percurso p(ind, f.cursor, f.decrescente);
  pg.cursorRotacionado = p.cursorRotacionado();

  bool visitouAlgum = false;
  uint64_t ultimoVisitado = 0;
  bool interrompido = false;
  while (p.valido()) {
    if (pg.entregues >= limite || (f.maxLidos > 0 && pg.lidos >= f.maxLidos)) break;

    if (p.segmento() != leitor.segmentoAberto) {
      const anel::Resumo& s = ind.em(p.segmento());
      if (filtro::podePular(f, s)) {
        pg.segmentosPulados++;
        p.pularSegmento();
        continue;
      }
      if (!leitor.abrir(p.segmento(), s.numero)) {  // arquivo sumiu: segue para o próximo
        pg.segmentosPulados++;
        p.pularSegmento();
        continue;
      }
    }

    const uint8_t* bytes = leitor.registro(p.posicao(), ind.em(p.segmento()).posicoes, f.decrescente);
    pg.lidos++;
    Item it;
    if (bytes == nullptr || registro::ler(bytes, TAM_REGISTRO, it.r) != registro::Erro::NENHUM) {
      pg.invalidos++;
    } else {
      it.id = p.id();
      it.hora = filtro::horaEfetiva(it.r, tabelaAncoras, it.utcEfetivo);
      if (filtro::passa(f, it.r, it.hora, it.utcEfetivo)) {
        pg.entregues++;
        if (!visitar(it, contexto)) interrompido = true;
      }
    }
    visitouAlgum = true;
    ultimoVisitado = p.id();
    p.avancar();
    if (interrompido) break;
  }
  leitor.fechar();

  if (p.valido()) {
    pg.proximoCursor = p.id();  // primeira posição ainda não visitada
  } else {
    pg.fim = true;
    // Crescente: o cursor continua valendo para buscar só os registros novos.
    // Decrescente: não há nada mais antigo.
    if (!f.decrescente) pg.proximoCursor = visitouAlgum ? ultimoVisitado + 1 : f.cursor;
  }
  pg.duracaoUs = micros() - t0;
  return pg;
}

void comando(const char* argumento) {
  if (argumento != nullptr && strcmp(argumento, "ultimas") == 0) {
    imprimirUltimas();
    return;
  }
  filtro::Filtro f;
  ContextoSerial ctx{true};
  char copia[160];
  strncpy(copia, argumento ? argumento : "", sizeof(copia) - 1);
  copia[sizeof(copia) - 1] = '\0';
  for (char* tok = strtok(copia, " "); tok != nullptr; tok = strtok(nullptr, " ")) {
    char* v = strchr(tok, '=');
    if (v == nullptr) {
      Serial.printf("[CONSULTA] parametro sem '=': %s\n", tok);
      return;
    }
    *v++ = '\0';
    int64_t t = 0;
    bool ok = true;
    if (strcmp(tok, "de") == 0 || strcmp(tok, "ate") == 0) {
      ok = calendario::interpretar(v, t) && t >= 0 && t <= static_cast<int64_t>(UINT32_MAX);
      if (ok) {
        f.porHora = true;
        if (tok[0] == 'd') f.de = static_cast<uint32_t>(t);
        else f.ate = static_cast<uint32_t>(t);
      }
    } else if (strcmp(tok, "no") == 0) {
      ok = lerMac(v, f.mac);
      f.porNo = ok;
    } else if (strcmp(tok, "ordem") == 0) {
      ok = strcmp(v, "asc") == 0 || strcmp(v, "desc") == 0;
      f.decrescente = strcmp(v, "desc") == 0;
    } else if (strcmp(tok, "cursor") == 0) {
      f.cursor = strtoull(v, nullptr, 10);
    } else if (strcmp(tok, "limite") == 0) {
      long n = atol(v);
      ok = n >= 1 && n <= 60000;
      f.limite = static_cast<uint16_t>(n);
    } else if (strcmp(tok, "maxlidos") == 0) {
      f.maxLidos = strtoul(v, nullptr, 10);
    } else if (strcmp(tok, "mostrar") == 0) {
      ctx.mostrar = atoi(v) != 0;
    } else {
      ok = false;
    }
    if (!ok) {
      Serial.printf("[CONSULTA] parametro invalido: %s=%s\n", tok, v);
      return;
    }
  }
  if (f.porHora && f.ate == 0) f.ate = UINT32_MAX;  // só "de": até o fim

  if (ctx.mostrar)
    Serial.println("[CONSULTA] id,mac,hora,utc,boot_coord,desde_boot_s,boot_no,seq,temp_c100,ur_c100,solo_mv,"
                   "alim_mv,rssi");
  size_t heapAntes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  heap_caps_monitor_local_minimum_free_size_start();
  Pagina pg = executar(f, visitarSerial, &ctx);
  size_t heapMin = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  heap_caps_monitor_local_minimum_free_size_stop();
  size_t heapDepois = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  Serial.printf("[CONSULTA] pagina: entregues=%u fim=%d proximo=%llu rotacionado=%d lidos=%lu invalidos=%lu "
                "pulados=%lu us=%lu heap_antes=%u heap_min=%u heap_depois=%u\n",
                pg.entregues, pg.fim, (unsigned long long)pg.proximoCursor, pg.cursorRotacionado,
                (unsigned long)pg.lidos, (unsigned long)pg.invalidos, (unsigned long)pg.segmentosPulados,
                (unsigned long)pg.duracaoUs, (unsigned)heapAntes, (unsigned)heapMin, (unsigned)heapDepois);
}

}  // namespace consulta
