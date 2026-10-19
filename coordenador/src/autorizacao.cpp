#include "autorizacao.h"

#include <Preferences.h>
#include <cstring>
#include <esp_random.h>

namespace autorizacao {

namespace {

// https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html
constexpr const char* NVS_NAMESPACE = "coordenador";
constexpr const char* NVS_CHAVE = "token";

char token[TAM_TOKEN + 1] = "";

bool valido(const char* t) {
  if (strlen(t) != TAM_TOKEN) return false;
  for (size_t i = 0; i < TAM_TOKEN; i++)
    if (!isxdigit(static_cast<unsigned char>(t[i]))) return false;
  return true;
}

bool gravar(const char* t) {
  Preferences nvs;
  nvs.begin(NVS_NAMESPACE, false);
  bool ok = t[0] ? nvs.putString(NVS_CHAVE, t) == strlen(t) : nvs.remove(NVS_CHAVE);
  nvs.end();
  return ok;
}

}  // namespace

void iniciar() {
  Preferences nvs;
  nvs.begin(NVS_NAMESPACE, true);
  String t = nvs.isKey(NVS_CHAVE) ? nvs.getString(NVS_CHAVE, "") : String();
  nvs.end();
  if (valido(t.c_str())) {
    strncpy(token, t.c_str(), sizeof(token));
    token[TAM_TOKEN] = '\0';
    Serial.println("[HTTP] token das rotas de escrita: configurado");
  } else {
    token[0] = '\0';
    Serial.println("[HTTP] token das rotas de escrita: NAO configurado (escritas recusadas; 'token gerar')");
  }
}

bool configurado() { return token[0] != '\0'; }

bool autorizado(const char* cabecalho) {
  if (!configurado() || cabecalho == nullptr) return false;
  static const char PREFIXO[] = "Bearer ";
  if (strncmp(cabecalho, PREFIXO, sizeof(PREFIXO) - 1) != 0) return false;
  const char* recebido = cabecalho + sizeof(PREFIXO) - 1;
  if (strlen(recebido) != TAM_TOKEN) return false;  // o tamanho é público: não precisa de tempo constante
  // Compara tudo, sem sair no primeiro caractere diferente: o tempo de resposta
  // não revela quantos caracteres estão certos.
  uint8_t diferenca = 0;
  for (size_t i = 0; i < TAM_TOKEN; i++) diferenca |= static_cast<uint8_t>(recebido[i] ^ token[i]);
  return diferenca == 0;
}

void comando(const char* argumento) {
  if (argumento == nullptr || argumento[0] == '\0') {
    Serial.printf("[HTTP] token: %s\n", configurado() ? "configurado" : "nao configurado (escritas recusadas)");
  } else if (strcmp(argumento, "gerar") == 0) {
    uint8_t bytes[TAM_TOKEN / 2];
    esp_fill_random(bytes, sizeof(bytes));  // rádio ligado (SoftAP): números aleatórios de verdade
    char novo[TAM_TOKEN + 1];
    for (size_t i = 0; i < sizeof(bytes); i++) snprintf(novo + 2 * i, 3, "%02x", bytes[i]);
    if (!gravar(novo)) {
      Serial.println("[HTTP] ERRO ao gravar o token na NVS");
      return;
    }
    memcpy(token, novo, sizeof(token));
    // Mostrado só agora: depois, nem a serial mostra (anotar e cadastrar no app).
    Serial.printf("[HTTP] token novo (anote; o anterior deixou de valer): %s\n", token);
  } else if (strcmp(argumento, "apagar") == 0) {
    if (!gravar("")) {
      Serial.println("[HTTP] ERRO ao apagar o token da NVS");
      return;
    }
    token[0] = '\0';
    Serial.println("[HTTP] token apagado: escritas pela API recusadas");
  } else {
    Serial.println("[HTTP] uso: token | token gerar | token apagar");
  }
}

}  // namespace autorizacao
