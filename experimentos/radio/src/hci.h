// BLE só pelo controlador, sem pilha (Bluedroid/NimBLE): comandos HCI pela
// interface virtual (VHCI), como o exemplo oficial do ESP-IDF v5.5
// examples/bluetooth/hci/controller_vhci_ble_adv. Comandos e eventos HCI:
// Bluetooth Core Specification v4.2, Vol 2, Part E (o controlador do ESP32 é 4.2).
#pragma once

#include <cstddef>
#include <cstdint>

namespace hci {

// Opcodes = (OGF << 10) | OCF
constexpr uint16_t SET_EVENT_MASK = 0x0C01;
constexpr uint16_t RESET = 0x0C03;
constexpr uint16_t LE_SET_ADV_PARAM = 0x2006;
constexpr uint16_t LE_SET_ADV_DATA = 0x2008;
constexpr uint16_t LE_SET_ADV_ENABLE = 0x200A;
constexpr uint16_t LE_SET_SCAN_PARAM = 0x200B;
constexpr uint16_t LE_SET_SCAN_ENABLE = 0x200C;

// Chamado na tarefa do controlador para cada LE Advertising Report:
// dados AD do anúncio e RSSI. Não pode bloquear.
using Relatorio = void (*)(const uint8_t* dados, uint8_t tamanho, int8_t rssi);

// Libera a memória do Bluetooth clássico, inicia e liga o controlador em modo
// BLE, registra os callbacks e envia HCI_Reset.
bool iniciar(Relatorio relatorio = nullptr);

// Envia um comando e espera o Command Complete. Retorna o status HCI
// (0 = sucesso) ou 0xFF se não houve resposta no prazo.
uint8_t comando(uint16_t opcode, const uint8_t* parametros, uint8_t tamanho, uint32_t esperaMs = 500);

// esp_bt_controller_disable(): exigido antes do deep sleep (ESP-IDF v5.5, Sleep Modes).
void desligar();

}  // namespace hci
