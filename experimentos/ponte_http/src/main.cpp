// Ponte HTTP: a placa entra no Wi-Fi do coordenador (STA) e repete pela serial
// as requisições que o app faria. Uma linha na serial = uma requisição:
//   GET /api/v1/alertas
// Resposta:
//   ### status=200 ms=34 bytes=512
//   {...corpo...}
//   ### fim
// "ms" é o tempo no cliente: da abertura da conexão TCP ao fim do corpo
// (o servidor fecha a conexão a cada resposta, docs/api.md, seção 1).
//
// HTTPClient e WiFi do Arduino-ESP32 3.3.12:
//   https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html
//   (HTTPClient: libraries/HTTPClient no núcleo instalado)
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "segredos.h"  // do coordenador (build_flags -I): AP_SSID e AP_SENHA

namespace {

constexpr const char* BASE = "http://192.168.4.1";
char linha[200];
size_t n = 0;

void conectar() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(segredos::AP_SSID, segredos::AP_SENHA);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(100);
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("### wifi conectado: ip=%s rssi=%d em %lu ms\n", WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                  (unsigned long)(millis() - t0));
  else
    Serial.println("### wifi FALHOU (SSID/senha do segredos.h do coordenador?)");
}

void requisitar(const char* metodo, const char* caminho) {
  conectar();
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String(BASE) + caminho;
  uint32_t t0 = millis();
  http.begin(url);
  int codigo = strcmp(metodo, "GET") == 0 ? http.GET() : http.sendRequest(metodo);
  String corpo = codigo > 0 ? http.getString() : String();
  uint32_t ms = millis() - t0;
  http.end();
  Serial.printf("### status=%d ms=%lu bytes=%u url=%s\n", codigo, (unsigned long)ms, (unsigned)corpo.length(),
                caminho);
  Serial.println(corpo);
  Serial.println("### fim");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("### ponte_http pronta: envie \"GET /api/v1/status\"");
  conectar();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (n < sizeof(linha) - 1) linha[n++] = c;
      continue;
    }
    linha[n] = '\0';
    n = 0;
    char* espaco = strchr(linha, ' ');
    if (espaco == nullptr) continue;
    *espaco = '\0';
    requisitar(linha, espaco + 1);
  }
}
