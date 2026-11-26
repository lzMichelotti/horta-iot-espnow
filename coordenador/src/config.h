// Configuração do coordenador: Wi-Fi, ESP-NOW, fila de recepção, relógio e console.
// Justificativas em docs/comunicacao.md. SSID e senha ficam em segredos.h (fora do Git).
#pragma once

#include <cstddef>
#include <cstdint>

namespace config {

// ---------------------------------------------------------------- Wi-Fi / SoftAP
// Canal fixo do SoftAP; o ESP-NOW transmite e recebe no canal da interface,
// então os nós usam o MESMO valor (no_sensor/src/config.h).
// 1, 6 e 11 são os canais de 2,4 GHz que não se sobrepõem.
constexpr uint8_t CANAL_WIFI = 1;
constexpr uint8_t AP_MAX_CONEXOES = 4;   // celulares do app (etapa 8)

// Varredura de redes no boot, antes de subir o AP: registra a ocupação de cada
// canal no local (dado para justificar o canal escolhido). Leva alguns segundos.
constexpr bool VARRER_CANAIS_NO_BOOT = true;

// ---------------------------------------------------------------- ESP-NOW
// Itens da fila entre o callback de recepção (tarefa do Wi-Fi) e o loop().
constexpr size_t FILA_ESPNOW_ITENS = 16;

// ---------------------------------------------------------------- Relógio
// Faixa plausível para o comando "hora" (UTC, segundos desde 1970).
// Antes de 2026 só pode ser erro de digitação; 2100 fica abaixo do limite de
// um uint32 (2106), caso o registro guarde a hora em 32 bits.
constexpr int64_t HORA_MINIMA_UTC = 1767225600;  // 2026-01-01T00:00:00Z
constexpr int64_t HORA_MAXIMA_UTC = 4102444800;  // 2100-01-01T00:00:00Z

// ---------------------------------------------------------------- Histórico (LittleFS)
// Organização, retenção e capacidade: docs/persistencia.md.
constexpr const char* FS_ROTULO = "littlefs";  // rótulo da partição em particoes.csv
constexpr const char* FS_PONTO = "/littlefs";  // ponto de montagem no VFS
constexpr const char* HIST_DIR = "/h";
// Geração do histórico (8 hex): nasce com o LittleFS e some quando ele é formatado.
constexpr const char* HIST_GERACAO = "/geracao";
// 470 registros × 52 B = 24 440 B: cabem em 6 blocos de 4 KB com os ponteiros
// da lista CTZ do LittleFS (32 B). 56 segmentos ≈ 75 % da partição de 1920 KB;
// o resto é folga do LittleFS. Com 2 nós a cada 15 min: ~137 dias de histórico.
// (Etapa 7: 500 × 48 B; o registro passou a 52 B na Etapa 7.1.)
// O env teste_rotacao reduz os dois valores para forçar a rotação na bancada.
#ifdef HIST_TESTE_REGISTROS_POR_SEGMENTO
constexpr uint32_t HIST_REGISTROS_POR_SEGMENTO = HIST_TESTE_REGISTROS_POR_SEGMENTO;
constexpr uint32_t HIST_MAX_SEGMENTOS = HIST_TESTE_MAX_SEGMENTOS;
#else
constexpr uint32_t HIST_REGISTROS_POR_SEGMENTO = 470;
constexpr uint32_t HIST_MAX_SEGMENTOS = 56;
#endif
// Rotação de emergência (passo 9): antes de abrir um segmento novo, sobra pelo
// menos esta margem livre (2 segmentos de 6 blocos de 4 KB). Sai da folga de
// ~29 % que a rotação normal já deixa: não reduz a retenção.
constexpr size_t HIST_MARGEM_LIVRE = 2 * 6 * 4096;
// Gravações seguidas com erro até o estado virar FALHAS_DE_GRAVACAO.
constexpr uint32_t HIST_FALHAS_ALERTA = 3;

// ---------------------------------------------------------------- Confirmação (Etapa 7.1)
// Modos de teste da segurança da confirmação (env teste_conf_*). O histórico
// grava normalmente; só a resposta ao nó muda. Em nenhum deles o nó pode
// liberar leituras (docs/protocolo.md, seção 13):
//   1 = forjada: assinada com uma chave errada;
//   2 = antiga: repete sempre a primeira confirmação autêntica enviada ao nó;
//   3 = além do gerado: autêntica, mas confirmando seq que o nó ainda não enviou.
#ifdef TESTE_CONFIRMACAO
constexpr uint8_t TESTE_CONFIRMACAO_MODO = TESTE_CONFIRMACAO;
#else
constexpr uint8_t TESTE_CONFIRMACAO_MODO = 0;
#endif

// ---------------------------------------------------------------- API HTTP (Etapa 8, docs/api.md)
constexpr const char* VERSAO_FIRMWARE = "coordenador 8.1";
constexpr uint16_t API_PAGINA_PADRAO = 50;   // leituras por página sem "limite"
constexpr uint16_t API_PAGINA_MAXIMA = 200;  // ~80 KB de JSON (provisório: confirmar com a medição)
// Convivência com o ESP-NOW (passo 5): a COORD monta cada resposta em blocos e
// esvazia a fila do ESP-NOW entre um bloco e outro, para a confirmação ao nó
// sair dentro da espera de 30 ms dele. Medido com o servidor parado durante a
// página inteira: 1/18 confirmações com páginas de 200 (~0,5 s por página).
constexpr uint16_t API_BLOCO_LEITURAS = 10;  // leituras em JSON por bloco (~2,4 ms cada)
constexpr uint16_t API_PAGINA_CSV = 20;      // linhas por bloco na exportação CSV (~1,2 ms cada)
constexpr uint32_t API_CACHE_ESPACO_MS = 30000;  // LittleFS.usedBytes() percorre o sistema de arquivos
// Registros lidos da flash por página, no máximo: com um filtro que descarta
// quase tudo, a página volta com menos itens (fim = false) em vez de segurar o
// loop() lendo o histórico inteiro (~1,45 s para 27 mil registros, Etapa 7).
constexpr uint32_t API_MAX_LIDOS = 2000;

// ---------------------------------------------------------------- Console (serial)
constexpr size_t CONSOLE_LINHA_MAX = 160;  // caracteres por comando (a consulta usa ~110)

// ---------------------------------------------------------------- Nós
// O nó é identificado pelo MAC de origem do quadro ESP-NOW, que é o MAC STA
// do nó (docs/placas.md). MAC fora do cadastro: rastreado como "desconhecido".
//
// SEMENTE do cadastro (Etapa 7, passo 8): usada só quando a NVS ainda não tem
// a tabela (primeiro boot) ou no comando "no padrao". Depois disso vale o que
// estiver na NVS, editado pelos comandos "no" da serial (src/configuracao.cpp).
struct NoCadastrado {
  uint8_t mac[6];
  const char* nome;
  uint16_t mvSeco;   // calibração do solo, mV (0 = sem calibração)
  uint16_t mvUmido;
};
constexpr NoCadastrado NOS[] = {
    // NÓ 1 (serial 5AC9001351). Calibração de BANCADA (docs/sensores.md, 4.3):
    // seco ao toque 1400 mV, ~1 h após a rega 655 mV. Recalibrar no campo.
    {{0x88, 0x57, 0x21, 0x70, 0x93, 0x70}, "no1", 1400, 655},
};
constexpr size_t MAX_NOS = 8;                  // tabela fixa, sem alocação dinâmica
static_assert(MAX_NOS == 8, "o cadastro (lib/cadastro) guarda até 8 nós");
constexpr uint32_t INTERVALO_RESUMO_MS = 60000;  // linha [RESUMO] por nó

// ---------------------------------------------------------------- Alertas (Etapa 9, docs/alertas.md)
// TODO(Etapa 9): TODOS os limiares abaixo são PROVISÓRIOS. Os de agronomia
// (solo, temperatura) ficam a definir com o pessoal do projeto de AUP.
//
// Solo: % pela calibração de CADA nó (cadastro): 0 % = solo seco ao toque
// ("hora de regar"), 100 % = ~1 h após a rega (docs/sensores.md, 4.3). Os
// limiares valem para todos os nós; a calibração é que é por nó.
// Classes (seco, umido, muito_umido) e os dois limites abaixo = os provisórios
// que o app já usava (repositório Hortas_Urbanas, classificacao.ts:
// LIMITES_PROVISORIOS), agora decididos no coordenador.
constexpr int32_t ALERTA_SOLO_SECO_DECIMOS = 250;        // TODO: abaixo de 25 % = seco (e alerta solo_seco). Do app; sem fonte agronômica: a definir com AUP
constexpr int32_t ALERTA_SOLO_MUITO_UMIDO_DECIMOS = 900; // TODO: acima de 90 % = classe muito_umido. Do app; a definir com AUP
constexpr int32_t ALERTA_SOLO_ENCHARCADO_DECIMOS = 1050; // TODO: acima de 105 % = encharcado. Logo após a rega a leitura
                                                         // chegou a ~110 % (584 mV) e voltou a ~100 % em 1 h (sensores.md 4.3–4.4)
constexpr int32_t ALERTA_SOLO_BANDA_DECIMOS = 50;        // TODO: histerese de 5 %. ~12× o ruído da média de 64 leituras
                                                         // (3,2 mV ≈ 0,4 % com a calibração 1400/655; teste 3 de 22/11)
// Temperatura do ar (AHT20).
constexpr int32_t ALERTA_TEMP_ALTA_C100 = 3500;   // TODO: acima de 35 °C. Sem fonte: a definir com AUP
constexpr int32_t ALERTA_TEMP_BAIXA_C100 = 500;   // TODO: abaixo de 5 °C. Sem fonte: a definir com AUP
constexpr int32_t ALERTA_TEMP_BANDA_C100 = 100;   // TODO: histerese de 1 °C, > 3× a precisão do AHT20 (±0,3 °C, datasheet tab. 1)
// Quantas leituras seguidas para ativar (o ciclo do nó é de 15 min).
constexpr uint8_t ALERTA_LEITURAS_PARA_ATIVAR = 2;  // TODO: 2 (30 min), contra uma leitura espúria isolada
constexpr uint8_t ALERTA_LEITURAS_ENCHARCADO = 4;   // TODO: 4 (1 h): a rega deixa o solo acima de 100 % por ~1 h (sensores.md 4.4)
constexpr uint8_t ALERTA_ERROS_PARA_ATIVAR = 2;     // TODO: 2 leituras seguidas com o sensor em erro ou fora de faixa
// TODO(Etapa 5, passo 10): ligar quando o divisor for montado; sem ele, a
// alimentação chega sempre fora_de_faixa e o alerta ficaria ativo o tempo todo.
constexpr bool ALERTA_ERRO_ALIMENTACAO = false;
// Bateria: sem limiar aqui. O modo (normal/economia/crítico) é decidido no nó,
// com a histerese dele (no_sensor/src/config.h: V_TX_MV, V_CRIT_MV), e vem no
// pacote (docs/protocolo.md, seção 14).

// Nó sem comunicar: limite = ALERTA_SEM_COMUNICAR_CICLOS × o período do nó no
// modo de bateria em que ele estava. Os períodos repetem no_sensor/src/config.h
// (PERIODO_CICLO_MS, FATOR_PERIODO_ECONOMIA, PERIODO_CRITICO_MS): mudar lá = mudar aqui.
#ifdef TESTE_ALERTAS  // env teste_alertas: nó nos envs bancada/teste_tensao (10 s; economia ×2; crítico 30 s)
constexpr uint32_t ALERTA_PERIODO_NO_S = 10;
constexpr uint32_t ALERTA_FATOR_ECONOMIA = 2;
constexpr uint32_t ALERTA_PERIODO_CRITICO_S = 30;
#else
constexpr uint32_t ALERTA_PERIODO_NO_S = 15 * 60;
constexpr uint32_t ALERTA_FATOR_ECONOMIA = 4;
constexpr uint32_t ALERTA_PERIODO_CRITICO_S = 3600;
#endif
constexpr uint32_t ALERTA_SEM_COMUNICAR_CICLOS = 3;  // TODO: 3 períodos (45 min): tolera 2 ciclos sem ACK; a fila do
                                                     // nó guarda as leituras e as reenvia quando ele volta (protocolo.md 13)
constexpr uint32_t ALERTA_VERIFICACAO_MS = 5000;     // de quanto em quanto tempo o loop() confere o sem_comunicar

}  // namespace config
