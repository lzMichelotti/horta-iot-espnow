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
// v1: PacoteLeitura de 22 B sem autenticação.
// v2: os mesmos 22 B (versao = 2) + assinatura HMAC-SHA256 de 16 B (PacoteAssinado).
// v3: + idade_s (26 B, assinado 42 B), bit SEM_ANTERIORES e a confirmação
//     coordenador → nó (Confirmacao). Etapa 7.1, docs/protocolo.md seção 13.
constexpr uint8_t VERSAO = 3;

enum class Tipo : uint8_t {
  LEITURA = 1,      // leituras de um ciclo do nó (nó → coordenador)
  CONFIRMACAO = 2,  // "tudo até (boot, seq) está gravado" (coordenador → nó)
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
// v3: o nó não tem nenhuma leitura pendente anterior a esta (é a mais antiga da
// fila). Calculado no envio. Só com ele o coordenador aceita um buraco de seq.
constexpr uint8_t SEM_ANTERIORES = 1u << 3;
// bits 4–7: reservados (o receptor ignora; o nó envia 0)
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
  uint32_t idade_s;          // 22  v3: s entre a leitura e este envio (0 = enviada no ciclo em que foi lida).
                             //     Fora do alinhamento natural: ler só por memcpy (regras no topo).
};

// Bytes do pacote antes da idade: o que a fila do nó guarda (a idade e o bit
// SEM_ANTERIORES são escritos no envio).
constexpr size_t TAMANHO_SEM_IDADE = 22;

// Travas de layout: se alguém mudar um campo sem atualizar a especificação
// (e a VERSAO), a compilação falha nos dois firmwares e nos testes.
static_assert(sizeof(PacoteLeitura) == 26, "PacoteLeitura deve ter 26 bytes");
static_assert(offsetof(PacoteLeitura, idade_s) == TAMANHO_SEM_IDADE, "offset de idade_s");
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

// ---------------------------------------------------------------- Autenticação (v2)
// HMAC-SHA256 (RFC 2104) sobre os 22 B do PacoteLeitura, truncado em 16 B.
// RFC 2104, seção 5: truncar para não menos que metade da saída do hash e não
// menos que 80 bits. Seção 3: chave de pelo menos L bytes (32 no SHA-256).
constexpr size_t TAMANHO_HMAC = 32;        // saída do HMAC-SHA256
constexpr size_t TAMANHO_ASSINATURA = 16;  // metade de TAMANHO_HMAC
constexpr size_t TAMANHO_CHAVE = 32;
constexpr size_t TAMANHO_MAC = 6;

// HMAC-SHA256 fornecido pela plataforma: no ESP32, hmacSha256 (mbedTLS, abaixo);
// nos testes no PC, uma função de teste. Devolve false em caso de erro.
using FuncaoHmac = bool (*)(const uint8_t* chave, size_t tamanhoChave, const uint8_t* mensagem,
                            size_t tamanhoMensagem, uint8_t saida[TAMANHO_HMAC]);

#if defined(ESP_PLATFORM)
// mbedtls_md_hmac com SHA-256 (mbedTLS do ESP-IDF; o SHA usa o acelerador de
// hardware do ESP32, CONFIG_MBEDTLS_HARDWARE_SHA). Implementado em hmac_esp32.cpp.
bool hmacSha256(const uint8_t* chave, size_t tamanhoChave, const uint8_t* mensagem, size_t tamanhoMensagem,
                uint8_t saida[TAMANHO_HMAC]);
#endif

// O que vai no ar a partir da v2: o pacote e, logo depois, a assinatura.
struct __attribute__((packed)) PacoteAssinado {
  PacoteLeitura leitura;                    //  0  versao = 3
  uint8_t assinatura[TAMANHO_ASSINATURA];   // 26  HMAC-SHA256(chave do nó, bytes 0–25)[0..15]
};
static_assert(sizeof(PacoteAssinado) == 42, "PacoteAssinado deve ter 42 bytes");
static_assert(offsetof(PacoteAssinado, assinatura) == 26, "offset da assinatura");

// Chave de cada nó derivada da chave mestra do coordenador (diversificação de
// chaves): chaveNo = HMAC-SHA256(chaveMestra, MAC STA do nó). O coordenador
// guarda só a mestra e calcula a chave de qualquer nó pelo MAC de origem; cada
// nó guarda só a sua (quem extrair a chave de um nó não falsifica os outros).
bool derivarChaveNo(const uint8_t chaveMestra[TAMANHO_CHAVE], const uint8_t mac[TAMANHO_MAC], FuncaoHmac hmac,
                    uint8_t chaveNo[TAMANHO_CHAVE]);

// Assina o pacote (nó). Devolve false se o HMAC falhar.
bool assinar(const PacoteLeitura& p, const uint8_t chave[TAMANHO_CHAVE], FuncaoHmac hmac, PacoteAssinado& saida);

// ---------------------------------------------------------------- Confirmação (v3, coordenador → nó)
// "Tudo até (boot, seq) deste nó está gravado no histórico" (confirmação
// cumulativa). Enviada em resposta a todo pacote autêntico, ANTES de gravá-lo:
// por isso confirma o que já estava gravado, não o pacote que a provocou.
// Assinada com a MESMA chave do nó: o campo tipo (dentro da assinatura) separa
// leitura de confirmação, e um pacote refletido de volta não passa.
namespace flag_conf {
constexpr uint8_t NADA_GRAVADO = 1u << 0;  // nenhum registro deste nó: boot e seq não valem
// bits 1–7: reservados
}  // namespace flag_conf

struct __attribute__((packed)) Confirmacao {
  uint8_t versao;  //  0  VERSAO
  uint8_t tipo;    //  1  Tipo::CONFIRMACAO
  uint16_t boot;   //  2  último (boot, seq) gravado deste nó
  uint32_t seq;    //  4
  uint8_t flags;   //  8  flag_conf
};
static_assert(sizeof(Confirmacao) == 9, "Confirmacao deve ter 9 bytes");

struct __attribute__((packed)) ConfirmacaoAssinada {
  Confirmacao conf;                        //  0
  uint8_t assinatura[TAMANHO_ASSINATURA];  //  9  HMAC-SHA256(chave do nó, bytes 0–8)[0..15]
};
static_assert(sizeof(ConfirmacaoAssinada) == 25, "ConfirmacaoAssinada deve ter 25 bytes");

bool assinarConfirmacao(const Confirmacao& c, const uint8_t chave[TAMANHO_CHAVE], FuncaoHmac hmac,
                        ConfirmacaoAssinada& saida);

enum class RejeicaoConfirmacao : uint8_t {
  NENHUMA = 0,
  TAMANHO_ERRADO,       // != sizeof(ConfirmacaoAssinada)
  VERSAO_DESCONHECIDA,
  TIPO_DESCONHECIDO,    // não é CONFIRMACAO (ex.: um pacote de leitura refletido)
  ASSINATURA_INVALIDA,
  ALEM_DO_GERADO,       // confirma um (boot, seq) que o nó ainda não gerou: recusada
};

const char* nomeRejeicaoConfirmacao(RejeicaoConfirmacao r);

// Valida os bytes recebidos pelo nó. (ultimoBoot, ultimoSeq) = último par que o
// nó gerou: ninguém pode confirmar o que ele ainda não enviou (protege, por
// exemplo, contra uma confirmação antiga repetida depois de a NVS do nó ser
// apagada). Com NADA_GRAVADO, o par não é conferido.
RejeicaoConfirmacao validarConfirmacao(const uint8_t* dados, size_t tamanho, const uint8_t chave[TAMANHO_CHAVE],
                                       FuncaoHmac hmac, uint16_t ultimoBoot, uint32_t ultimoSeq,
                                       Confirmacao& saida);

// Compara dois pares (boot, seq) na ordem em que o nó os gera, com aritmética
// modular (RFC 1982): boot em 16 bits e, no mesmo boot, seq em 32 bits.
// Negativo: a vem antes de b; 0: iguais; positivo: a vem depois.
int compararPar(uint16_t bootA, uint32_t seqA, uint16_t bootB, uint32_t seqB);

// Carga útil máxima do ESP-NOW v1.0 (ESP_NOW_MAX_DATA_LEN), ESP-IDF v5.5.
constexpr size_t ESPNOW_MAX_CARGA = 250;
static_assert(sizeof(PacoteAssinado) <= ESPNOW_MAX_CARGA, "pacote maior que a carga do ESP-NOW v1");

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
  TAMANHO_ERRADO,       // versão e tipo conhecidos, mas tamanho != sizeof(PacoteAssinado)
  ESTADO_RESERVADO,     // algum estado com o valor 3
  TEMPERATURA_FORA,     // estado OK com valor fora da faixa válida
  UMIDADE_AR_FORA,
  SOLO_FORA,
  ALIMENTACAO_FORA,
  ASSINATURA_INVALIDA,  // HMAC não confere (chave errada, pacote alterado ou forjado)
};

const char* nomeRejeicao(Rejeicao r);

// Valida os bytes recebidos (PacoteAssinado) e, se válidos, copia a leitura
// (memcpy) para `saida`. `chave` é a chave do nó de origem (derivarChaveNo).
// Ordem das verificações: tamanho mínimo, versão, tipo, tamanho exato,
// assinatura, estados, faixas. Versão e tipo vêm antes do tamanho exato para
// que uma versão futura, com outro tamanho, seja registrada como versão
// desconhecida; a assinatura vem antes do conteúdo para que nada de um pacote
// não autenticado seja interpretado.
Rejeicao validar(const uint8_t* dados, size_t tamanho, const uint8_t chave[TAMANHO_CHAVE], FuncaoHmac hmac,
                 PacoteLeitura& saida);

// ---------------------------------------------------------------- Sequência (coordenador)
// Classificação de um pacote válido pelo par (boot, seq), um rastreador por nó (MAC).
// v3 (Etapa 7.1): a "borda" é o último par GRAVADO do nó, e só o próximo é
// aceito; um buraco só é aceito se o nó declarar SEM_ANTERIORES (as leituras do
// meio se perderam no próprio nó). Assim o histórico de cada nó fica contínuo e
// a confirmação cumulativa "tudo até a borda" é sempre verdadeira.
enum class Classe : uint8_t {
  PRIMEIRO,       // sem borda (coordenador sem histórico do nó) e SEM_ANTERIORES
  NOVO,           // mesmo boot, seq seguinte (ou com buraco declarado por SEM_ANTERIORES)
  DUPLICADO,      // mesmo boot e mesmo seq da borda: já gravado
  ANTIGO,         // mesmo boot e seq anterior à borda: já gravado
  REINICIO,       // boot posterior à borda, com SEM_ANTERIORES: o nó reiniciou
  BOOT_ANTIGO,    // boot anterior à borda: repetição de um pacote antigo, ou a NVS do
                  // nó foi apagada (liberar com esquecer())
  FORA_DE_ORDEM,  // à frente da borda com buraco e sem SEM_ANTERIORES (ou sem borda e sem
                  // SEM_ANTERIORES): o nó ainda tem leituras anteriores; recusar
};

const char* nomeClasse(Classe c);

// Classes que são gravadas (e, depois de gravadas, avançam a borda).
constexpr bool aceitar(Classe c) {
  return c == Classe::PRIMEIRO || c == Classe::NOVO || c == Classe::REINICIO;
}

class RastreadorSequencia {
 public:
  // Classifica sem mover a borda. `semAnteriores` = bit SEM_ANTERIORES do
  // pacote. `lacuna` recebe o número de seq perdidos antes deste (só para as
  // classes aceitas). Conta as recusas. As comparações de seq (32 bits) e de
  // boot (16 bits) usam aritmética modular (RFC 1982).
  Classe classificar(uint16_t boot, uint32_t seq, bool semAnteriores, uint32_t& lacuna);

  // Depois que o registro foi gravado: move a borda para (boot, seq) e conta.
  // Se a gravação falhar, não chamar: a borda fica, e o reenvio é aceito.
  void confirmar(uint16_t boot, uint32_t seq, Classe c, uint32_t lacuna);

  // Retoma a borda (ex.: último registro do nó lido do histórico no boot do
  // coordenador). Os contadores não mudam.
  void restaurar(uint16_t boot, uint32_t seq);

  // Esquece o nó (ex.: NVS do nó apagada): fica sem borda até um pacote com SEM_ANTERIORES.
  void esquecer() { iniciado_ = false; }

  bool temBorda() const { return iniciado_; }
  uint16_t bordaBoot() const { return boot_; }
  uint32_t bordaSeq() const { return seq_; }

  uint32_t aceitos() const { return aceitos_; }
  uint32_t perdidos() const { return perdidos_; }        // lacunas declaradas pelo nó (fila cheia, reinício)
  uint32_t descartados() const { return descartados_; }  // duplicados + antigos + boots antigos
  uint32_t foraDeOrdem() const { return foraDeOrdem_; }
  uint32_t bootsAntigos() const { return bootsAntigos_; }
  uint32_t reinicios() const { return reinicios_; }

 private:
  bool iniciado_ = false;
  uint16_t boot_ = 0;
  uint32_t seq_ = 0;
  uint32_t aceitos_ = 0;
  uint32_t perdidos_ = 0;
  uint32_t descartados_ = 0;
  uint32_t foraDeOrdem_ = 0;
  uint32_t bootsAntigos_ = 0;
  uint32_t reinicios_ = 0;
};

}  // namespace protocolo
