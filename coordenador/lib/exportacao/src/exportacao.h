// Exportação do histórico em CSV (Etapa 7, passo 10): formatação de uma linha
// a partir de um registro e CRC-32 do conteúdo para conferir a transferência.
// C++ puro: compila no ESP32 e no PC (testes nativos). Quem envia pela serial
// é src/exportar.cpp; quem recebe é ferramentas/exportar_historico.py.
#pragma once

#include <cstddef>
#include <cstdint>

#include <filtro.h>
#include <registro.h>

namespace exportacao {

// CRC-32 do zlib (polinômio refletido 0xEDB88320, valor inicial e XOR final
// 0xFFFFFFFF), incremental: crc = crc32(crc, parte, n), começando de 0.
// Igual a zlib.crc32 do Python; valor de verificação de "123456789": 0xCBF43926.
uint32_t crc32(uint32_t crc, const uint8_t* dados, size_t tamanho);

// Linha de cabeçalho do CSV (sem '\n').
const char* cabecalho();

constexpr size_t TAM_LINHA = 256;

// Uma linha do CSV (sem '\n'), na ordem do cabeçalho:
// - hora: valida/reconstruida/sem_hora; utc em ISO 8601 (vazio sem hora);
// - temperatura e umidade em unidades (2 casas), solo e alimentação em mV;
// - valor com estado erro (ou reservado) sai como célula vazia;
// - solo_pct vazio se temPct == false (sem calibração ou solo sem estado ok).
// Devolve o tamanho da linha.
size_t formatarLinha(char (&saida)[TAM_LINHA], uint64_t id, const registro::Registro& r, const char* nome,
                     filtro::Hora hora, uint32_t utcEfetivo, bool temPct, int16_t pctDecimos);

}  // namespace exportacao
