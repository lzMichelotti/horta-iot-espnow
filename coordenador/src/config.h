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

// ---------------------------------------------------------------- Histórico (LittleFS)
// Organização, retenção e capacidade: docs/persistencia.md.
constexpr const char* FS_ROTULO = "littlefs";  // rótulo da partição em particoes.csv
constexpr const char* FS_PONTO = "/littlefs";  // ponto de montagem no VFS
constexpr const char* HIST_DIR = "/h";
// 500 registros × 48 B = 24 000 B: cabem em 6 blocos de 4 KB com os ponteiros
// da lista CTZ do LittleFS (32 B). 56 segmentos ≈ 75 % da partição de 1920 KB;
// o resto é folga do LittleFS. Com 2 nós a cada 15 min: ~146 dias de histórico.
// O env teste_rotacao reduz os dois valores para forçar a rotação na bancada.
#ifdef HIST_TESTE_REGISTROS_POR_SEGMENTO
constexpr uint32_t HIST_REGISTROS_POR_SEGMENTO = HIST_TESTE_REGISTROS_POR_SEGMENTO;
constexpr uint32_t HIST_MAX_SEGMENTOS = HIST_TESTE_MAX_SEGMENTOS;
#else
constexpr uint32_t HIST_REGISTROS_POR_SEGMENTO = 500;
constexpr uint32_t HIST_MAX_SEGMENTOS = 56;
#endif
// Rotação de emergência (passo 9): antes de abrir um segmento novo, sobra pelo
// menos esta margem livre (2 segmentos de 6 blocos de 4 KB). Sai da folga de
// ~29 % que a rotação normal já deixa: não reduz a retenção.
constexpr size_t HIST_MARGEM_LIVRE = 2 * 6 * 4096;
// Gravações seguidas com erro até o estado virar FALHAS_DE_GRAVACAO.
constexpr uint32_t HIST_FALHAS_ALERTA = 3;

// ---------------------------------------------------------------- Console (serial)
constexpr size_t CONSOLE_LINHA_MAX = 160;  // caracteres por comando (a consulta usa ~110)

// ---------------------------------------------------------------- Nós
// O nó é identificado pelo MAC de origem do quadro ESP-NOW, que é o MAC STA
// do nó (docs/placas.md). MAC fora do cadastro: rastreado como "desconhecido".
//
// SEMENTE do cadastro (Etapa 7, passo 8): usada só quando a NVS ainda não tem
// a tabela (primeiro boot) ou no comando "no padrao". Depois disso vale o que
// estiver na NVS, editado pelos comandos "no" da serial (src/configuracao.cpp).
struct NoCadastrado {
  uint8_t mac[6];
  const char* nome;
  uint16_t mvSeco;   // calibração do solo, mV (0 = sem calibração)
  uint16_t mvUmido;
};
constexpr NoCadastrado NOS[] = {
    // NÓ 1 (serial 5AC9001351). Calibração de BANCADA (docs/sensores.md, 4.3):
    // seco ao toque 1400 mV, ~1 h após a rega 655 mV. Recalibrar no campo.
    {{0x88, 0x57, 0x21, 0x70, 0x93, 0x70}, "no1", 1400, 655},
};
constexpr size_t MAX_NOS = 8;                  // tabela fixa, sem alocação dinâmica
static_assert(MAX_NOS == 8, "o cadastro (lib/cadastro) guarda até 8 nós");
constexpr uint32_t INTERVALO_RESUMO_MS = 60000;  // linha [RESUMO] por nó

}  // namespace config
