#ifdef TESTE_GRAVACAO

#include "teste_gravacao.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <atomic>
#include <cstring>

#include <registro.h>

namespace teste_gravacao {

namespace {

constexpr const char* ARQ_BENCH = "/bench.seg";
constexpr const char* ARQ_ESTRESSE = "/estresse.seg";
constexpr size_t TAM = sizeof(registro::Registro);
constexpr size_t MAX_LOTE = 64;

registro::Registro exemplo(uint32_t i) {
  registro::Metadados m{};
  m.utc_s = 1791300000 + i;
  m.horaValida = true;
  protocolo::PacoteLeitura p{};
  p.versao = protocolo::VERSAO;
  p.seq = i;
  return registro::montar(m, p);
}

struct Estatistica {
  uint64_t soma = 0;
  uint32_t max = 0;
  uint32_t n = 0;
  void somar(uint32_t us) {
    soma += us;
    n++;
    if (us > max) max = us;
  }
};

void linha(const char* nome, uint32_t i, uint32_t us) { Serial.printf("[BENCH] %s,%lu,%lu\n", nome, (unsigned long)i, (unsigned long)us); }

void bench(const char* estrategia, uint32_t n, uint32_t lote) {
  LittleFS.remove(ARQ_BENCH);
  if (lote < 1) lote = 1;
  if (lote > MAX_LOTE) lote = MAX_LOTE;
  Serial.printf("[BENCH] inicio %s n=%lu lote=%lu, usado %u KB\n", estrategia, (unsigned long)n, (unsigned long)lote,
                (unsigned)(LittleFS.usedBytes() / 1024));
  Estatistica e;
  uint32_t inicio = millis();
  File f;
  static registro::Registro buf[MAX_LOTE];
  uint32_t noBuf = 0;
  bool aberto = strcmp(estrategia, "E2") == 0 || strcmp(estrategia, "E3") == 0;
  if (aberto) f = LittleFS.open(ARQ_BENCH, FILE_APPEND);

  for (uint32_t i = 0; i < n; i++) {
    registro::Registro r = exemplo(i);
    uint32_t t0 = micros();
    if (strcmp(estrategia, "E1") == 0) {
      File g = LittleFS.open(ARQ_BENCH, FILE_APPEND);
      g.write(reinterpret_cast<const uint8_t*>(&r), TAM);
      g.close();
    } else if (strcmp(estrategia, "E2") == 0) {
      f.write(reinterpret_cast<const uint8_t*>(&r), TAM);
      f.flush();  // fflush + fsync = lfs_file_sync
    } else if (strcmp(estrategia, "E3") == 0) {
      f.write(reinterpret_cast<const uint8_t*>(&r), TAM);
      if ((i + 1) % lote == 0) f.flush();
    } else {  // E4
      buf[noBuf++] = r;
      if (noBuf == lote) {
        File g = LittleFS.open(ARQ_BENCH, FILE_APPEND);
        g.write(reinterpret_cast<const uint8_t*>(buf), noBuf * TAM);
        g.close();
        noBuf = 0;
      }
    }
    uint32_t us = micros() - t0;
    e.somar(us);
    linha(estrategia, i, us);
    if ((i & 15) == 15) delay(1);  // deixa a tarefa ociosa rodar
  }
  if (aberto) f.close();
  File g = LittleFS.open(ARQ_BENCH, FILE_READ);
  size_t tamanho = g.size();
  g.close();
  Serial.printf("[BENCH] fim %s n=%lu lote=%lu: media %lu us, max %lu us, total %lu ms, arquivo %u bytes\n", estrategia,
                (unsigned long)n, (unsigned long)lote, (unsigned long)(e.soma / e.n), (unsigned long)e.max,
                (unsigned long)(millis() - inicio), (unsigned)tamanho);
  LittleFS.remove(ARQ_BENCH);
}

// ---------------------------------------------------------------- Estresse
std::atomic<bool> estresseLigado{false};
std::atomic<uint32_t> estresseGravacoes{0};
TaskHandle_t tarefaEstresse = nullptr;

// Mesma prioridade e núcleo do loop() (1, núcleo 1); o Wi-Fi roda no núcleo 0.
void estresse(void*) {
  uint32_t i = 0;
  while (estresseLigado) {
    registro::Registro r = exemplo(i++);
    File g = LittleFS.open(ARQ_ESTRESSE, FILE_APPEND);
    g.write(reinterpret_cast<const uint8_t*>(&r), TAM);
    size_t tamanho = g.size();
    g.close();
    if (tamanho >= 500 * TAM) LittleFS.remove(ARQ_ESTRESSE);  // ciclo de um segmento
    estresseGravacoes++;
    vTaskDelay(1);
  }
  LittleFS.remove(ARQ_ESTRESSE);
  tarefaEstresse = nullptr;
  vTaskDelete(nullptr);
}

}  // namespace

void comando(const char* argumento) {
  char estrategia[16] = {};
  unsigned long n = 0, lote = 1;
  if (argumento == nullptr) argumento = "";
  if (strcmp(argumento, "estresse on") == 0) {
    if (tarefaEstresse == nullptr) {
      estresseGravacoes = 0;
      estresseLigado = true;
      xTaskCreatePinnedToCore(estresse, "estresse", 4096, nullptr, 1, &tarefaEstresse, 1);
    }
    Serial.println("[BENCH] estresse ligado");
    return;
  }
  if (strcmp(argumento, "estresse off") == 0) {
    estresseLigado = false;
    Serial.printf("[BENCH] estresse desligado: %lu gravacoes\n", (unsigned long)estresseGravacoes.load());
    return;
  }
  int lidos = sscanf(argumento, "%15s %lu %lu", estrategia, &n, &lote);
  bool valida = strcmp(estrategia, "E1") == 0 || strcmp(estrategia, "E2") == 0 || strcmp(estrategia, "E3") == 0 ||
                strcmp(estrategia, "E4") == 0;
  if (lidos < 2 || !valida || n == 0 || n > 5000) {
    Serial.println("[BENCH] uso: bench E1|E2 N | bench E3|E4 N LOTE | bench estresse on|off");
    return;
  }
  bench(estrategia, n, lote);
}

}  // namespace teste_gravacao

#endif  // TESTE_GRAVACAO
