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

}  // namespace

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
  }
  return "?";
}

Rejeicao validar(const uint8_t* dados, size_t tamanho, PacoteLeitura& saida) {
  if (dados == nullptr || tamanho < 2) return Rejeicao::TAMANHO_CURTO;
  if (dados[0] != VERSAO) return Rejeicao::VERSAO_DESCONHECIDA;
  if (dados[1] != static_cast<uint8_t>(Tipo::LEITURA)) return Rejeicao::TIPO_DESCONHECIDO;
  if (tamanho != sizeof(PacoteLeitura)) return Rejeicao::TAMANHO_ERRADO;

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

const char* nomeClasse(Classe c) {
  switch (c) {
    case Classe::PRIMEIRO:  return "primeiro";
    case Classe::NOVO:      return "novo";
    case Classe::DUPLICADO: return "duplicado";
    case Classe::ANTIGO:    return "antigo";
    case Classe::REINICIO:  return "reinicio";
  }
  return "?";
}

Classe RastreadorSequencia::registrar(uint16_t boot, uint32_t seq) {
  ultimaLacuna_ = 0;
  Classe c;
  if (!iniciado_) {
    // Coordenador acabou de ligar: o histórico anterior é desconhecido, não conta perdas.
    iniciado_ = true;
    c = Classe::PRIMEIRO;
  } else if (boot != boot_) {
    // Qualquer boot diferente é tratado como reinício (inclusive menor: a NVS do nó
    // pode ter sido apagada). O seq recomeça de 0, então os seq anteriores a este
    // no novo boot se perderam. Perdas no fim do boot anterior não são detectáveis.
    reinicios_++;
    ultimaLacuna_ = seq;
    c = Classe::REINICIO;
  } else {
    uint32_t diferenca = seq - seq_;  // aritmética modular de 32 bits
    if (diferenca == 0) {
      descartados_++;
      return Classe::DUPLICADO;
    }
    if (diferenca >= 0x80000000u) {  // "atrás" do último na metade do círculo
      descartados_++;
      return Classe::ANTIGO;
    }
    ultimaLacuna_ = diferenca - 1;
    c = Classe::NOVO;
  }
  perdidos_ += ultimaLacuna_;
  aceitos_++;
  boot_ = boot;
  seq_ = seq;
  return c;
}

}  // namespace protocolo
