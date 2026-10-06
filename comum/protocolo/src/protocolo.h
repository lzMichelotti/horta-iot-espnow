// Protocolo de aplicação nó → coordenador (horta-iot-espnow).
// Especificação campo a campo, regras de validação e de sequência: docs/protocolo.md.
//
// C++ puro (sem Arduino.h): compila no ESP32 (firmwares) e no PC (testes nativos).
//
// Regras de uso do struct packed:
//  - nunca pegar ponteiro para um campo (ex.: &p.seq): o endereço pode ser
//    desalinhado e, no ESP32, uma leitura de 16/32 bits desalinhada gera a
//    exceção LoadStoreAlignment (ESP-IDF, "Fatal Errors");
//  - para ler um pacote recebido, copiar os bytes com memcpy para uma
//    variável PacoteLeitura local (é o que validar() faz).
#pragma once

#include <cstddef>
#include <cstdint>

namespace protocolo {

// ---------------------------------------------------------------- Identificação
constexpr uint8_t VERSAO = 1;

enum class Tipo : uint8_t {
  LEITURA = 1,  // leituras de um ciclo do nó
};

// ---------------------------------------------------------------- Estados
// 2 bits por grandeza no campo `estados`.
enum class Estado : uint8_t {
  OK = 0,
  ERRO = 1,           // valor ausente: o campo vai como 0 e deve ser ignorado
  FORA_DE_FAIXA = 2,  // valor medido, mas fora da faixa válida no nó
  RESERVADO = 3,      // inválido nesta versão
};

// Posição de cada grandeza no campo `estados` (bits 2i+1..2i).
enum class Grandeza : uint8_t {
  TEMPERATURA = 0,
  UMIDADE_AR = 1,
  SOLO = 2,
  ALIMENTACAO = 3,
};
constexpr uint8_t NUM_GRANDEZAS = 4;

// ---------------------------------------------------------------- Flags
namespace flag {
constexpr uint8_t AHT20_NOVA_TENTATIVA = 1u << 0;  // o driver repetiu a consulta de estado
constexpr uint8_t ANTERIOR_SEM_ACK = 1u << 1;      // o pacote anterior esgotou as tentativas sem ACK
                                                   // (desfaz a ambiguidade de tentativas_ant = máximo)
constexpr uint8_t CICLO_ANTERIOR_ABORTADO = 1u << 2;  // o prazo máximo acordado interrompeu o ciclo anterior
// bits 3–7: reservados (o receptor ignora; o nó envia 0)
}  // namespace flag

// ---------------------------------------------------------------- Pacote
// Todos os campos multibyte são little-endian (ordem nativa do ESP32 e do PC).
struct __attribute__((packed)) PacoteLeitura {
  uint8_t versao;            //  0  VERSAO
  uint8_t tipo;              //  1  Tipo::LEITURA
  uint16_t boot;             //  2  contador de boots do nó (NVS), dá a volta em 65535
  uint32_t seq;              //  4  nº de sequência, zera a cada boot (RTC_DATA_ATTR)
  int16_t temperatura_c100;  //  8  temperatura do ar, 0,01 °C
  uint16_t umidade_ar_c100;  // 10  umidade relativa do ar, 0,01 %
  uint16_t solo_mv;          // 12  saída do sensor de solo, mV (o % é calculado no coordenador)
  uint16_t alimentacao_mv;   // 14  tensão de alimentação (entrada do divisor), mV
  uint8_t estados;           // 16  2 bits por grandeza (Estado), ordem de Grandeza
  uint8_t motivo_boot;       // 17  esp_reset_reason() do boot atual
  uint16_t acordado_ant_ms;  // 18  tempo acordado no ciclo anterior, ms (0 = desconhecido)
  uint8_t tentativas_ant;    // 20  envios do pacote anterior (0 = desconhecido)
  uint8_t flags;             // 21  ver namespace flag
};

// Travas de layout: se alguém mudar um campo sem atualizar a especificação
// (e a VERSAO), a compilação falha nos dois firmwares e nos testes.
static_assert(sizeof(PacoteLeitura) == 22, "PacoteLeitura deve ter 22 bytes");
static_assert(offsetof(PacoteLeitura, boot) == 2, "offset de boot");
static_assert(offsetof(PacoteLeitura, seq) == 4, "offset de seq");
static_assert(offsetof(PacoteLeitura, temperatura_c100) == 8, "offset de temperatura");
static_assert(offsetof(PacoteLeitura, umidade_ar_c100) == 10, "offset de umidade_ar");
static_assert(offsetof(PacoteLeitura, solo_mv) == 12, "offset de solo_mv");
static_assert(offsetof(PacoteLeitura, alimentacao_mv) == 14, "offset de alimentacao_mv");
static_assert(offsetof(PacoteLeitura, estados) == 16, "offset de estados");
static_assert(offsetof(PacoteLeitura, motivo_boot) == 17, "offset de motivo_boot");
static_assert(offsetof(PacoteLeitura, acordado_ant_ms) == 18, "offset de acordado_ant_ms");
static_assert(offsetof(PacoteLeitura, tentativas_ant) == 20, "offset de tentativas_ant");
static_assert(offsetof(PacoteLeitura, flags) == 21, "offset de flags");
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "o protocolo assume plataforma little-endian (ESP32 e PC x86-64)");

// Carga útil máxima do ESP-NOW v1.0 (ESP_NOW_MAX_DATA_LEN), ESP-IDF v5.5.
constexpr size_t ESPNOW_MAX_CARGA = 250;
static_assert(sizeof(PacoteLeitura) <= ESPNOW_MAX_CARGA, "pacote maior que a carga do ESP-NOW v1");

// ---------------------------------------------------------------- Faixas válidas
// Mesmas fontes do firmware do nó (docs/sensores.md, seção 6).
constexpr int16_t TEMPERATURA_MIN_C100 = -4000;  // −40,00 °C (AHT20, tab. 2)
constexpr int16_t TEMPERATURA_MAX_C100 = 8500;   //  85,00 °C
constexpr uint16_t UMIDADE_AR_MAX_C100 = 10000;  // 100,00 % (AHT20, tab. 1)
constexpr uint16_t SOLO_MAX_MV = 3300;           // saída do ADC limitada a ~3,1 V; folga até 3,3 V
constexpr uint16_t ALIMENTACAO_MAX_MV = 6000;    // acima do máximo medível com o divisor 1/2 (~4,9 V)

// ---------------------------------------------------------------- Estados (bits)
constexpr Estado lerEstado(uint8_t estados, Grandeza g) {
  return static_cast<Estado>((estados >> (2 * static_cast<uint8_t>(g))) & 0x3);
}

constexpr uint8_t gravarEstado(uint8_t estados, Grandeza g, Estado e) {
  return static_cast<uint8_t>((estados & ~(0x3 << (2 * static_cast<uint8_t>(g)))) |
                              (static_cast<uint8_t>(e) << (2 * static_cast<uint8_t>(g))));
}

// ---------------------------------------------------------------- Montagem (nó)
// Leituras de um ciclo, nas unidades naturais (como saem de sensores::lerTodas).
struct DadosLeitura {
  uint16_t boot;
  uint32_t seq;
  float temperaturaC;
  Estado estadoTemperatura;
  float umidadeArPct;
  Estado estadoUmidadeAr;
  float soloMv;
  Estado estadoSolo;
  float alimentacaoMv;
  Estado estadoAlimentacao;
  uint8_t motivoBoot;
  uint16_t acordadoAntMs;
  uint8_t tentativasAnt;
  uint8_t flags;
};

// Escala cada valor para inteiro (arredondando) e o grava no pacote.
// - Estado ERRO ou valor NaN: o campo vai como 0 e o estado como ERRO.
// - Valor fora da faixa válida com estado OK: o estado passa a FORA_DE_FAIXA.
// - Valor além do que o tipo comporta: é saturado no limite do tipo.
// - Estado RESERVADO na entrada é tratado como ERRO.
PacoteLeitura montarLeitura(const DadosLeitura& d);

// ---------------------------------------------------------------- Validação (coordenador)
enum class Rejeicao : uint8_t {
  NENHUMA = 0,          // pacote válido
  TAMANHO_CURTO,        // menos de 2 bytes: não dá nem para ler versão e tipo
  VERSAO_DESCONHECIDA,  // versao != VERSAO
  TIPO_DESCONHECIDO,    // tipo != LEITURA
  TAMANHO_ERRADO,       // versão e tipo conhecidos, mas tamanho != sizeof(PacoteLeitura)
  ESTADO_RESERVADO,     // algum estado com o valor 3
  TEMPERATURA_FORA,     // estado OK com valor fora da faixa válida
  UMIDADE_AR_FORA,
  SOLO_FORA,
  ALIMENTACAO_FORA,
};

const char* nomeRejeicao(Rejeicao r);

// Valida os bytes recebidos e, se válidos, copia-os (memcpy) para `saida`.
// Ordem das verificações: tamanho mínimo, versão, tipo, tamanho exato,
// estados, faixas. Versão e tipo vêm antes do tamanho exato para que uma
// versão futura, com outro tamanho, seja registrada como versão desconhecida.
Rejeicao validar(const uint8_t* dados, size_t tamanho, PacoteLeitura& saida);

// ---------------------------------------------------------------- Sequência (coordenador)
// Classificação de um pacote válido pelo par (boot, seq), um rastreador por nó (MAC).
enum class Classe : uint8_t {
  PRIMEIRO,   // primeiro pacote do nó desde que o coordenador ligou
  NOVO,       // mesmo boot, seq posterior ao último (pode haver perdas no meio)
  DUPLICADO,  // mesmo boot e mesmo seq do último aceito: descartar
  ANTIGO,     // mesmo boot e seq anterior ao último: descartar
  REINICIO,   // boot diferente do último: o nó reiniciou (seq recomeça de 0)
};

const char* nomeClasse(Classe c);

constexpr bool aceitar(Classe c) {
  return c == Classe::PRIMEIRO || c == Classe::NOVO || c == Classe::REINICIO;
}

class RastreadorSequencia {
 public:
  // Classifica e atualiza os contadores. A comparação de seq usa aritmética
  // modular (RFC 1982): depois de 0xFFFFFFFF vem 0, sem confundir com reinício.
  Classe registrar(uint16_t boot, uint32_t seq);

  uint32_t aceitos() const { return aceitos_; }
  uint32_t perdidos() const { return perdidos_; }    // lacunas de seq dentro de um boot
  uint32_t descartados() const { return descartados_; }  // duplicados + antigos
  uint32_t reinicios() const { return reinicios_; }
  uint32_t ultimaLacuna() const { return ultimaLacuna_; }  // perdidos no último registro

 private:
  bool iniciado_ = false;
  uint16_t boot_ = 0;
  uint32_t seq_ = 0;
  uint32_t aceitos_ = 0;
  uint32_t perdidos_ = 0;
  uint32_t descartados_ = 0;
  uint32_t reinicios_ = 0;
  uint32_t ultimaLacuna_ = 0;
};

}  // namespace protocolo
