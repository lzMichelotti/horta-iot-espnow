// Configuração do coordenador: Wi-Fi, ESP-NOW, fila de recepção, relógio e console.
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

// ---------------------------------------------------------------- Relógio
// Faixa plausível para o comando "hora" (UTC, segundos desde 1970).
// Antes de 2026 só pode ser erro de digitação; 2100 fica abaixo do limite de
// um uint32 (2106), caso o registro guarde a hora em 32 bits.
constexpr int64_t HORA_MINIMA_UTC = 1767225600;  // 2026-01-01T00:00:00Z
constexpr int64_t HORA_MAXIMA_UTC = 4102444800;  // 2100-01-01T00:00:00Z

// ---------------------------------------------------------------- Console (serial)
constexpr size_t CONSOLE_LINHA_MAX = 96;  // caracteres por comando

// ---------------------------------------------------------------- Nós
// O nó é identificado pelo MAC de origem do quadro ESP-NOW, que é o MAC STA
// do nó (docs/placas.md). MAC fora desta lista: rastreado como "desconhecido".
struct NoCadastrado {
  uint8_t mac[6];
  const char* nome;
};
constexpr NoCadastrado NOS[] = {
    {{0x88, 0x57, 0x21, 0x70, 0x93, 0x70}, "no1"},  // NÓ 1 (serial 5AC9001351)
};
constexpr size_t MAX_NOS = 8;                  // tabela fixa, sem alocação dinâmica
constexpr uint32_t INTERVALO_RESUMO_MS = 60000;  // linha [RESUMO] por nó

}  // namespace config
