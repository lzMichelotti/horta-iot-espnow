// Exportação do histórico em CSV pela serial (Etapa 7, passo 10), para a
// avaliação usar os dados antes de existir o HTTP. Quem recebe e confere é
// ferramentas/exportar_historico.py.
//
// Em páginas: cada chamada de passo() (uma por volta do loop) envia até
// PAGINA registros e volta, para a fila do ESP-NOW continuar sendo esvaziada.
// Protocolo na serial:
//   [EXPORT] inicio                       começo
//   [EXP] <linha do CSV>                  cabeçalho e uma linha por registro
//   [EXPORT] fim linhas=N crc32=XXXXXXXX ms=T
//            N = linhas de dados (sem o cabeçalho); CRC-32 (zlib) de todas as
//            linhas do CSV, cabeçalho incluído, cada uma seguida de '\n'
// Outras linhas de log ([CSV], [FS] ...) podem aparecer entre as páginas.
#pragma once

#include <Arduino.h>

namespace exportar {

// Comando "exportar" da serial:
//   exportar [de=T] [ate=T] [no=MAC] [cursor=N]   inicia (ordem crescente)
//   exportar parar                                interrompe
void comando(const char* argumento);

// Envia a próxima página, se houver exportação em andamento.
void passo();

bool ativa();

}  // namespace exportar
