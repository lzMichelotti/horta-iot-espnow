#include "protocolo.h"

#include <cmath>
#include <cstring>

namespace protocolo {

namespace {

// Escala `valor` por `escala`, arredonda e devolve o inteiro já limitado ao tipo
// [minTipo, maxTipo]. Ajusta `estado` conforme as regras de montarLeitura().
long escalar(float valor, float escala, long minValido, long maxValido, long minTipo, long maxTipo,
             Estado& estado) {
  if (estado == Estado::RESERVADO) estado = Estado::ERRO;
  if (estado == Estado::ERRO || std::isnan(valor)) {
    estado = Estado::ERRO;
    return 0;
  }
  // Arredonda para o inteiro mais próximo (meio para longe do zero) e verifica a
  // faixa ANTES de saturar: −1 % saturado em 0 continua fora de faixa.
  double arredondado = std::round(static_cast<double>(valor) * escala);
  if (estado == Estado::OK && (arredondado < minValido || arredondado > maxValido))
    estado = Estado::FORA_DE_FAIXA;
  if (arredondado <= minTipo) return minTipo;  // também cobre −infinito
  if (arredondado >= maxTipo) return maxTipo;  // também cobre +infinito
  return static_cast<long>(arredondado);
}

bool foraComEstadoOk(uint8_t estados, Grandeza g, long valor, long minimo, long maximo) {
  return lerEstado(estados, g) == Estado::OK && (valor < minimo || valor > maximo);
}

// Compara sem sair no primeiro byte diferente: o tempo não revela quantos bytes
// da assinatura um atacante acertou.
bool iguaisTempoConstante(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t diferenca = 0;
  for (size_t i = 0; i < n; i++) diferenca |= static_cast<uint8_t>(a[i] ^ b[i]);
  return diferenca == 0;
}

}  // namespace

bool derivarChaveNo(const uint8_t chaveMestra[TAMANHO_CHAVE], const uint8_t mac[TAMANHO_MAC], FuncaoHmac hmac,
                    uint8_t chaveNo[TAMANHO_CHAVE]) {
  static_assert(TAMANHO_CHAVE == TAMANHO_HMAC, "a chave derivada é a saída inteira do HMAC");
  if (hmac == nullptr) return false;
  return hmac(chaveMestra, TAMANHO_CHAVE, mac, TAMANHO_MAC, chaveNo);
}

bool assinar(const PacoteLeitura& p, const uint8_t chave[TAMANHO_CHAVE], FuncaoHmac hmac, PacoteAssinado& saida) {
  if (hmac == nullptr) return false;
  uint8_t bytes[sizeof(PacoteLeitura)];
  std::memcpy(bytes, &p, sizeof(bytes));
  uint8_t completo[TAMANHO_HMAC];
  if (!hmac(chave, TAMANHO_CHAVE, bytes, sizeof(bytes), completo)) return false;
  std::memcpy(&saida.leitura, bytes, sizeof(bytes));
  std::memcpy(saida.assinatura, completo, TAMANHO_ASSINATURA);
  return true;
}

PacoteLeitura montarLeitura(const DadosLeitura& d) {
  PacoteLeitura p{};
  p.versao = VERSAO;
  p.tipo = static_cast<uint8_t>(Tipo::LEITURA);
  p.boot = d.boot;
  p.seq = d.seq;

  Estado eT = d.estadoTemperatura, eU = d.estadoUmidadeAr, eS = d.estadoSolo, eA = d.estadoAlimentacao;
  p.temperatura_c100 = static_cast<int16_t>(
      escalar(d.temperaturaC, 100.0f, TEMPERATURA_MIN_C100, TEMPERATURA_MAX_C100, INT16_MIN, INT16_MAX, eT));
  p.umidade_ar_c100 = static_cast<uint16_t>(
      escalar(d.umidadeArPct, 100.0f, 0, UMIDADE_AR_MAX_C100, 0, UINT16_MAX, eU));
  p.solo_mv = static_cast<uint16_t>(escalar(d.soloMv, 1.0f, 0, SOLO_MAX_MV, 0, UINT16_MAX, eS));
  p.alimentacao_mv =
      static_cast<uint16_t>(escalar(d.alimentacaoMv, 1.0f, 0, ALIMENTACAO_MAX_MV, 0, UINT16_MAX, eA));

  uint8_t estados = 0;
  estados = gravarEstado(estados, Grandeza::TEMPERATURA, eT);
  estados = gravarEstado(estados, Grandeza::UMIDADE_AR, eU);
  estados = gravarEstado(estados, Grandeza::SOLO, eS);
  estados = gravarEstado(estados, Grandeza::ALIMENTACAO, eA);
  p.estados = estados;

  p.motivo_boot = d.motivoBoot;
  p.acordado_ant_ms = d.acordadoAntMs;
  p.tentativas_ant = d.tentativasAnt;
  p.flags = d.flags;
  return p;
}

const char* nomeRejeicao(Rejeicao r) {
  switch (r) {
    case Rejeicao::NENHUMA:             return "nenhuma";
    case Rejeicao::TAMANHO_CURTO:       return "tamanho_curto";
    case Rejeicao::VERSAO_DESCONHECIDA: return "versao_desconhecida";
    case Rejeicao::TIPO_DESCONHECIDO:   return "tipo_desconhecido";
    case Rejeicao::TAMANHO_ERRADO:      return "tamanho_errado";
    case Rejeicao::ESTADO_RESERVADO:    return "estado_reservado";
    case Rejeicao::TEMPERATURA_FORA:    return "temperatura_fora";
    case Rejeicao::UMIDADE_AR_FORA:     return "umidade_ar_fora";
    case Rejeicao::SOLO_FORA:           return "solo_fora";
    case Rejeicao::ALIMENTACAO_FORA:    return "alimentacao_fora";
    case Rejeicao::ASSINATURA_INVALIDA: return "assinatura_invalida";
  }
  return "?";
}

Rejeicao validar(const uint8_t* dados, size_t tamanho, const uint8_t chave[TAMANHO_CHAVE], FuncaoHmac hmac,
                 PacoteLeitura& saida) {
  if (dados == nullptr || tamanho < 2) return Rejeicao::TAMANHO_CURTO;
  if (dados[0] != VERSAO) return Rejeicao::VERSAO_DESCONHECIDA;
  if (dados[1] != static_cast<uint8_t>(Tipo::LEITURA)) return Rejeicao::TIPO_DESCONHECIDO;
  if (tamanho != sizeof(PacoteAssinado)) return Rejeicao::TAMANHO_ERRADO;

  uint8_t esperado[TAMANHO_HMAC];
  if (chave == nullptr || hmac == nullptr || !hmac(chave, TAMANHO_CHAVE, dados, sizeof(PacoteLeitura), esperado) ||
      !iguaisTempoConstante(esperado, dados + sizeof(PacoteLeitura), TAMANHO_ASSINATURA))
    return Rejeicao::ASSINATURA_INVALIDA;

  PacoteLeitura p;
  std::memcpy(&p, dados, sizeof(p));  // cópia para variável local: sem acesso desalinhado

  for (uint8_t i = 0; i < NUM_GRANDEZAS; i++)
    if (lerEstado(p.estados, static_cast<Grandeza>(i)) == Estado::RESERVADO) return Rejeicao::ESTADO_RESERVADO;

  if (foraComEstadoOk(p.estados, Grandeza::TEMPERATURA, p.temperatura_c100, TEMPERATURA_MIN_C100,
                      TEMPERATURA_MAX_C100))
    return Rejeicao::TEMPERATURA_FORA;
  if (foraComEstadoOk(p.estados, Grandeza::UMIDADE_AR, p.umidade_ar_c100, 0, UMIDADE_AR_MAX_C100))
    return Rejeicao::UMIDADE_AR_FORA;
  if (foraComEstadoOk(p.estados, Grandeza::SOLO, p.solo_mv, 0, SOLO_MAX_MV)) return Rejeicao::SOLO_FORA;
  if (foraComEstadoOk(p.estados, Grandeza::ALIMENTACAO, p.alimentacao_mv, 0, ALIMENTACAO_MAX_MV))
    return Rejeicao::ALIMENTACAO_FORA;

  saida = p;
  return Rejeicao::NENHUMA;
}

// ---------------------------------------------------------------- Confirmação
bool assinarConfirmacao(const Confirmacao& c, const uint8_t chave[TAMANHO_CHAVE], FuncaoHmac hmac,
                        ConfirmacaoAssinada& saida) {
  if (hmac == nullptr) return false;
  uint8_t bytes[sizeof(Confirmacao)];
  std::memcpy(bytes, &c, sizeof(bytes));
  uint8_t completo[TAMANHO_HMAC];
  if (!hmac(chave, TAMANHO_CHAVE, bytes, sizeof(bytes), completo)) return false;
  std::memcpy(&saida.conf, bytes, sizeof(bytes));
  std::memcpy(saida.assinatura, completo, TAMANHO_ASSINATURA);
  return true;
}

const char* nomeRejeicaoConfirmacao(RejeicaoConfirmacao r) {
  switch (r) {
    case RejeicaoConfirmacao::NENHUMA:             return "nenhuma";
    case RejeicaoConfirmacao::TAMANHO_ERRADO:      return "tamanho_errado";
    case RejeicaoConfirmacao::VERSAO_DESCONHECIDA: return "versao_desconhecida";
    case RejeicaoConfirmacao::TIPO_DESCONHECIDO:   return "tipo_desconhecido";
    case RejeicaoConfirmacao::ASSINATURA_INVALIDA: return "assinatura_invalida";
    case RejeicaoConfirmacao::ALEM_DO_GERADO:      return "alem_do_gerado";
  }
  return "?";
}

RejeicaoConfirmacao validarConfirmacao(const uint8_t* dados, size_t tamanho, const uint8_t chave[TAMANHO_CHAVE],
                                       FuncaoHmac hmac, uint16_t ultimoBoot, uint32_t ultimoSeq,
                                       Confirmacao& saida) {
  if (dados == nullptr || tamanho != sizeof(ConfirmacaoAssinada)) return RejeicaoConfirmacao::TAMANHO_ERRADO;
  if (dados[0] != VERSAO) return RejeicaoConfirmacao::VERSAO_DESCONHECIDA;
  if (dados[1] != static_cast<uint8_t>(Tipo::CONFIRMACAO)) return RejeicaoConfirmacao::TIPO_DESCONHECIDO;

  uint8_t esperado[TAMANHO_HMAC];
  if (chave == nullptr || hmac == nullptr || !hmac(chave, TAMANHO_CHAVE, dados, sizeof(Confirmacao), esperado) ||
      !iguaisTempoConstante(esperado, dados + sizeof(Confirmacao), TAMANHO_ASSINATURA))
    return RejeicaoConfirmacao::ASSINATURA_INVALIDA;

  Confirmacao c;
  std::memcpy(&c, dados, sizeof(c));
  if (!(c.flags & flag_conf::NADA_GRAVADO) && compararPar(c.boot, c.seq, ultimoBoot, ultimoSeq) > 0)
    return RejeicaoConfirmacao::ALEM_DO_GERADO;
  saida = c;
  return RejeicaoConfirmacao::NENHUMA;
}

int compararPar(uint16_t bootA, uint32_t seqA, uint16_t bootB, uint32_t seqB) {
  if (bootA != bootB) return static_cast<uint16_t>(bootA - bootB) < 0x8000u ? 1 : -1;
  if (seqA == seqB) return 0;
  return static_cast<uint32_t>(seqA - seqB) < 0x80000000u ? 1 : -1;
}

// ---------------------------------------------------------------- Sequência
const char* nomeClasse(Classe c) {
  switch (c) {
    case Classe::PRIMEIRO:      return "primeiro";
    case Classe::NOVO:          return "novo";
    case Classe::DUPLICADO:     return "duplicado";
    case Classe::ANTIGO:        return "antigo";
    case Classe::REINICIO:      return "reinicio";
    case Classe::BOOT_ANTIGO:   return "boot_antigo";
    case Classe::FORA_DE_ORDEM: return "fora_de_ordem";
  }
  return "?";
}

void RastreadorSequencia::restaurar(uint16_t boot, uint32_t seq) {
  iniciado_ = true;
  boot_ = boot;
  seq_ = seq;
}

Classe RastreadorSequencia::classificar(uint16_t boot, uint32_t seq, bool semAnteriores, uint32_t& lacuna) {
  lacuna = 0;
  if (!iniciado_) {
    // Sem borda (histórico do nó desconhecido): só aceita a leitura mais antiga
    // que o nó tem; senão as anteriores, ainda na fila do nó, seriam recusadas depois.
    if (semAnteriores) return Classe::PRIMEIRO;
    foraDeOrdem_++;
    return Classe::FORA_DE_ORDEM;
  }
  if (boot != boot_) {
    // O contador de boots do nó só avança (NVS). Um boot "atrás" da borda, na
    // metade do círculo de 16 bits, é um pacote antigo repetido (com assinatura
    // válida, mas capturado antes) ou a NVS do nó foi apagada: descartar até
    // alguém chamar esquecer().
    if (static_cast<uint16_t>(boot - boot_) >= 0x8000u) {
      descartados_++;
      bootsAntigos_++;
      return Classe::BOOT_ANTIGO;
    }
    // Boot novo: só se o nó não tem mais nada do boot anterior. Os seq
    // anteriores a este no boot novo se perderam no nó; perdas no fim do boot
    // anterior não são detectáveis.
    if (!semAnteriores) {
      foraDeOrdem_++;
      return Classe::FORA_DE_ORDEM;
    }
    lacuna = seq;
    return Classe::REINICIO;
  }
  uint32_t diferenca = seq - seq_;  // aritmética modular de 32 bits
  if (diferenca == 0) {
    descartados_++;
    return Classe::DUPLICADO;
  }
  if (diferenca >= 0x80000000u) {  // "atrás" da borda na metade do círculo
    descartados_++;
    return Classe::ANTIGO;
  }
  if (diferenca > 1 && !semAnteriores) {
    foraDeOrdem_++;
    return Classe::FORA_DE_ORDEM;
  }
  lacuna = diferenca - 1;
  return Classe::NOVO;
}

void RastreadorSequencia::confirmar(uint16_t boot, uint32_t seq, Classe c, uint32_t lacuna) {
  if (!aceitar(c)) return;
  if (c == Classe::REINICIO) reinicios_++;
  perdidos_ += lacuna;
  aceitos_++;
  iniciado_ = true;
  boot_ = boot;
  seq_ = seq;
}

}  // namespace protocolo
