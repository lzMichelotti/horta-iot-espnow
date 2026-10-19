// Firmware do coordenador — Etapa 4: SoftAP em canal fixo, recepção ESP-NOW por
// fila, validação (comum/protocolo) e rastreamento por nó (perda, duplicata,
// reinício). Uma linha [CSV] por pacote e um [RESUMO] por nó a cada minuto.
// Etapa 7: relógio provisório (acerto pela serial), console de comandos e
// histórico no LittleFS (um registro por pacote aceito, linha [FS] gravado).
// Etapa 7.1: confirmação cumulativa assinada para o nó ("tudo até (boot, seq)
// está gravado"), enviada antes de gravar; a borda de cada nó só avança depois
// do close() (docs/protocolo.md, seção 13). (DS3231 e HTTP: etapas 6 e 8.)
#include <Arduino.h>
#include <cstring>
#include <esp_mac.h>
#include <esp_partition.h>

#include <protocolo.h>

#include "config.h"
#include "configuracao.h"
#include "consulta.h"
#include "exportar.h"
#include "historico.h"
#include "nos.h"
#include "radio.h"
#include "relogio.h"
#include "teste_gravacao.h"

namespace {

// ---------------------------------------------------------------- Console
// Uma linha por comando ("\n"; o "\r" do CRLF é ignorado): "comando [argumento]".
void executar(char* linha) {
  while (*linha == ' ') linha++;
  char* argumento = strchr(linha, ' ');
  if (argumento != nullptr) {
    *argumento++ = '\0';
    while (*argumento == ' ') argumento++;
    for (char* fim = argumento + strlen(argumento); fim > argumento && fim[-1] == ' ';) *--fim = '\0';
  }
  if (linha[0] == '\0') return;

  if (strcmp(linha, "hora") == 0) {
    relogio::comandoHora(argumento);
  } else if (strcmp(linha, "fs") == 0) {
    historico::comando(argumento);
  } else if (strcmp(linha, "no") == 0) {
    configuracao::comando(argumento);
  } else if (strcmp(linha, "exportar") == 0) {
    exportar::comando(argumento);
  } else if (strcmp(linha, "consulta") == 0) {
    consulta::comando(argumento);
#ifdef TESTE_GRAVACAO
  } else if (strcmp(linha, "bench") == 0) {
    teste_gravacao::comando(argumento);
#endif
  } else if (strcmp(linha, "reiniciar") == 0) {
    Serial.println("[CONSOLE] esp_restart()");
    Serial.flush();
    esp_restart();
  } else if (strcmp(linha, "ajuda") == 0) {
    Serial.println("[CONSOLE] comandos:");
    Serial.println("[CONSOLE]   hora                         mostra a hora e a validade");
    Serial.println("[CONSOLE]   hora 1791297000              acerta (epoch UTC, ex.: date +%s)");
    Serial.println("[CONSOLE]   hora 2026-10-06T14:30:00Z    acerta (ISO 8601, UTC)");
    Serial.println("[CONSOLE]   fs                           estado do historico (LittleFS)");
    Serial.println("[CONSOLE]   fs listar                    resumo de cada segmento");
    Serial.println("[CONSOLE]   fs formatar                  formata a particao (APAGA o historico)");
    Serial.println("[CONSOLE]   fs gravar N                  grava N registros sinteticos (teste)");
    Serial.println("[CONSOLE]   no                           cadastro: nome e calibracao do solo de cada no");
    Serial.println("[CONSOLE]   no nome MAC NOME             cadastra ou renomeia (1-15: letras, digitos, _ -)");
    Serial.println("[CONSOLE]   no cal MAC SECO UMIDO        calibracao do solo em mV (seco > umido)");
    Serial.println("[CONSOLE]   no seco MAC | no umido MAC   ponto de calibracao = solo da ultima leitura");
    Serial.println("[CONSOLE]   no semcal MAC | no apagar MAC | no padrao (semente do config.h)");
    Serial.println("[CONSOLE]   no esquecer MAC              aceita o no de novo (NVS do no apagada: boot_antigo)");
    Serial.println("[CONSOLE]   consulta [de=T] [ate=T] [no=MAC] [ordem=asc|desc] [cursor=N] [limite=N]");
    Serial.println("[CONSOLE]            [maxlidos=N] [mostrar=0|1]   historico paginado (T: epoch ou ISO)");
    Serial.println("[CONSOLE]   consulta ultimas             ultima leitura de cada no");
    Serial.println("[CONSOLE]   exportar [de=T] [ate=T] [no=MAC] [cursor=N]   historico em CSV (linhas [EXP])");
    Serial.println("[CONSOLE]   exportar parar               interrompe a exportacao");
    Serial.println("[CONSOLE]   reiniciar                    reset por software (esp_restart)");
  } else {
    Serial.printf("[CONSOLE] comando desconhecido: \"%s\" (digite ajuda)\n", linha);
  }
}

// Lê a serial sem bloquear e executa no máximo UMA linha por chamada: com
// várias linhas pendentes, o loop() volta a esvaziar a fila do ESP-NOW entre
// um comando e outro (o resto fica no buffer da UART).
void lerConsole() {
  static char linha[config::CONSOLE_LINHA_MAX + 1];
  static size_t n = 0;
  static bool longaDemais = false;
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      linha[n] = '\0';
      if (longaDemais)
        Serial.printf("[CONSOLE] linha maior que %u caracteres, ignorada\n", (unsigned)config::CONSOLE_LINHA_MAX);
      else
        executar(linha);
      n = 0;
      longaDemais = false;
      return;
    } else if (n < config::CONSOLE_LINHA_MAX) {
      linha[n++] = c;
    } else {
      longaDemais = true;
    }
  }
}

// Tabela de partições gravada na flash (conferência do particoes.csv).
// https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/storage/partition.html
void imprimirParticoes() {
  Serial.println("[PART] nome,tipo,subtipo,endereco,tamanho_kb");
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  for (; it != nullptr; it = esp_partition_next(it)) {
    const esp_partition_t* p = esp_partition_get(it);
    Serial.printf("[PART] %s,%u,0x%02x,0x%06lx,%lu\n", p->label, (unsigned)p->type, (unsigned)p->subtype,
                  (unsigned long)p->address, (unsigned long)(p->size / 1024));
  }
  esp_partition_iterator_release(it);  // aceita nullptr
}

void imprimirMac(const char* nome, esp_mac_type_t tipo) {
  uint8_t mac[6];
  esp_read_mac(mac, tipo);
  Serial.printf("[MAC] %s " MACSTR "\n", nome, MAC2STR(mac));
}

// Um registro (lib/registro) por pacote aceito; duplicados e antigos não entram.
// Linha: [FS] gravado,segmento,posicao,us (posicao = índice do registro no segmento).
// Sem histórico (LittleFS não montado) ou com a gravação falhando, o registro
// ainda atualiza a última leitura em RAM (o app continua com o valor atual),
// mas devolve false: a borda não avança e o nó continua com a leitura.
bool gravarHistorico(const radio::Recebido& r, const protocolo::PacoteLeitura& p, protocolo::Classe c,
                     uint32_t perdidos) {
  relogio::Instante agora = relogio::agora();
  registro::Metadados m{};
  m.horaValida = agora.valida;
  m.fonteRelogio = static_cast<uint8_t>(agora.fonte);
  m.bootCoord = agora.boot;
  m.utc_s = agora.utc_s;
  m.desdeBoot_ms = agora.desdeBoot_ms;
  memcpy(m.mac, r.mac, sizeof(m.mac));
  m.rssi = r.rssi;
  m.ruido = r.ruido;
  m.classe = c;
  m.perdidos = perdidos;
  registro::Registro reg = registro::montar(m, p);
  if (historico::montado() && historico::gravar(reg)) {
    const anel::Resumo* s = historico::indice().atual();
    Serial.printf("[FS] gravado,%lu,%lu,%lu\n", (unsigned long)s->numero, (unsigned long)(s->posicoes - 1),
                  (unsigned long)historico::ultimaGravacaoUs());
    return true;
  }
  consulta::observar(reg);  // gravar() chama o observador só quando grava
  return false;
}

// Confirmação para o nó: "tudo até a borda está gravado" (ou NADA_GRAVADO).
// Vai ANTES de gravar o pacote atual (opção b do passo 2): confirma o que já
// estava na flash. Devolve o texto da linha [CSV] ("boot:seq", "nada" ou "erro").
void responder(nos::No& n, const uint8_t mac[6], char (&texto)[24]) {
  protocolo::Confirmacao c{};
  c.versao = protocolo::VERSAO;
  c.tipo = static_cast<uint8_t>(protocolo::Tipo::CONFIRMACAO);
  if (n.sequencia.temBorda()) {
    c.boot = n.sequencia.bordaBoot();
    c.seq = n.sequencia.bordaSeq();
  } else {
    c.flags = protocolo::flag_conf::NADA_GRAVADO;
  }

  // Modos de teste (config.h): nada disso é gravado no histórico.
  static const uint8_t CHAVE_ERRADA[protocolo::TAMANHO_CHAVE] = {};
  const uint8_t* chave = n.chave;
  if (config::TESTE_CONFIRMACAO_MODO == 1) chave = CHAVE_ERRADA;
  if (config::TESTE_CONFIRMACAO_MODO == 2) {
    if (!n.temConfAntiga) {
      n.confAntiga = c;
      n.temConfAntiga = true;
    }
    c = n.confAntiga;
  }
  if (config::TESTE_CONFIRMACAO_MODO == 3 && n.sequencia.temBorda()) c.seq += 1000;

  protocolo::ConfirmacaoAssinada a;
  bool ok = n.temChave && protocolo::assinarConfirmacao(c, chave, protocolo::hmacSha256, a);
  uint8_t bytes[sizeof(a)];
  memcpy(bytes, &a, sizeof(bytes));
  ok = ok && radio::enviarConfirmacao(mac, bytes, sizeof(bytes));
  if (!ok) snprintf(texto, sizeof(texto), "erro");
  else if (c.flags & protocolo::flag_conf::NADA_GRAVADO) snprintf(texto, sizeof(texto), "nada");
  else snprintf(texto, sizeof(texto), "%u:%lu", c.boot, (unsigned long)c.seq);
}

void processar(const radio::Recebido& r) {
  nos::No* n = nos::buscar(r.mac);
  if (n == nullptr) {
    Serial.printf("[ESPNOW] tabela de nos cheia, pacote de " MACSTR " ignorado\n", MAC2STR(r.mac));
    return;
  }
  nos::contarRecepcao(*n, r.rssi);

  protocolo::PacoteLeitura p{};
  protocolo::Rejeicao rej =
      protocolo::validar(r.dados, r.tamanho, n->temChave ? n->chave : nullptr, protocolo::hmacSha256, p);
  Serial.printf("[CSV] %lu," MACSTR ",%s,%d,%d,%u,%s", millis(), MAC2STR(r.mac), n->nome, r.rssi, r.ruido,
                r.tamanhoOriginal, protocolo::nomeRejeicao(rej));
  if (rej != protocolo::Rejeicao::NENHUMA) {
    n->rejeitados++;
    Serial.println(",,,,,,,,,,,,,,,");  // 15 campos vazios: classe, perdidos, os 11 do pacote, idade e conf
    return;
  }

  // Etapa 7.1: classifica sem mover a borda, responde e só então grava.
  uint32_t lacuna = 0;
  bool semAnteriores = (p.flags & protocolo::flag::SEM_ANTERIORES) != 0;
  protocolo::Classe c = n->sequencia.classificar(p.boot, p.seq, semAnteriores, lacuna);
  char conf[24];
  responder(*n, r.mac, conf);
  if (p.flags & protocolo::flag::ANTERIOR_SEM_ACK) n->semAck++;
  Serial.printf(",%s,%lu,%u,%lu,%d,%u,%u,%u,%u,%u,%u,%u,%u,%lu,%s\n", protocolo::nomeClasse(c),
                (unsigned long)lacuna, p.boot, (unsigned long)p.seq, p.temperatura_c100, p.umidade_ar_c100,
                p.solo_mv, p.alimentacao_mv, p.estados, p.motivo_boot, p.acordado_ant_ms, p.tentativas_ant, p.flags,
                (unsigned long)p.idade_s, conf);

  if (protocolo::aceitar(c) && gravarHistorico(r, p, c, lacuna)) n->sequencia.confirmar(p.boot, p.seq, c, lacuna);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[BOOT] coordenador - etapa 7.1");
  if (config::TESTE_CONFIRMACAO_MODO != 0)
    Serial.printf("[BOOT] MODO DE TESTE: confirmacao %s\n",
                  config::TESTE_CONFIRMACAO_MODO == 1   ? "forjada (chave errada)"
                  : config::TESTE_CONFIRMACAO_MODO == 2 ? "antiga (sempre a primeira)"
                                                        : "alem do gerado (+1000)");
  relogio::iniciar();
  imprimirParticoes();
  imprimirMac("STA", ESP_MAC_WIFI_STA);
  imprimirMac("AP ", ESP_MAC_WIFI_SOFTAP);
  Serial.printf("[BOOT] protocolo v%u (%u bytes, assinatura HMAC-SHA256 de %u)\n", protocolo::VERSAO,
                (unsigned)sizeof(protocolo::PacoteAssinado), (unsigned)protocolo::TAMANHO_ASSINATURA);
  if (!nos::chaveMestraValida())
    Serial.println("[BOOT] ERRO: CHAVE_MESTRA nao gerada (ferramentas/chaves.py mestra); todo pacote sera recusado");
  configuracao::iniciar();  // antes do histórico: os nós da carga já recebem o nome do cadastro
  historico::iniciar(consulta::observar);  // antes do rádio: a leitura dos segmentos não disputa com a fila do ESP-NOW
  if (!radio::iniciar()) Serial.println("[BOOT] ERRO: radio nao iniciou");
  // conf = confirmação enviada ao nó ("boot:seq" gravado até, "nada" ou "erro"); ver docs/protocolo.md, seção 13.
  Serial.println("[CSV] t_ms,mac,nome,rssi,ruido,len,validacao,classe,perdidos,boot,seq,temp_c100,ur_c100,"
                 "solo_mv,alim_mv,estados,motivo_boot,acordado_ant_ms,tent_ant,flags,idade_s,conf");
  Serial.println("[RESUMO] t_ms,mac,nome,recebidos,rejeitados,aceitos,perdidos,descartados,reinicios,sem_ack,"
                 "entrega_pct,rssi_medio,rssi_min,rssi_max,fora_de_ordem");
}

void loop() {
  radio::Recebido r;
  if (radio::receber(r, 100)) processar(r);
  lerConsole();
  exportar::passo();  // uma página por volta: a fila do ESP-NOW continua sendo atendida

  static uint32_t descartadosAntes = 0;
  uint32_t d = radio::descartadosFilaCheia();
  if (d != descartadosAntes) {
    Serial.printf("[ESPNOW] fila cheia: %lu pacotes descartados no total\n", (unsigned long)d);
    descartadosAntes = d;
  }

  static uint32_t proximoResumo = config::INTERVALO_RESUMO_MS;
  if ((int32_t)(millis() - proximoResumo) >= 0) {
    proximoResumo += config::INTERVALO_RESUMO_MS;
    nos::imprimirResumo();
  }
}
