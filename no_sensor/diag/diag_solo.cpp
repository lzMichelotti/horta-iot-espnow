// Firmware de DIAGNÓSTICO do sensor capacitivo de solo (não é o firmware de produção).
// Ambiente: pio run -e diag_solo -t upload   (no_sensor/platformio.ini)
//
// - Não entra em deep sleep; o rádio fica DESLIGADO até o comando 'r'.
// - Saída em CSV na serial. Linhas que começam com '#' são comentários (filtrar na planilha).
// - Pinos, atenuação, calibração e razão do divisor vêm de src/config.h (os mesmos da produção).
//
// API de ADC do Arduino-ESP32 3.x (núcleo 3.3.12 fixado no platformio.ini):
//   https://docs.espressif.com/projects/arduino-esp32/en/latest/api/adc.html
//   analogRead() = valor bruto sem calibração; analogReadMilliVolts() = mV com a
//   calibração de fábrica (eFuse); analogSetAttenuation() = atenuação global.
//
// Comandos (uma letra + Enter):
//   1  Teste 1/6/7: 2 Hz por 30 s, leitura única (N=1)
//   2  Teste 2: tempo de estabilização (10 ms por 2 s após ligar o VCC do sensor)
//   3  Teste 3: leitura única × média de 16 × média de 64 (60 rodadas)
//   p  Patamar (Testes 4, 5, 9): média de 64, 1 por segundo, por 60 s
//   f  Pinos ADC1 soltos/flutuando (Teste 8)
//   r  Liga/desliga o rádio (ESP-NOW transmitindo em broadcast) — Teste 6
//   s  Liga/desliga o segundo sensor no GPIO33 — Teste 9
//   g  Liga/desliga o GPIO25 em nível alto, drive 3 (~40 mA): alimentar o
//      sensor pelo pino em vez do 3V3 (Etapa 5, 26/11/2026: GPIO × MOSFET)
//   h  Ajuda
#include <Arduino.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "config.h"

namespace {

constexpr uint8_t PINO_SOLO2 = 33;  // ADC1_CH5: segundo sensor (Teste 9)
constexpr gpio_num_t PINO_GPIO_ALIM = GPIO_NUM_25;  // comando 'g'
bool gpioAlimLigado = false;
constexpr uint8_t PINOS_ADC1[] = {32, 33, 34, 35, 36, 39};

bool radioLigado = false;
bool segundoSensor = false;
uint32_t pacotesEnviados = 0, errosEnvio = 0;
uint32_t ultimoEnvioMs = 0;
const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---------------------------------------------------------------- Rádio (Teste 6)
// Transmite 200 bytes em broadcast a cada 20 ms enquanto mede, para o rádio
// trabalhar o máximo possível durante as leituras. O tamanho não bate com o
// pacote do projeto, então o coordenador o descarta.
void ligarRadio() {
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(config::CANAL_WIFI, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("# ERRO: esp_now_init falhou");
    WiFi.mode(WIFI_OFF);
    return;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = config::CANAL_WIFI;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
  pacotesEnviados = errosEnvio = 0;
  radioLigado = true;
}

void desligarRadio() {
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  radioLigado = false;
}

void manterRadio() {
  if (!radioLigado || millis() - ultimoEnvioMs < 20) return;
  ultimoEnvioMs = millis();
  static uint8_t carga[200] = {0};
  if (esp_now_send(BROADCAST, carga, sizeof(carga)) == ESP_OK) pacotesEnviados++;
  else errosEnvio++;
}

// Espera ocupada que continua transmitindo (se o rádio estiver ligado).
void esperarMs(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    manterRadio();
    delay(1);
  }
}

// ---------------------------------------------------------------- Leitura
struct Leitura {
  float bruto;      // média de analogRead()
  float mv;         // média de analogReadMilliVolts()
  uint32_t durUs;   // tempo só das n leituras em mV (custo da estratégia da produção)
};

// As médias bruta e em mV vêm de conversões separadas (o Arduino-ESP32 não
// expõe a conversão bruto → mV): primeiro n leituras brutas, depois n em mV.
Leitura ler(uint8_t pino, int n) {
  uint32_t somaB = 0, somaMv = 0;
  for (int i = 0; i < n; i++) somaB += analogRead(pino);
  uint32_t t0 = micros();
  for (int i = 0; i < n; i++) somaMv += analogReadMilliVolts(pino);
  uint32_t dur = micros() - t0;
  return {(float)somaB / n, (float)somaMv / n, dur};
}

// Mesma fórmula de src/sensores.cpp, mas SEM limitar a 0–100 %, para mostrar
// quanto a leitura cai fora da calibração.
float umidadePct(float mv) {
  return (config::SOLO_MV_SECO - mv) / (config::SOLO_MV_SECO - config::SOLO_MV_UMIDO) * 100.0f;
}

float alimentacaoMv() { return ler(config::PINO_ALIMENTACAO, 16).mv / config::RAZAO_DIVISOR; }

void cabecalho() {
  Serial.println("tempo_ms,teste,pino,n,bruto,mv,umidade_pct,alim_mv,radio,dur_us");
}

void linha(const char* teste, uint8_t pino, int n, const Leitura& l, float alim) {
  Serial.printf("%lu,%s,%u,%d,%.1f,%.1f,%.1f,%.0f,%d,%lu\n", (unsigned long)millis(), teste, pino, n,
                l.bruto, l.mv, umidadePct(l.mv), alim, radioLigado ? 1 : 0, (unsigned long)l.durUs);
}

// Estatística simples impressa como comentário ao fim de cada teste.
struct Estat {
  double soma = 0, soma2 = 0;
  float mn = 1e9, mx = -1e9;
  int n = 0;
  void add(float v) { soma += v; soma2 += (double)v * v; mn = min(mn, v); mx = max(mx, v); n++; }
  void imprimir(const char* nome) const {
    if (n == 0) return;
    double media = soma / n;
    double var = n > 1 ? (soma2 - n * media * media) / (n - 1) : 0;
    Serial.printf("# %s: n=%d min=%.1f max=%.1f media=%.1f desvio=%.2f\n", nome, n, mn, mx, media,
                  sqrt(var > 0 ? var : 0));
  }
};

// ---------------------------------------------------------------- Testes
void teste1() {
  Serial.printf("# Teste 1: 2 Hz por 30 s, N=1, radio=%d\n", radioLigado);
  cabecalho();
  Estat eb, em, eb2;
  for (int i = 0; i < 60; i++) {
    uint32_t t0 = millis();
    float alim = alimentacaoMv();
    Leitura l = ler(config::PINO_SOLO, 1);
    linha("1", config::PINO_SOLO, 1, l, alim);
    eb.add(l.bruto);
    em.add(l.mv);
    if (segundoSensor) {
      Leitura l2 = ler(PINO_SOLO2, 1);
      linha("1", PINO_SOLO2, 1, l2, alim);
      eb2.add(l2.bruto);
    }
    esperarMs(500 - min<uint32_t>(500, millis() - t0));
  }
  eb.imprimir("bruto GPIO34");
  em.imprimir("mV GPIO34");
  if (segundoSensor) eb2.imprimir("bruto GPIO33");
  if (radioLigado) Serial.printf("# radio: %lu pacotes enviados, %lu erros\n", pacotesEnviados, errosEnvio);
}

// O sensor fica sempre no 3V3 (sem chaveamento), então o VCC é ligado à mão.
// Gatilho: a leitura passa de 300 mV; guarda 100 ms antes do gatilho.
void teste2() {
  constexpr int PRE = 10, POS = 200;  // 10 ms por amostra: 0,1 s antes e 2 s depois
  static Leitura buf[PRE + POS];
  static uint32_t tempos[PRE + POS];

  Serial.println("# Teste 2: DESCONECTE o fio VCC do sensor (aguardando leitura < 200 mV)");
  uint32_t ini = millis();
  while (ler(config::PINO_SOLO, 4).mv >= 200) {
    if (millis() - ini > 60000) { Serial.println("# Teste 2 cancelado: o sensor nao desligou em 60 s"); return; }
    delay(10);
  }
  Serial.println("# Sensor desligado. Agora RECONECTE o fio VCC (espero ate 60 s)");

  int i = 0;
  bool disparou = false;
  ini = millis();
  while (true) {
    uint32_t t = micros();
    Leitura l = ler(config::PINO_SOLO, 1);
    if (!disparou) {
      // buffer circular com as últimas PRE amostras
      for (int k = 0; k < PRE - 1; k++) { buf[k] = buf[k + 1]; tempos[k] = tempos[k + 1]; }
      buf[PRE - 1] = l;
      tempos[PRE - 1] = millis();
      if (i < PRE) i++;
      if (l.mv > 300) { disparou = true; i = PRE; }
      if (millis() - ini > 60000) { Serial.println("# Teste 2 cancelado: VCC nao foi reconectado"); return; }
    } else {
      buf[i] = l;
      tempos[i] = millis();
      if (++i >= PRE + POS) break;
    }
    while (micros() - t < 10000) {}
  }
  uint32_t t0 = tempos[PRE - 1];  // instante do gatilho
  Serial.println("t_rel_ms,teste,pino,n,bruto,mv,umidade_pct");
  for (int k = 0; k < PRE + POS; k++) {
    Serial.printf("%ld,2,%u,1,%.0f,%.0f,%.1f\n", (long)tempos[k] - (long)t0, config::PINO_SOLO, buf[k].bruto,
                  buf[k].mv, umidadePct(buf[k].mv));
  }
  Serial.println("# Teste 2 fim (t_rel_ms = 0 no gatilho de 300 mV)");
}

void teste3() {
  Serial.println("# Teste 3: N=1, 16 e 64 intercalados, 60 rodadas");
  cabecalho();
  const int ns[] = {1, 16, 64};
  Estat e[3], d[3];
  for (int r = 0; r < 60; r++) {
    for (int k = 0; k < 3; k++) {
      Leitura l = ler(config::PINO_SOLO, ns[k]);
      linha("3", config::PINO_SOLO, ns[k], l, NAN);
      e[k].add(l.mv);
      d[k].add(l.durUs);
    }
    esperarMs(200);
  }
  const char* nomes[] = {"mV N=1", "mV N=16", "mV N=64"};
  const char* nomesD[] = {"dur_us N=1", "dur_us N=16", "dur_us N=64"};
  for (int k = 0; k < 3; k++) { e[k].imprimir(nomes[k]); d[k].imprimir(nomesD[k]); }
}

void patamar() {
  Serial.printf("# Patamar: N=64, 1 Hz por 60 s, segundo sensor=%d\n", segundoSensor);
  cabecalho();
  Estat em, em2;
  for (int i = 0; i < 60; i++) {
    uint32_t t0 = millis();
    float alim = alimentacaoMv();
    Leitura l = ler(config::PINO_SOLO, config::AMOSTRAS_ADC);
    linha("p", config::PINO_SOLO, config::AMOSTRAS_ADC, l, alim);
    em.add(l.mv);
    if (segundoSensor) {
      Leitura l2 = ler(PINO_SOLO2, config::AMOSTRAS_ADC);
      linha("p", PINO_SOLO2, config::AMOSTRAS_ADC, l2, alim);
      em2.add(l2.mv);
    }
    esperarMs(1000 - min<uint32_t>(1000, millis() - t0));
  }
  em.imprimir("mV GPIO34");
  if (segundoSensor) em2.imprimir("mV GPIO33");
}

void pinosSoltos() {
  Serial.println("# Pinos ADC1: 50 leituras unicas cada (solto = media baixa e min-max largo)");
  for (uint8_t p : PINOS_ADC1) {
    Estat e;
    for (int i = 0; i < 50; i++) { e.add(analogReadMilliVolts(p)); delay(2); }
    char nome[16];
    snprintf(nome, sizeof(nome), "GPIO%u mV", p);
    e.imprimir(nome);
  }
}

void ajuda() {
  Serial.println("# diag_solo — comandos: 1 (2 Hz/30 s), 2 (estabilizacao), 3 (medias),");
  Serial.println("#   p (patamar 60 s), f (pinos soltos), r (radio on/off), s (2o sensor GPIO33), h (ajuda)");
  Serial.printf("# radio=%d segundo_sensor=%d | calibracao config.h: seco=%.0f mV umido=%.0f mV\n", radioLigado,
                segundoSensor, config::SOLO_MV_SECO, config::SOLO_MV_UMIDO);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  // Mesma configuração de src/sensores.cpp: 12 bits e atenuação global de 11 dB.
  analogReadResolution(12);
  analogSetAttenuation(config::ATENUACAO);
  WiFi.mode(WIFI_OFF);  // garante o rádio desligado por padrão
  Serial.printf("# diag_solo | Arduino-ESP32 %d.%d.%d | solo=GPIO%u alim=GPIO%u\n", ESP_ARDUINO_VERSION_MAJOR,
                ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH, config::PINO_SOLO, config::PINO_ALIMENTACAO);
  ajuda();
}

void loop() {
  manterRadio();
  if (!Serial.available()) { delay(5); return; }
  char c = Serial.read();
  switch (c) {
    case '1': teste1(); break;
    case '2': teste2(); break;
    case '3': teste3(); break;
    case 'p': patamar(); break;
    case 'f': pinosSoltos(); break;
    case 'r':
      radioLigado ? desligarRadio() : ligarRadio();
      Serial.printf("# radio=%d\n", radioLigado);
      break;
    case 's':
      segundoSensor = !segundoSensor;
      Serial.printf("# segundo_sensor=%d (GPIO%u)\n", segundoSensor, PINO_SOLO2);
      break;
    case 'g':
      gpioAlimLigado = !gpioAlimLigado;
      pinMode(PINO_GPIO_ALIM, OUTPUT);
      // Drive 3 = o mais forte (gpio_set_drive_capability, driver/gpio.h do ESP-IDF 5.5).
      gpio_set_drive_capability(PINO_GPIO_ALIM, GPIO_DRIVE_CAP_3);
      digitalWrite(PINO_GPIO_ALIM, gpioAlimLigado ? HIGH : LOW);
      Serial.printf("# GPIO25 (drive 3) = %s\n", gpioAlimLigado ? "ALTO (sensor ligado)" : "BAIXO");
      break;
    case 'h': ajuda(); break;
    default: break;  // ignora \r, \n e outros
  }
}
