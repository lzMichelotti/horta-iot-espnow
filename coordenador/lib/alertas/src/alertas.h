// Regras locais e alertas do coordenador (Etapa 9; docs/alertas.md).
//
// Lógica pura (sem Arduino): compila no ESP32 e no PC (testes nativos). Quem
// alimenta é src/regras.cpp, com cada registro gravado (e, na carga do boot,
// com cada registro do histórico, em ordem: os alertas se reconstroem sem
// nenhuma gravação a mais na flash).
//
// Estados: inativo → (N leituras seguidas na condição) → ativo → (condição de
// resolver, com banda de histerese) → resolvido. Um alerta resolvido guarda
// início e fim até ser ativado de novo.
#pragma once

#include <cstddef>
#include <cstdint>

#include <protocolo.h>

namespace alertas {

enum class Tipo : uint8_t {
  SOLO_SECO,
  SOLO_ENCHARCADO,
  TEMPERATURA_ALTA,
  TEMPERATURA_BAIXA,
  BATERIA_BAIXA,     // o nó está em economia ou crítico (decidido no nó, protocolo seção 14)
  BATERIA_CRITICA,   // o nó entrou no crítico: último pacote antes de silenciar
  SEM_COMUNICAR,     // nenhuma leitura há mais que o limite (por tempo, não por leitura)
  ERRO_AR,           // AHT20: temperatura ou umidade do ar em erro ou fora de faixa
  ERRO_SOLO,         // sensor de solo em erro ou fora de faixa
  ERRO_ALIMENTACAO,  // divisor em erro ou fora de faixa (só com Limiares::erroAlimentacao)
};
constexpr size_t NUM_TIPOS = 10;

const char* nomeTipo(Tipo t);

// Unidade de valor e limiar (o JSON converte: décimos → 1 casa, centésimos → 2).
enum class Unidade : uint8_t { PCT_DECIMOS, C_CENTESIMOS, MV, SEGUNDOS, NENHUMA };
Unidade unidade(Tipo t);
const char* nomeUnidade(Unidade u);  // "pct", "c", "mv", "s" ou nullptr

// ---------------------------------------------------------------- Solo
// Classe a partir do % SEM limite (cadastro::umidadeSoloDecimosSemLimite), com a
// calibração do próprio nó: abaixo de seco → SECO; acima de muito úmido →
// MUITO_UMIDO; entre os dois → UMIDO. São as 3 classes que o app já exibe
// (repositório Hortas_Urbanas, frontend/src/features/coordenador/tipos.ts).
enum class ClasseSolo : uint8_t { DESCONHECIDA, SECO, UMIDO, MUITO_UMIDO };
const char* nomeClasseSolo(ClasseSolo c);  // nullptr para DESCONHECIDA

struct Limiares {
  int32_t soloSecoDecimos;        // abaixo: classe seco; alerta solo_seco
  int32_t soloMuitoUmidoDecimos;  // acima: classe muito_umido
  int32_t soloEncharcadoDecimos;  // acima: alerta solo_encharcado
  int32_t soloBandaDecimos;       // histerese dos alertas do solo
  int32_t temperaturaAltaC100;
  int32_t temperaturaBaixaC100;
  int32_t temperaturaBandaC100;
  uint8_t leiturasParaAtivar;     // solo seco e temperaturas
  uint8_t leiturasEncharcado;     // encharcado (a rega deixa o solo acima de 100 % por um tempo)
  uint8_t errosParaAtivar;        // erro de sensor (resolve com 1 leitura ok)
  bool erroAlimentacao;           // regra erro_alimentacao ligada
};

ClasseSolo classificarSolo(bool temPct, int32_t decimos, const Limiares& l);

// ---------------------------------------------------------------- Estado
// Onde e quando, o bastante para calcular a hora da leitura como em /leituras
// (recepção − idade, ou hora reconstruída; docs/api.md, 2.2).
struct Ref {
  uint32_t desdeBoot_s = 0;
  uint32_t utcRecepcao_s = 0;  // 0 = sem hora
  uint32_t idade_s = 0;
  uint16_t bootCoord = 0;
  uint8_t flagsRegistro = 0;   // registro::flag (hora válida, fonte do relógio)
  bool definida = false;
};

struct EstadoAlerta {
  bool ativo = false;
  bool resolvido = false;  // foi ativo e resolveu: fim vale
  uint8_t seguidas = 0;    // leituras seguidas na condição de ativar
  bool temValor = false;
  int32_t valor = 0;       // último valor medido (na unidade do tipo)
  Ref candidato;           // primeira leitura da sequência que pode ativar
  Ref inicio;              // leitura que começou o alerta ativo
  Ref fim;                 // leitura que o resolveu
};

// Uma leitura gravada, já decodificada.
struct Leitura {
  Ref ref;
  uint8_t estados = 0;     // protocolo: 2 bits por grandeza
  int16_t temperaturaC100 = 0;
  uint16_t alimentacaoMv = 0;
  bool temPct = false;     // o nó tem calibração do solo
  int32_t soloDecimos = 0; // % sem limite, em décimos (vale com temPct)
  protocolo::ModoBateria modo = protocolo::ModoBateria::DESCONHECIDO;
};

struct No {
  bool usado = false;
  uint8_t mac[6] = {};
  EstadoAlerta estado[NUM_TIPOS];
  Ref ultima;                     // última leitura recebida (base de sem_comunicar)
  protocolo::ModoBateria modo = protocolo::ModoBateria::DESCONHECIDO;
  ClasseSolo classe = ClasseSolo::DESCONHECIDA;
};

// Limite do sem_comunicar, em s: `ciclos` períodos do nó no modo da última
// leitura dele. O período é o mesmo com que o nó dorme (protocolo::periodoMs,
// comum/protocolo): economia espera período × fator, sem alarme falso.
constexpr uint32_t limiteSemComunicarS(protocolo::ModoBateria m, uint32_t ciclos) {
  return protocolo::periodoMs(m) / 1000 * ciclos;
}

enum class Mudanca : uint8_t { ATIVOU, RESOLVEU };
using Aviso = void (*)(const No& n, Tipo t, Mudanca m, void* contexto);

constexpr size_t MAX_NOS = 8;  // = config::MAX_NOS do coordenador (static_assert em src/regras.cpp)

class Tabela {
 public:
  // Nó pelo MAC; com criar, ocupa uma posição livre. nullptr se não achar (ou tabela cheia).
  No* buscar(const uint8_t mac[6], bool criar);
  const No* encontrar(const uint8_t mac[6]) const;
  const No& posicao(size_t i) const { return nos_[i]; }

  // Aplica as regras de leitura (todas menos sem_comunicar, que esta leitura resolve).
  void avaliar(No& n, const Leitura& l, const Limiares& lim, Aviso aviso = nullptr, void* contexto = nullptr);

  // sem_comunicar: haS = segundos desde a última leitura; ativa acima de limiteS
  // (início = a última leitura). Atualiza o valor mesmo sem mudar de estado.
  void verificarSilencio(No& n, uint32_t haS, uint32_t limiteS, Aviso aviso = nullptr, void* contexto = nullptr);

  size_t ativos() const;
  static size_t ativos(const No& n);
  void limpar();

 private:
  No nos_[MAX_NOS];
};

}  // namespace alertas
