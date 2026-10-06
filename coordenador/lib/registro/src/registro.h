// Registro do histórico no coordenador (LittleFS): 48 bytes de tamanho fixo,
// metadados do coordenador + o PacoteLeitura recebido, sem alterações.
// Especificação campo a campo: docs/persistencia.md.
//
// C++ puro (sem Arduino.h): compila no ESP32 e no PC (testes nativos).
// Mesmas regras do struct packed de comum/protocolo: não pegar ponteiro para
// campo; para ler bytes da flash, usar ler(), que copia com memcpy.
#pragma once

#include <cstddef>
#include <cstdint>

#include <protocolo.h>

namespace registro {

constexpr uint8_t VERSAO = 1;

// ---------------------------------------------------------------- Flags
namespace flag {
constexpr uint8_t HORA_VALIDA = 1u << 0;
constexpr uint8_t FONTE_DESLOCAMENTO = 1;               // bits 1–2: fonte do relógio
constexpr uint8_t FONTE_MASCARA = 0x3u << FONTE_DESLOCAMENTO;  // (valores de relogio::Fonte)
// bits 3–7: reservados (gravados como 0; ignorados na leitura)
}  // namespace flag

// ---------------------------------------------------------------- Registro
// Little-endian, como o pacote. Campos naturalmente alinhados: o seq do pacote
// cai no offset 28 (múltiplo de 4) e todos os uint16 em offsets pares.
struct __attribute__((packed)) Registro {
  uint8_t versao;                  //  0  VERSAO
  uint8_t flags;                   //  1  ver namespace flag
  uint16_t boot_coord;             //  2  contador de boots do coordenador (NVS)
  uint32_t utc_s;                  //  4  hora UTC, s desde 1970; 0 se a hora não é válida
  uint32_t desde_boot_s;           //  8  tempo desde o boot do coordenador, s
  uint8_t mac[6];                  // 12  MAC de origem do nó
  int8_t rssi;                     // 18  dBm
  int8_t ruido;                    // 19  piso de ruído, dBm
  uint8_t classe;                  // 20  protocolo::Classe (só as aceitas são gravadas)
  uint8_t reservado;               // 21  0
  uint16_t perdidos;               // 22  lacuna de seq antes deste pacote (satura em 65535)
  protocolo::PacoteLeitura pacote;  // 24  os 22 bytes recebidos, sem alterações
  uint16_t crc;                    // 46  CRC-16/CCITT-FALSE dos bytes 0–45
};

static_assert(sizeof(Registro) == 48, "Registro deve ter 48 bytes");
static_assert(offsetof(Registro, flags) == 1, "offset de flags");
static_assert(offsetof(Registro, boot_coord) == 2, "offset de boot_coord");
static_assert(offsetof(Registro, utc_s) == 4, "offset de utc_s");
static_assert(offsetof(Registro, desde_boot_s) == 8, "offset de desde_boot_s");
static_assert(offsetof(Registro, mac) == 12, "offset de mac");
static_assert(offsetof(Registro, rssi) == 18, "offset de rssi");
static_assert(offsetof(Registro, ruido) == 19, "offset de ruido");
static_assert(offsetof(Registro, classe) == 20, "offset de classe");
static_assert(offsetof(Registro, reservado) == 21, "offset de reservado");
static_assert(offsetof(Registro, perdidos) == 22, "offset de perdidos");
static_assert(offsetof(Registro, pacote) == 24, "offset de pacote");
static_assert(offsetof(Registro, crc) == 46, "offset de crc");

// ---------------------------------------------------------------- CRC
// CRC-16/CCITT-FALSE: polinômio 0x1021, valor inicial 0xFFFF, sem reflexão e
// sem XOR final. Valor de verificação de "123456789": 0x29B1. No Python:
// binascii.crc_hqx(dados, 0xFFFF).
uint16_t crc16(const uint8_t* dados, size_t tamanho);

// ---------------------------------------------------------------- Montagem
// O que o coordenador sabe no momento da recepção (fora do pacote).
struct Metadados {
  bool horaValida;
  uint8_t fonteRelogio;  // relogio::Fonte (0–3)
  uint16_t bootCoord;
  int64_t utc_s;
  uint64_t desdeBoot_ms;
  uint8_t mac[6];
  int8_t rssi;
  int8_t ruido;
  protocolo::Classe classe;
  uint32_t perdidos;
};

// Preenche o registro e calcula o CRC.
// - Hora inválida ou fora de 0–UINT32_MAX: utc_s = 0 e o bit HORA_VALIDA zerado.
// - desdeBoot_ms vira segundos (arredonda para baixo); perdidos satura em 65535.
Registro montar(const Metadados& m, const protocolo::PacoteLeitura& p);

// ---------------------------------------------------------------- Leitura
enum class Erro : uint8_t {
  NENHUM = 0,
  TAMANHO_CURTO,        // não dá nem para ler a versão
  VERSAO_DESCONHECIDA,  // versao != VERSAO
  TAMANHO_ERRADO,       // versão conhecida, mas tamanho != sizeof(Registro)
  CRC,                  // conteúdo corrompido
};

const char* nomeErro(Erro e);

// Confere versão, tamanho e CRC e, se tudo estiver certo, copia (memcpy) para
// `saida`. A versão vem antes do tamanho para que um registro de versão futura,
// com outro tamanho, seja identificado como tal.
Erro ler(const uint8_t* dados, size_t tamanho, Registro& saida);

// ---------------------------------------------------------------- Acesso
inline bool horaValida(const Registro& r) { return (r.flags & flag::HORA_VALIDA) != 0; }
inline uint8_t fonteRelogio(const Registro& r) {
  return static_cast<uint8_t>((r.flags & flag::FONTE_MASCARA) >> flag::FONTE_DESLOCAMENTO);
}

}  // namespace registro
