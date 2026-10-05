#include "aht20.h"

namespace aht20 {

namespace {

TwoWire* barramento = &Wire;

void esperaPadrao(uint32_t ms) { delay(ms); }
FuncaoEspera esperar = esperaPadrao;

constexpr uint32_t ESPERA_LIGAR_MS = 100;       // ≥ 100 ms após energizar (7.1)
constexpr uint32_t ESPERA_ANTES_MEDIR_MS = 10;  // 10 ms antes do 0xAC (7.4, passo 2)
constexpr uint32_t ESPERA_CONVERSAO_MS = 80;    // 80 ms de conversão (7.4, passo 3)
constexpr uint32_t INTERVALO_OCUPADO_MS = 10;   // nova consulta enquanto ocupado
// O datasheet só dá os 80 ms típicos, sem máximo. Limite nosso, com folga;
// o tempo real medido fica em Leitura::conversaoMs.
constexpr uint32_t LIMITE_CONVERSAO_MS = 200;

constexpr uint8_t BIT_OCUPADO = 0x80;       // estado, bit 7 (tabela 9)
constexpr uint8_t MASCARA_CALIBRACAO = 0x18;  // 7.4, passo 1

// Lê n bytes do sensor. Ler 1 byte é a "leitura do estado" (o "0x71" do
// datasheet é o byte de endereço de leitura: 0x38 << 1 | 1).
bool lerBytes(uint8_t* destino, size_t n) {
  if (barramento->requestFrom(ENDERECO, n) != n) return false;
  for (size_t i = 0; i < n; i++) destino[i] = barramento->read();
  return true;
}

}  // namespace

const char* nomeEstado(Estado e) {
  switch (e) {
    case Estado::OK:            return "ok";
    case Estado::SEM_RESPOSTA:  return "sem_resposta";
    case Estado::NAO_CALIBRADO: return "nao_calibrado";
    case Estado::TIMEOUT:       return "timeout";
    case Estado::ERRO_CRC:      return "erro_crc";
  }
  return "?";
}

uint8_t crc8(const uint8_t* dados, size_t n) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= dados[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
  }
  return crc;
}

void definirEspera(FuncaoEspera espera) { esperar = espera ? espera : esperaPadrao; }

Estado iniciar(TwoWire& wire, uint32_t energizadoMs) {
  barramento = &wire;

  // A espera conta a partir do instante em que o sensor recebeu alimentação
  // (MOSFET ligado, ou o boot, quando ele fica sempre ligado).
  uint32_t desdeLigar = millis() - energizadoMs;
  if ((int32_t)desdeLigar >= 0 && desdeLigar < ESPERA_LIGAR_MS) esperar(ESPERA_LIGAR_MS - desdeLigar);

  uint8_t estado;
  if (!lerBytes(&estado, 1)) return Estado::SEM_RESPOSTA;

  // O datasheet manda inicializar os registradores 0x1B, 0x1C e 0x1E neste caso,
  // mas remete a um exemplo do site em vez de documentar a sequência.
  // Sem documentação, reportamos o erro em vez de executar comandos não descritos.
  if ((estado & MASCARA_CALIBRACAO) != MASCARA_CALIBRACAO) return Estado::NAO_CALIBRADO;

  return Estado::OK;
}

Estado ler(Leitura& leitura) {
  esperar(ESPERA_ANTES_MEDIR_MS);

  // Comando de medição: 0xAC com os parâmetros 0x33 e 0x00 (7.4, passo 2).
  barramento->beginTransmission(ENDERECO);
  barramento->write(0xAC);
  barramento->write(0x33);
  barramento->write(0x00);
  if (barramento->endTransmission() != 0) return Estado::SEM_RESPOSTA;
  uint32_t inicio = millis();

  // Consulta o byte de estado até o bit de ocupado ir a 0 e só então lê os
  // dados (7.4, passo 3). Uma consulta que falha no I²C é repetida dentro do
  // mesmo limite de tempo: no estado anômalo do sensor, a 1ª leitura após o
  // 0xAC falha e a seguinte funciona (ver docs/sensores.md).
  esperar(ESPERA_CONVERSAO_MS);
  leitura.tentativasExtras = 0;
  while (true) {
    uint8_t estado;
    if (lerBytes(&estado, 1)) {
      if (!(estado & BIT_OCUPADO)) break;
    } else {
      leitura.tentativasExtras++;
    }
    if (millis() - inicio > LIMITE_CONVERSAO_MS) {
      return leitura.tentativasExtras ? Estado::SEM_RESPOSTA : Estado::TIMEOUT;
    }
    esperar(INTERVALO_OCUPADO_MS);
  }
  leitura.conversaoMs = millis() - inicio;

  if (!lerBytes(leitura.bruto, sizeof(leitura.bruto))) return Estado::SEM_RESPOSTA;

  // O datasheet não diz sobre quais bytes o CRC é calculado; a hipótese é
  // "estado + 5 bytes de dados" (os 6 primeiros), verificada na Etapa 2.
  if (crc8(leitura.bruto, 6) != leitura.bruto[6]) return Estado::ERRO_CRC;

  // 20 bits de umidade e 20 bits de temperatura; o byte 3 é dividido entre os dois.
  const uint8_t* b = leitura.bruto;
  uint32_t sUmidade = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | (b[3] >> 4);
  uint32_t sTemperatura = ((uint32_t)(b[3] & 0x0F) << 16) | ((uint32_t)b[4] << 8) | b[5];

  // Conversões da seção 8 (2^20 = 1048576).
  leitura.umidadePct = sUmidade / 1048576.0f * 100.0f;
  leitura.temperaturaC = sTemperatura / 1048576.0f * 200.0f - 50.0f;
  return Estado::OK;
}

}  // namespace aht20
