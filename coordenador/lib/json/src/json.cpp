#include "json.h"

#include <cstdio>
#include <cstring>

namespace json {

// ---------------------------------------------------------------- Escritor
void Escritor::escrever(const char* s) { escrever(s, strlen(s)); }

void Escritor::antesDeValor() {
  if (depoisDeChave_) {
    depoisDeChave_ = false;
    return;
  }
  if (n_ > 0) {
    if (!primeiro_[n_ - 1]) escrever(",", 1);
    primeiro_[n_ - 1] = false;
  }
}

void Escritor::abrir(char c) {
  antesDeValor();
  if (n_ >= MAX_PROFUNDIDADE) {
    ok_ = false;
    return;
  }
  escrever(&c, 1);
  primeiro_[n_++] = true;
}

void Escritor::fechar(char c) {
  if (n_ == 0 || depoisDeChave_) {
    ok_ = false;
    return;
  }
  n_--;
  escrever(&c, 1);
}

Escritor& Escritor::objeto() {
  abrir('{');
  return *this;
}
Escritor& Escritor::fimObjeto() {
  fechar('}');
  return *this;
}
Escritor& Escritor::lista() {
  abrir('[');
  return *this;
}
Escritor& Escritor::fimLista() {
  fechar(']');
  return *this;
}

Escritor& Escritor::chave(const char* nome) {
  texto(nome);
  escrever(":", 1);
  depoisDeChave_ = true;
  return *this;
}

Escritor& Escritor::texto(const char* s) {
  antesDeValor();
  escrever("\"", 1);
  // RFC 8259, §7: escapar aspas, barra invertida e os caracteres de controle
  // (U+0000 a U+001F). Bytes ≥ 0x80 (UTF-8) passam como estão.
  const char* inicio = s;
  for (; *s; s++) {
    unsigned char c = static_cast<unsigned char>(*s);
    const char* esc = nullptr;
    char u[7];
    if (c == '"') esc = "\\\"";
    else if (c == '\\') esc = "\\\\";
    else if (c == '\n') esc = "\\n";
    else if (c == '\r') esc = "\\r";
    else if (c == '\t') esc = "\\t";
    else if (c < 0x20) {
      snprintf(u, sizeof(u), "\\u%04x", c);
      esc = u;
    }
    if (esc != nullptr) {
      if (s > inicio) escrever(inicio, static_cast<size_t>(s - inicio));
      escrever(esc);
      inicio = s + 1;
    }
  }
  if (s > inicio) escrever(inicio, static_cast<size_t>(s - inicio));
  escrever("\"", 1);
  return *this;
}

Escritor& Escritor::inteiro(int64_t v) {
  antesDeValor();
  char b[24];
  snprintf(b, sizeof(b), "%lld", static_cast<long long>(v));
  escrever(b);
  return *this;
}

Escritor& Escritor::natural(uint64_t v) {
  antesDeValor();
  char b[24];
  snprintf(b, sizeof(b), "%llu", static_cast<unsigned long long>(v));
  escrever(b);
  return *this;
}

Escritor& Escritor::decimal(int32_t v, uint8_t casas) {
  antesDeValor();
  uint32_t escala = 1;
  for (uint8_t i = 0; i < casas; i++) escala *= 10;
  uint32_t a = v < 0 ? static_cast<uint32_t>(-(int64_t)v) : static_cast<uint32_t>(v);
  char b[24];
  if (casas == 0)
    snprintf(b, sizeof(b), "%s%lu", v < 0 ? "-" : "", static_cast<unsigned long>(a));
  else
    snprintf(b, sizeof(b), "%s%lu.%0*lu", v < 0 ? "-" : "", static_cast<unsigned long>(a / escala), casas,
             static_cast<unsigned long>(a % escala));
  escrever(b);
  return *this;
}

Escritor& Escritor::booleano(bool v) {
  antesDeValor();
  escrever(v ? "true" : "false");
  return *this;
}

Escritor& Escritor::nulo() {
  antesDeValor();
  escrever("null", 4);
  return *this;
}

// ---------------------------------------------------------------- Leitor
namespace {

constexpr int MAX_NIVEIS = 8;

struct Analisador {
  const char* s;
  size_t n;
  size_t i = 0;
  Token* t;
  size_t max;
  size_t usados = 0;

  void espacos() {
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
  }

  int novo(Tipo tipo, size_t inicio) {
    if (usados >= max) return -1;
    t[usados] = {tipo, static_cast<uint16_t>(inicio), 0, 0};
    return static_cast<int>(usados++);
  }

  bool texto() {  // s[i] == '"'
    int k = novo(Tipo::TEXTO, i + 1);
    if (k < 0) return false;
    i++;
    while (i < n) {
      unsigned char c = static_cast<unsigned char>(s[i]);
      if (c == '"') {
        t[k].fim = static_cast<uint16_t>(i);
        t[k].proximo = static_cast<uint16_t>(usados);
        i++;
        return true;
      }
      if (c < 0x20) return false;  // controle sem escape
      if (c == '\\') {
        if (++i >= n) return false;
        char e = s[i];
        if (e == 'u') {
          for (int h = 1; h <= 4; h++) {
            if (i + h >= n) return false;
            char x = s[i + h];
            bool hex = (x >= '0' && x <= '9') || (x >= 'a' && x <= 'f') || (x >= 'A' && x <= 'F');
            if (!hex) return false;
          }
          i += 4;
        } else if (!strchr("\"\\/bfnrt", e)) {
          return false;
        }
      }
      i++;
    }
    return false;
  }

  static bool digito(char c) { return c >= '0' && c <= '9'; }

  bool primitivo() {
    size_t ini = i;
    if (s[i] == 't' || s[i] == 'f' || s[i] == 'n') {
      const char* p = s[i] == 't' ? "true" : s[i] == 'f' ? "false" : "null";
      size_t m = strlen(p);
      if (n - i < m || strncmp(s + i, p, m) != 0) return false;
      i += m;
    } else {
      // número (RFC 8259, §6): -? (0 | [1-9][0-9]*) (.[0-9]+)? ([eE][+-]?[0-9]+)?
      if (s[i] == '-') i++;
      if (i >= n) return false;
      if (s[i] == '0') i++;
      else if (digito(s[i]))
        while (i < n && digito(s[i])) i++;
      else
        return false;
      if (i < n && s[i] == '.') {
        i++;
        if (i >= n || !digito(s[i])) return false;
        while (i < n && digito(s[i])) i++;
      }
      if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) i++;
        if (i >= n || !digito(s[i])) return false;
        while (i < n && digito(s[i])) i++;
      }
    }
    int k = novo(Tipo::PRIMITIVO, ini);
    if (k < 0) return false;
    t[k].fim = static_cast<uint16_t>(i);
    t[k].proximo = static_cast<uint16_t>(usados);
    return true;
  }

  bool valor(int nivel) {
    espacos();
    if (i >= n) return false;
    char c = s[i];
    if (c == '{' || c == '[') {
      if (nivel >= MAX_NIVEIS) return false;
      bool obj = c == '{';
      int k = novo(obj ? Tipo::OBJETO : Tipo::LISTA, i);
      if (k < 0) return false;
      i++;
      espacos();
      char fecha = obj ? '}' : ']';
      if (i < n && s[i] == fecha) {
        i++;
      } else {
        for (;;) {
          if (obj) {
            espacos();
            if (i >= n || s[i] != '"' || !texto()) return false;
            espacos();
            if (i >= n || s[i] != ':') return false;
            i++;
          }
          if (!valor(nivel + 1)) return false;
          espacos();
          if (i >= n) return false;
          if (s[i] == ',') {
            i++;
            continue;
          }
          if (s[i] != fecha) return false;
          i++;
          break;
        }
      }
      t[k].fim = static_cast<uint16_t>(i);
      t[k].proximo = static_cast<uint16_t>(usados);
      return true;
    }
    if (c == '"') return texto();
    return primitivo();
  }
};

}  // namespace

int analisar(const char* s, size_t n, Token* t, size_t max) {
  if (s == nullptr || t == nullptr || n > 65535) return -1;
  Analisador a{s, n, 0, t, max, 0};
  if (!a.valor(0)) return -1;
  a.espacos();
  if (a.i != n) return -1;  // lixo depois do valor raiz
  return static_cast<int>(a.usados);
}

int buscar(const char* s, const Token* t, int ntokens, int objeto, const char* nome) {
  if (objeto < 0 || objeto >= ntokens || t[objeto].tipo != Tipo::OBJETO) return -1;
  size_t m = strlen(nome);
  int k = objeto + 1;
  while (k < t[objeto].proximo && k < ntokens) {
    const Token& ch = t[k];  // chave (TEXTO, sem escapes nos nomes conhecidos)
    int v = ch.proximo;      // o valor vem logo depois da chave
    if (v >= ntokens) return -1;
    if (ch.tipo == Tipo::TEXTO && static_cast<size_t>(ch.fim - ch.inicio) == m &&
        strncmp(s + ch.inicio, nome, m) == 0)
      return v;
    k = t[v].proximo;
  }
  return -1;
}

bool comoTexto(const char* s, const Token& t, char* saida, size_t max) {
  if (t.tipo != Tipo::TEXTO || max == 0) return false;
  size_t o = 0;
  for (size_t i = t.inicio; i < t.fim; i++) {
    char c = s[i];
    if (c == '\\') {
      char e = s[++i];
      switch (e) {
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'u': {
          unsigned v = 0;
          for (int h = 1; h <= 4; h++) {
            char x = s[i + h];
            v = v * 16 + static_cast<unsigned>(x <= '9' ? x - '0' : (x | 0x20) - 'a' + 10);
          }
          if (v == 0 || v >= 0x80) return false;  // só ASCII (os campos da API são ASCII)
          c = static_cast<char>(v);
          i += 4;
          break;
        }
        default: c = e;  // \" \\ \/
      }
    }
    if (o + 1 >= max) return false;
    saida[o++] = c;
  }
  saida[o] = '\0';
  return true;
}

bool comoInteiro(const char* s, const Token& t, int64_t& v) {
  if (t.tipo != Tipo::PRIMITIVO) return false;
  size_t i = t.inicio;
  bool neg = s[i] == '-';
  if (neg) i++;
  if (i >= t.fim) return false;
  uint64_t a = 0;
  for (; i < t.fim; i++) {
    if (s[i] < '0' || s[i] > '9') return false;  // fração, expoente, true/false/null
    a = a * 10 + static_cast<uint64_t>(s[i] - '0');
    if (a > (1ULL << 62)) return false;
  }
  v = neg ? -static_cast<int64_t>(a) : static_cast<int64_t>(a);
  return true;
}

bool ehNulo(const char* s, const Token& t) {
  return t.tipo == Tipo::PRIMITIVO && t.fim - t.inicio == 4 && strncmp(s + t.inicio, "null", 4) == 0;
}

bool comoBool(const char* s, const Token& t, bool& v) {
  if (t.tipo != Tipo::PRIMITIVO) return false;
  if (t.fim - t.inicio == 4 && strncmp(s + t.inicio, "true", 4) == 0) v = true;
  else if (t.fim - t.inicio == 5 && strncmp(s + t.inicio, "false", 5) == 0) v = false;
  else return false;
  return true;
}

}  // namespace json
