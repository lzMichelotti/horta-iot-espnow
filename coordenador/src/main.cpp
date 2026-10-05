// Firmware do coordenador — Etapa 4: SoftAP em canal fixo, recepção ESP-NOW por
// fila, validação (comum/protocolo) e rastreamento por nó (perda, duplicata,
// reinício). Uma linha [CSV] por pacote e um [RESUMO] por nó a cada minuto.
// (RTC, LittleFS e HTTP: etapas 6 a 8.)
#include <Arduino.h>
#include <esp_mac.h>

#include <protocolo.h>

#include "config.h"
#include "nos.h"
#include "radio.h"

namespace {

void imprimirMac(const char* nome, esp_mac_type_t tipo) {
  uint8_t mac[6];
  esp_read_mac(mac, tipo);
  Serial.printf("[MAC] %s " MACSTR "\n", nome, MAC2STR(mac));
}

void processar(const radio::Recebido& r) {
  nos::No* n = nos::buscar(r.mac);
  if (n == nullptr) {
    Serial.printf("[ESPNOW] tabela de nos cheia, pacote de " MACSTR " ignorado\n", MAC2STR(r.mac));
    return;
  }
  nos::contarRecepcao(*n, r.rssi);

  protocolo::PacoteLeitura p{};
  protocolo::Rejeicao rej = protocolo::validar(r.dados, r.tamanho, p);
  Serial.printf("[CSV] %lu," MACSTR ",%s,%d,%d,%u,%s", millis(), MAC2STR(r.mac), n->nome, r.rssi, r.ruido,
                r.tamanhoOriginal, protocolo::nomeRejeicao(rej));
  if (rej != protocolo::Rejeicao::NENHUMA) {
    n->rejeitados++;
    Serial.println(",,,,,,,,,,,,,");  // 13 campos vazios: classe, perdidos e os 11 do pacote
    return;
  }

  protocolo::Classe c = n->sequencia.registrar(p.boot, p.seq);
  if (p.flags & protocolo::flag::ANTERIOR_SEM_ACK) n->semAck++;
  Serial.printf(",%s,%lu,%u,%lu,%d,%u,%u,%u,%u,%u,%u,%u,%u\n", protocolo::nomeClasse(c),
                (unsigned long)n->sequencia.ultimaLacuna(), p.boot, (unsigned long)p.seq, p.temperatura_c100,
                p.umidade_ar_c100, p.solo_mv, p.alimentacao_mv, p.estados, p.motivo_boot, p.acordado_ant_ms,
                p.tentativas_ant, p.flags);
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
  Serial.println("[CSV] t_ms,mac,nome,rssi,ruido,len,validacao,classe,perdidos,boot,seq,temp_c100,ur_c100,"
                 "solo_mv,alim_mv,estados,motivo_boot,acordado_ant_ms,tent_ant,flags");
  Serial.println("[RESUMO] t_ms,mac,nome,recebidos,rejeitados,aceitos,perdidos,descartados,reinicios,sem_ack,"
                 "entrega_pct,rssi_medio,rssi_min,rssi_max");
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

  static uint32_t proximoResumo = config::INTERVALO_RESUMO_MS;
  if ((int32_t)(millis() - proximoResumo) >= 0) {
    proximoResumo += config::INTERVALO_RESUMO_MS;
    nos::imprimirResumo();
  }
}
