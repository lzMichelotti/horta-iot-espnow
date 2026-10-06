// Teste de bancada das estratégias de gravação (Etapa 7, passo 6).
// Só compila no env teste_gravacao (-D TESTE_GRAVACAO); não entra no firmware
// de produção. Usa arquivos próprios (/bench.seg, /estresse.seg), fora de /h.
#pragma once

#ifdef TESTE_GRAVACAO

namespace teste_gravacao {

// Comando "bench" da serial:
//   bench E1 N        abrir + anexar + fechar a cada registro
//   bench E2 N        arquivo aberto; escrever + flush a cada registro
//   bench E3 N L      arquivo aberto; flush a cada L registros
//   bench E4 N L      L registros em RAM; abrir + anexar L + fechar
//   bench estresse on|off   tarefa que grava sem parar (E1), para medir o efeito na recepção
// Uma linha [BENCH] estrategia,i,us por registro e um resumo no fim.
void comando(const char* argumento);

}  // namespace teste_gravacao

#endif
