// Configuração do coordenador: Wi-Fi, ESP-NOW e fila de recepção.
// Justificativas em docs/comunicacao.md. SSID e senha ficam em segredos.h (fora do Git).
#pragma once

#include <cstddef>
#include <cstdint>

namespace config {

// ---------------------------------------------------------------- Wi-Fi / SoftAP
// Canal fixo do SoftAP; o ESP-NOW transmite e recebe no canal da interface,
// então os nós usam o MESMO valor (no_sensor/src/config.h).
// 1, 6 e 11 são os canais de 2,4 GHz que não se sobrepõem.
constexpr uint8_t CANAL_WIFI = 1;
constexpr uint8_t AP_MAX_CONEXOES = 4;   // celulares do app (etapa 8)

// Varredura de redes no boot, antes de subir o AP: registra a ocupação de cada
// canal no local (dado para justificar o canal escolhido). Leva alguns segundos.
constexpr bool VARRER_CANAIS_NO_BOOT = true;

// ---------------------------------------------------------------- ESP-NOW
// Itens da fila entre o callback de recepção (tarefa do Wi-Fi) e o loop().
constexpr size_t FILA_ESPNOW_ITENS = 16;

}  // namespace config
