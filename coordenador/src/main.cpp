// Firmware do coordenador — Etapa 4, passo 2: SoftAP em canal fixo e recepção
// ESP-NOW. Cada pacote é validado com comum/protocolo e impresso numa linha.
// (Rastreamento por nó: passo 4. RTC, LittleFS e HTTP: etapas 6 a 8.)
#include <Arduino.h>
#include <esp_mac.h>

#include <protocolo.h>

#include "radio.h"

namespace {

void imprimirMac(const char* nome, esp_mac_type_t tipo) {
  uint8_t mac[6];
  esp_read_mac(mac, tipo);
  Serial.printf("[MAC] %s " MACSTR "\n", nome, MAC2STR(mac));
}

void processar(const radio::Recebido& r) {
  protocolo::PacoteLeitura p;
  protocolo::Rejeicao rej = protocolo::validar(r.dados, r.tamanho, p);
  Serial.printf("[ESPNOW] " MACSTR " rssi=%d ruido=%d canal=%u len=%u validacao=%s", MAC2STR(r.mac), r.rssi,
                r.ruido, r.canal, r.tamanhoOriginal, protocolo::nomeRejeicao(rej));
  if (rej == protocolo::Rejeicao::NENHUMA)
    Serial.printf(" boot=%u seq=%lu T=%d UR=%u solo=%u alim=%u estados=0x%02X tent_ant=%u", p.boot,
                  (unsigned long)p.seq, p.temperatura_c100, p.umidade_ar_c100, p.solo_mv, p.alimentacao_mv,
                  p.estados, p.tentativas_ant);
  Serial.println();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[BOOT] coordenador - etapa 4");
  imprimirMac("STA", ESP_MAC_WIFI_STA);
  imprimirMac("AP ", ESP_MAC_WIFI_SOFTAP);
  Serial.printf("[BOOT] protocolo v%u (%u bytes)\n", protocolo::VERSAO, (unsigned)sizeof(protocolo::PacoteLeitura));
  if (!radio::iniciar()) Serial.println("[BOOT] ERRO: radio nao iniciou");
}

void loop() {
  radio::Recebido r;
  if (radio::receber(r, 100)) processar(r);

  static uint32_t descartadosAntes = 0;
  uint32_t d = radio::descartadosFilaCheia();
  if (d != descartadosAntes) {
    Serial.printf("[ESPNOW] fila cheia: %lu pacotes descartados no total\n", (unsigned long)d);
    descartadosAntes = d;
  }
}
