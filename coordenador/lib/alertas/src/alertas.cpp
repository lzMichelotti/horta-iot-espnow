#include "alertas.h"

#include <cstring>

namespace alertas {

namespace {

EstadoAlerta& estado(No& n, Tipo t) { return n.estado[static_cast<size_t>(t)]; }

// Uma regra: ativa depois de nAtivar leituras seguidas em condAtivar; resolve
// na primeira leitura em condResolver (a banda de histerese fica entre as duas).
void aplicar(No& n, Tipo t, bool condAtivar, bool condResolver, uint8_t nAtivar, bool temValor, int32_t valor,
             const Ref& ref, Aviso aviso, void* contexto) {
  EstadoAlerta& s = estado(n, t);
  if (temValor) {
    s.valor = valor;
    s.temValor = true;
  }
  if (!s.ativo) {
    if (!condAtivar) {
      s.seguidas = 0;
      return;
    }
    if (s.seguidas == 0) s.candidato = ref;
    if (s.seguidas < 255) s.seguidas++;
    if (s.seguidas < (nAtivar ? nAtivar : 1)) return;
    s.ativo = true;
    s.resolvido = false;
    s.inicio = s.candidato;
    s.fim = Ref{};
    s.seguidas = 0;
    if (aviso != nullptr) aviso(n, t, Mudanca::ATIVOU, contexto);
  } else if (condResolver) {
    s.ativo = false;
    s.resolvido = true;
    s.fim = ref;
    s.seguidas = 0;
    if (aviso != nullptr) aviso(n, t, Mudanca::RESOLVEU, contexto);
  }
}

}  // namespace

const char* nomeTipo(Tipo t) {
  switch (t) {
    case Tipo::SOLO_SECO:         return "solo_seco";
    case Tipo::SOLO_ENCHARCADO:   return "solo_encharcado";
    case Tipo::TEMPERATURA_ALTA:  return "temperatura_alta";
    case Tipo::TEMPERATURA_BAIXA: return "temperatura_baixa";
    case Tipo::BATERIA_BAIXA:     return "bateria_baixa";
    case Tipo::BATERIA_CRITICA:   return "bateria_critica";
    case Tipo::SEM_COMUNICAR:     return "sem_comunicar";
    case Tipo::ERRO_AR:           return "erro_sensor_ar";
    case Tipo::ERRO_SOLO:         return "erro_sensor_solo";
    case Tipo::ERRO_ALIMENTACAO:  return "erro_sensor_alimentacao";
    case Tipo::SEM_BATERIA:       return "no_sem_bateria";
  }
  return "?";
}

Unidade unidade(Tipo t) {
  switch (t) {
    case Tipo::SOLO_SECO:
    case Tipo::SOLO_ENCHARCADO:   return Unidade::PCT_DECIMOS;
    case Tipo::TEMPERATURA_ALTA:
    case Tipo::TEMPERATURA_BAIXA: return Unidade::C_CENTESIMOS;
    case Tipo::BATERIA_BAIXA:
    case Tipo::BATERIA_CRITICA:   return Unidade::MV;
    case Tipo::SEM_COMUNICAR:
    case Tipo::SEM_BATERIA:       return Unidade::SEGUNDOS;
    default:                      return Unidade::NENHUMA;
  }
}

const char* nomeUnidade(Unidade u) {
  switch (u) {
    case Unidade::PCT_DECIMOS:  return "pct";
    case Unidade::C_CENTESIMOS: return "c";
    case Unidade::MV:           return "mv";
    case Unidade::SEGUNDOS:     return "s";
    default:                    return nullptr;
  }
}

const char* nomeClasseSolo(ClasseSolo c) {
  switch (c) {
    case ClasseSolo::SECO:        return "seco";
    case ClasseSolo::UMIDO:       return "umido";
    case ClasseSolo::MUITO_UMIDO: return "muito_umido";
    default:                      return nullptr;
  }
}

ClasseSolo classificarSolo(bool temPct, int32_t decimos, const Limiares& l) {
  if (!temPct) return ClasseSolo::DESCONHECIDA;
  if (decimos < l.soloSecoDecimos) return ClasseSolo::SECO;
  if (decimos > l.soloMuitoUmidoDecimos) return ClasseSolo::MUITO_UMIDO;
  return ClasseSolo::UMIDO;
}

const No* Tabela::encontrar(const uint8_t mac[6]) const {
  for (const auto& n : nos_)
    if (n.usado && memcmp(n.mac, mac, 6) == 0) return &n;
  return nullptr;
}

No* Tabela::buscar(const uint8_t mac[6], bool criar) {
  for (auto& n : nos_)
    if (n.usado && memcmp(n.mac, mac, 6) == 0) return &n;
  if (!criar) return nullptr;
  for (auto& n : nos_)
    if (!n.usado) {
      n = No{};
      n.usado = true;
      memcpy(n.mac, mac, 6);
      return &n;
    }
  return nullptr;
}

void Tabela::avaliar(No& n, const Leitura& l, const Limiares& lim, Aviso aviso, void* contexto) {
  using protocolo::Estado;
  using protocolo::Grandeza;
  Estado eT = protocolo::lerEstado(l.estados, Grandeza::TEMPERATURA);
  Estado eU = protocolo::lerEstado(l.estados, Grandeza::UMIDADE_AR);
  Estado eS = protocolo::lerEstado(l.estados, Grandeza::SOLO);
  Estado eA = protocolo::lerEstado(l.estados, Grandeza::ALIMENTACAO);
  const Ref& r = l.ref;

  // Erros de sensor: N leituras seguidas com erro ativam; 1 leitura ok resolve.
  bool erroAr = eT != Estado::OK || eU != Estado::OK;
  aplicar(n, Tipo::ERRO_AR, erroAr, !erroAr, lim.errosParaAtivar, false, 0, r, aviso, contexto);
  bool erroSolo = eS != Estado::OK;
  aplicar(n, Tipo::ERRO_SOLO, erroSolo, !erroSolo, lim.errosParaAtivar, false, 0, r, aviso, contexto);
  if (lim.erroAlimentacao) {
    bool erroA = eA != Estado::OK;
    aplicar(n, Tipo::ERRO_ALIMENTACAO, erroA, !erroA, lim.errosParaAtivar, false, 0, r, aviso, contexto);
  }

  // Solo: só com leitura ok e calibração; sem isso, o estado dos alertas fica como está.
  if (eS == Estado::OK && l.temPct) {
    int32_t d = l.soloDecimos;
    aplicar(n, Tipo::SOLO_SECO, d < lim.soloSecoDecimos, d >= lim.soloSecoDecimos + lim.soloBandaDecimos,
            lim.leiturasParaAtivar, true, d, r, aviso, contexto);
    aplicar(n, Tipo::SOLO_ENCHARCADO, d > lim.soloEncharcadoDecimos,
            d <= lim.soloEncharcadoDecimos - lim.soloBandaDecimos, lim.leiturasEncharcado, true, d, r, aviso,
            contexto);
    n.classe = classificarSolo(true, d, lim);
  } else {
    n.classe = ClasseSolo::DESCONHECIDA;
  }

  // Temperatura do ar.
  if (eT == Estado::OK) {
    int32_t t = l.temperaturaC100;
    aplicar(n, Tipo::TEMPERATURA_ALTA, t > lim.temperaturaAltaC100,
            t <= lim.temperaturaAltaC100 - lim.temperaturaBandaC100, lim.leiturasParaAtivar, true, t, r, aviso,
            contexto);
    aplicar(n, Tipo::TEMPERATURA_BAIXA, t < lim.temperaturaBaixaC100,
            t >= lim.temperaturaBaixaC100 + lim.temperaturaBandaC100, lim.leiturasParaAtivar, true, t, r, aviso,
            contexto);
  }

  // Bateria: o modo vem do nó (com a histerese dele); desconhecido não muda nada.
  if (l.modo != protocolo::ModoBateria::DESCONHECIDO) {
    using protocolo::ModoBateria;
    n.modo = l.modo;
    bool temV = eA != Estado::ERRO;
    bool baixa = l.modo == ModoBateria::ECONOMIA || l.modo == ModoBateria::CRITICO;
    bool critica = l.modo == ModoBateria::CRITICO;
    aplicar(n, Tipo::BATERIA_BAIXA, baixa, l.modo == ModoBateria::NORMAL, 1, temV, l.alimentacaoMv, r, aviso,
            contexto);
    aplicar(n, Tipo::BATERIA_CRITICA, critica, !critica, 1, temV, l.alimentacaoMv, r, aviso, contexto);
  }

  // Qualquer leitura nova resolve o sem_comunicar (inclusive uma atrasada da fila
  // do nó) e o no_sem_bateria (a bateria foi recarregada: o nó voltou a falar).
  aplicar(n, Tipo::SEM_COMUNICAR, false, true, 1, false, 0, r, aviso, contexto);
  aplicar(n, Tipo::SEM_BATERIA, false, true, 1, false, 0, r, aviso, contexto);
  n.ultima = r;
}

void Tabela::verificarSilencio(No& n, uint32_t haS, uint32_t limiteS, Aviso aviso, void* contexto) {
  if (!n.ultima.definida) return;
  // Ativa na primeira verificação acima do limite; o início é a última leitura
  // recebida (o nó está calado desde ela). Só uma leitura nova resolve (avaliar).
  // Último pacote em crítico: o nó silenciou de propósito (protocolo.md, 14).
  Tipo t = n.modo == protocolo::ModoBateria::CRITICO ? Tipo::SEM_BATERIA : Tipo::SEM_COMUNICAR;
  aplicar(n, t, haS > limiteS, false, 1, true, static_cast<int32_t>(haS), n.ultima, aviso, contexto);
}

size_t Tabela::ativos(const No& n) {
  size_t k = 0;
  for (const auto& s : n.estado)
    if (s.ativo) k++;
  return k;
}

size_t Tabela::ativos() const {
  size_t k = 0;
  for (const auto& n : nos_)
    if (n.usado) k += ativos(n);
  return k;
}

void Tabela::limpar() {
  for (auto& n : nos_) n = No{};
}

}  // namespace alertas
