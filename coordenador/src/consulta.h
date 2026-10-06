// Consulta ao histórico (API interna; as rotas HTTP da etapa 8 usam esta API).
//
// Lê os segmentos em partes (buffer fixo de 16 registros), pula os segmentos
// fora do período pelo resumo em RAM e entrega um registro por vez a uma
// função de quem pediu (callback). Paginação sem estado no coordenador: cada
// página devolve um cursor (id global) para pedir a seguinte.
// Lógica pura (filtro, percurso, hora reconstruída) em lib/filtro.
// Roda na tarefa do loop(), a mesma que grava: consulta e gravação não se
// misturam no meio de um registro.
#pragma once

#include <Arduino.h>

#include <filtro.h>
#include <registro.h>

namespace consulta {

struct Item {
  uint64_t id;             // id global (segmento × K + posição): serve de cursor
  registro::Registro r;    // registro bruto (solo em mV)
  uint32_t utcEfetivo;     // r.utc_s, ou a hora reconstruída (0 se SEM_HORA)
  filtro::Hora hora;
};

struct Pagina {
  uint16_t entregues = 0;
  bool fim = false;              // não há mais registros no filtro (por enquanto)
  uint64_t proximoCursor = 0;    // para a página seguinte (crescente: também para buscar só os novos)
  bool cursorRotacionado = false;  // o cursor apontava para registros já apagados pela rotação
  uint32_t lidos = 0;            // registros lidos da flash
  uint32_t invalidos = 0;        // reprovados em registro::ler (CRC/versão)
  uint32_t segmentosPulados = 0;
  uint32_t duracaoUs = 0;
};

// Devolve false para interromper a consulta (ex.: cliente HTTP desconectou).
using Visitante = bool (*)(const Item& item, void* contexto);

Pagina executar(const filtro::Filtro& f, Visitante visitar, void* contexto);

// Observador do histórico (historico::iniciar): âncoras de hora e última
// leitura por nó, para cada registro gravado ou lido na carga do boot.
void observar(const registro::Registro& r);

// Última leitura gravada do nó; nullptr se nunca houve.
const registro::Registro* ultimaLeitura(const uint8_t mac[6]);

const filtro::Ancoras& ancoras();

// Comando "consulta" da serial (teste da API):
//   consulta [de=T] [ate=T] [no=MAC] [ordem=asc|desc] [cursor=N] [limite=N] [maxlidos=N] [mostrar=0|1]
//   consulta ultimas
// T em epoch ou AAAA-MM-DDTHH:MM:SSZ.
void comando(const char* argumento);

}  // namespace consulta
