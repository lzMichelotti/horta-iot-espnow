#include "hci.h"

#include <Arduino.h>
#include <esp_bt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// O núcleo Arduino libera a memória do controlador BLE no boot se nenhuma
// biblioteca BLE estiver em uso (esp32-hal-bt.c / esp32-hal-misc.c).
extern "C" bool bleInUse(void) { return true; }

namespace hci {

namespace {

SemaphoreHandle_t respondeu = nullptr;
volatile uint16_t opcodeEsperado = 0;
volatile uint8_t statusRecebido = 0xFF;
Relatorio aoRelatar = nullptr;

void prontoParaEnviar() {}

// Eventos HCI (pacote H4 tipo 0x04): [0x04][código][tamanho][parâmetros...]
int aoReceber(uint8_t* d, uint16_t n) {
  if (n < 3 || d[0] != 0x04) return 0;
  uint8_t codigo = d[1];
  if (codigo == 0x0E && n >= 7) {  // Command Complete: num, opcode(2), status
    uint16_t op = d[4] | (d[5] << 8);
    if (op == opcodeEsperado) {
      statusRecebido = d[6];
      xSemaphoreGive(respondeu);
    }
  } else if (codigo == 0x0F && n >= 7) {  // Command Status: status, num, opcode(2)
    uint16_t op = d[5] | (d[6] << 8);
    if (op == opcodeEsperado) {
      statusRecebido = d[3];
      xSemaphoreGive(respondeu);
    }
  } else if (codigo == 0x3E && n >= 5 && d[3] == 0x02 && aoRelatar != nullptr) {
    // LE Advertising Report: num; por relatório: tipo, tipo_end, end(6), tam, dados, rssi
    uint8_t num = d[4];
    size_t i = 5;
    for (uint8_t k = 0; k < num; k++) {
      if (i + 9 > n) break;
      uint8_t tam = d[i + 8];
      if (i + 9 + tam + 1 > n) break;
      aoRelatar(&d[i + 9], tam, static_cast<int8_t>(d[i + 9 + tam]));
      i += 10 + tam;
    }
  }
  return 0;
}

esp_vhci_host_callback_t callbacks = {prontoParaEnviar, aoReceber};

}  // namespace

bool iniciar(Relatorio relatorio) {
  aoRelatar = relatorio;
  if (respondeu == nullptr) respondeu = xSemaphoreCreateBinary();
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  // O núcleo Arduino compila o controlador em modo BTDM (sdkconfig); o exemplo
  // oficial usa um sdkconfig só BLE. Com a memória do clássico liberada, o
  // modo da configuração precisa ser BLE (senão: ESP_ERR_INVALID_ARG).
  cfg.mode = ESP_BT_MODE_BLE;
  esp_err_t err = esp_bt_controller_init(&cfg);
  if (err == ESP_OK) err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (err == ESP_OK) err = esp_vhci_host_register_callback(&callbacks);
  if (err != ESP_OK) {
    Serial.printf("[BLE] falha ao iniciar o controlador: %s\n", esp_err_to_name(err));
    return false;
  }
  if (comando(RESET, nullptr, 0) != 0) return false;
  // Após o reset, a máscara padrão não inclui o LE Meta Event (bit 61), que
  // traz os LE Advertising Reports (Core 4.2, Vol 2, Part E, 7.3.1).
  const uint8_t mascara[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1F, 0x00, 0x20};
  return comando(SET_EVENT_MASK, mascara, sizeof(mascara)) == 0;
}

uint8_t comando(uint16_t opcode, const uint8_t* parametros, uint8_t tamanho, uint32_t esperaMs) {
  uint8_t buf[4 + 40];
  if (tamanho > 40) return 0xFF;
  buf[0] = 0x01;  // H4: comando
  buf[1] = opcode & 0xFF;
  buf[2] = opcode >> 8;
  buf[3] = tamanho;
  if (tamanho) memcpy(&buf[4], parametros, tamanho);

  uint32_t t0 = millis();
  while (!esp_vhci_host_check_send_available()) {
    if (millis() - t0 > esperaMs) return 0xFF;
    vTaskDelay(1);
  }
  xSemaphoreTake(respondeu, 0);
  opcodeEsperado = opcode;
  statusRecebido = 0xFF;
  esp_vhci_host_send_packet(buf, 4 + tamanho);
  if (xSemaphoreTake(respondeu, pdMS_TO_TICKS(esperaMs)) != pdTRUE) return 0xFF;
  return statusRecebido;
}

void desligar() { esp_bt_controller_disable(); }

}  // namespace hci
