// HMAC-SHA256 no ESP32 pelo mbedTLS do ESP-IDF (sem dependência nova).
// API: mbedtls_md_hmac(), mbedtls/md.h (mbedTLS 3.x do ESP-IDF v5.5). O SHA-256
// roda no acelerador de hardware (CONFIG_MBEDTLS_HARDWARE_SHA=y no sdkconfig do
// núcleo Arduino-ESP32 3.3.12). No PC (testes) este arquivo não compila nada.
#if defined(ESP_PLATFORM)

#include <mbedtls/md.h>

#include "protocolo.h"

namespace protocolo {

bool hmacSha256(const uint8_t* chave, size_t tamanhoChave, const uint8_t* mensagem, size_t tamanhoMensagem,
                uint8_t saida[TAMANHO_HMAC]) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr) return false;
  return mbedtls_md_hmac(info, chave, tamanhoChave, mensagem, tamanhoMensagem, saida) == 0;
}

}  // namespace protocolo

#endif  // ESP_PLATFORM
