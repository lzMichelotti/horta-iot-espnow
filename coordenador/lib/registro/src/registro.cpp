#include "registro.h"

#include <cstring>

namespace registro {

uint16_t crc16(const uint8_t* dados, size_t tamanho) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < tamanho; i++) {
    crc ^= static_cast<uint16_t>(dados[i]) << 8;
    for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : crc << 1;
  }
  return crc;
}

namespace {

constexpr size_t TAMANHO_SEM_CRC = offsetof(Registro, crc);

uint16_t crcDe(const Registro& r) { return crc16(reinterpret_cast<const uint8_t*>(&r), TAMANHO_SEM_CRC); }

}  // namespace

Registro montar(const Metadados& m, const protocolo::PacoteLeitura& p) {
  Registro r{};
  r.versao = VERSAO;

  bool valida = m.horaValida && m.utc_s >= 0 && m.utc_s <= static_cast<int64_t>(UINT32_MAX);
  r.flags = static_cast<uint8_t>((valida ? flag::HORA_VALIDA : 0) |
                                 ((m.fonteRelogio << flag::FONTE_DESLOCAMENTO) & flag::FONTE_MASCARA));
  r.boot_coord = m.bootCoord;
  r.utc_s = valida ? static_cast<uint32_t>(m.utc_s) : 0;

  uint64_t s = m.desdeBoot_ms / 1000;
  r.desde_boot_s = s > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(s);

  memcpy(r.mac, m.mac, sizeof(r.mac));
  r.rssi = m.rssi;
  r.ruido = m.ruido;
  r.classe = static_cast<uint8_t>(m.classe);
  r.reservado = 0;
  r.perdidos = m.perdidos > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(m.perdidos);
  memcpy(&r.pacote, &p, sizeof(r.pacote));
  r.crc = crcDe(r);
  return r;
}

const char* nomeErro(Erro e) {
  switch (e) {
    case Erro::NENHUM: return "nenhum";
    case Erro::TAMANHO_CURTO: return "tamanho_curto";
    case Erro::VERSAO_DESCONHECIDA: return "versao_desconhecida";
    case Erro::TAMANHO_ERRADO: return "tamanho_errado";
    case Erro::CRC: return "crc";
  }
  return "?";
}

Erro ler(const uint8_t* dados, size_t tamanho, Registro& saida) {
  if (dados == nullptr || tamanho < 1) return Erro::TAMANHO_CURTO;
  if (dados[0] != VERSAO) return Erro::VERSAO_DESCONHECIDA;
  if (tamanho != sizeof(Registro)) return Erro::TAMANHO_ERRADO;
  Registro r;
  memcpy(&r, dados, sizeof(r));
  if (crcDe(r) != r.crc) return Erro::CRC;
  saida = r;
  return Erro::NENHUM;
}

}  // namespace registro
