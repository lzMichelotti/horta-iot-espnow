#include "filtro.h"

#include <cstring>

namespace filtro {

// ---------------------------------------------------------------- Âncoras
void Ancoras::registrar(const registro::Registro& r) {
  Ancora* a = nullptr;
  for (size_t i = 0; i < n_; i++)
    if (tabela_[i].boot == r.boot_coord) a = &tabela_[i];

  if (!registro::horaValida(r)) {
    if (a != nullptr) return;
    if (n_ == CAPACIDADE) {
      memmove(&tabela_[0], &tabela_[1], (CAPACIDADE - 1) * sizeof(Ancora));
      n_--;
    }
    tabela_[n_++] = {r.boot_coord, false, 0};
    return;
  }
  // Com hora: só interessa se o boot tem registros sem hora e ainda não tem desvio
  // (vale o do primeiro registro com hora).
  if (a != nullptr && !a->temDesvio) {
    a->desvio = static_cast<int64_t>(r.utc_s) - static_cast<int64_t>(r.desde_boot_s);
    a->temDesvio = true;
  }
}

bool Ancoras::desvio(uint16_t boot, int64_t& saida) const {
  for (size_t i = 0; i < n_; i++)
    if (tabela_[i].boot == boot && tabela_[i].temDesvio) {
      saida = tabela_[i].desvio;
      return true;
    }
  return false;
}

const char* nomeHora(Hora h) {
  switch (h) {
    case Hora::VALIDA: return "valida";
    case Hora::RECONSTRUIDA: return "reconstruida";
    case Hora::SEM_HORA: return "sem_hora";
  }
  return "?";
}

Hora horaEfetiva(const registro::Registro& r, const Ancoras& a, uint32_t& utc) {
  if (registro::horaValida(r)) {
    utc = r.utc_s;
    return Hora::VALIDA;
  }
  int64_t d = 0;
  if (a.desvio(r.boot_coord, d)) {
    int64_t v = d + static_cast<int64_t>(r.desde_boot_s);
    if (v >= 0 && v <= static_cast<int64_t>(UINT32_MAX)) {
      utc = static_cast<uint32_t>(v);
      return Hora::RECONSTRUIDA;
    }
  }
  utc = 0;
  return Hora::SEM_HORA;
}

// ---------------------------------------------------------------- Filtro
bool passa(const Filtro& f, const registro::Registro& r, Hora h, uint32_t utcEfetivo) {
  if (f.porNo && memcmp(r.mac, f.mac, 6) != 0) return false;
  if (f.porHora && (h == Hora::SEM_HORA || utcEfetivo < f.de || utcEfetivo > f.ate)) return false;
  return true;
}

bool podePular(const Filtro& f, const anel::Resumo& s) {
  if (s.posicoes == 0) return true;
  return f.porHora && s.semHora == 0 && !anel::cruza(s, f.de, f.ate);
}

// ---------------------------------------------------------------- Percurso
Percurso::Percurso(const anel::Indice& ind, uint64_t cursor, bool decrescente) : ind_(ind), dec_(decrescente) {
  const size_t n = ind_.quantidade();
  if (n == 0) return;
  const uint32_t k = ind_.politica().registrosPorSegmento;
  const uint64_t primeiroId = anel::idGlobal(ind_.em(0).numero, 0, k);

  if (cursor == 0) {
    irParaSegmento(dec_ ? n - 1 : 0);
    return;
  }
  uint32_t segC = 0, posC = 0;
  anel::deIdGlobal(cursor, k, segC, posC);

  if (!dec_) {
    if (cursor < primeiroId) {  // já apagado: continua do mais antigo que existe
      rotacionado_ = true;
      irParaSegmento(0);
      return;
    }
    for (size_t s = 0; s < n; s++) {
      const anel::Resumo& r = ind_.em(s);
      if (r.numero < segC) continue;
      if (r.numero == segC && posC < r.posicoes) {
        seg_ = s;
        pos_ = posC;
        valido_ = true;
      } else {
        irParaSegmento(r.numero == segC ? s + 1 : s);
      }
      return;
    }
    return;  // depois do fim: nada (ainda) a entregar
  }

  // Decrescente
  if (cursor < primeiroId) {  // nada mais antigo existe
    rotacionado_ = true;
    return;
  }
  for (size_t s = n; s-- > 0;) {
    const anel::Resumo& r = ind_.em(s);
    if (r.numero > segC) continue;
    if (r.numero == segC && r.posicoes > 0) {
      seg_ = s;
      pos_ = posC < r.posicoes ? posC : r.posicoes - 1;
      valido_ = true;
    } else {
      irParaSegmento(s);
    }
    return;
  }
}

uint64_t Percurso::id() const {
  return anel::idGlobal(ind_.em(seg_).numero, pos_, ind_.politica().registrosPorSegmento);
}

void Percurso::irParaSegmento(size_t s) {
  const size_t n = ind_.quantidade();
  valido_ = false;
  if (!dec_) {
    for (; s < n; s++)
      if (ind_.em(s).posicoes > 0) {
        seg_ = s;
        pos_ = 0;
        valido_ = true;
        return;
      }
    return;
  }
  for (size_t i = s + 1; i-- > 0;)
    if (i < n && ind_.em(i).posicoes > 0) {
      seg_ = i;
      pos_ = ind_.em(i).posicoes - 1;
      valido_ = true;
      return;
    }
}

void Percurso::avancar() {
  if (!valido_) return;
  if (!dec_) {
    if (++pos_ >= ind_.em(seg_).posicoes) irParaSegmento(seg_ + 1);
    return;
  }
  if (pos_ > 0) {
    pos_--;
  } else if (seg_ > 0) {
    irParaSegmento(seg_ - 1);
  } else {
    valido_ = false;
  }
}

void Percurso::pularSegmento() {
  if (!valido_) return;
  if (!dec_) {
    irParaSegmento(seg_ + 1);
  } else if (seg_ > 0) {
    irParaSegmento(seg_ - 1);
  } else {
    valido_ = false;
  }
}

}  // namespace filtro
