#include "historico.h"

#include <LittleFS.h>
#include <algorithm>
#include <cstring>
#include <esp_partition.h>

#include "config.h"
#include "relogio.h"

namespace historico {

namespace {

constexpr size_t TAM_REGISTRO = sizeof(registro::Registro);
constexpr size_t REGISTROS_POR_LEITURA = 16;  // buffer de 768 B na leitura dos segmentos
constexpr size_t MAX_ARQUIVOS = 2 * anel::CAPACIDADE;  // na varredura do diretório

anel::Indice tabela({config::HIST_REGISTROS_POR_SEGMENTO, config::HIST_MAX_SEGMENTOS});
bool fsMontado = false;

// Instrumentação das gravações (passo 6).
uint32_t gravacoes = 0;
uint32_t falhas = 0;
uint32_t ultimaUs = 0;
uint32_t maximaUs = 0;
uint64_t somaUs = 0;

Observador observador = nullptr;

// Estado do armazenamento (passo 9).
bool naoMontado = false;       // não montou e não foi formatado (corrompido ou sem partição)
bool formatadoNoBoot = false;  // partição sem LittleFS, formatada automaticamente neste boot
bool espacoBaixo = false;      // a rotação de emergência não conseguiu a margem livre
uint32_t falhasSeguidas = 0;
uint32_t rotacoesEmergencia = 0;

void caminho(uint32_t numero, char (&saida)[TAM_CAMINHO]) { caminhoSegmento(numero, saida); }

// Lê o segmento inteiro, em partes, e monta o resumo.
bool resumir(uint32_t numero, anel::Resumo& s, uint32_t& invalidos) {
  char c[TAM_CAMINHO];
  caminho(numero, c);
  File f = LittleFS.open(c, FILE_READ);
  if (!f) return false;

  s = anel::Resumo{};
  s.numero = numero;
  size_t tamanho = f.size();
  size_t restantes = tamanho / TAM_REGISTRO;
  static uint8_t buf[REGISTROS_POR_LEITURA * TAM_REGISTRO];
  while (restantes > 0) {
    size_t n = std::min(restantes, REGISTROS_POR_LEITURA);
    if (f.read(buf, n * TAM_REGISTRO) != n * TAM_REGISTRO) {
      s.fechado = true;  // leitura curta: não anexar a este arquivo
      break;
    }
    for (size_t i = 0; i < n; i++) {
      registro::Registro r{};
      bool ok = registro::ler(buf + i * TAM_REGISTRO, TAM_REGISTRO, r) == registro::Erro::NENHUM;
      if (!ok) invalidos++;
      anel::acumular(s, r, ok);
      if (ok && observador != nullptr) observador(r);
    }
    restantes -= n;
  }
  f.close();

  if (tamanho % TAM_REGISTRO != 0) {
    s.fechado = true;
    Serial.printf("[FS] %s: %u bytes sobrando no fim (escrita parcial?); segmento fechado\n", c,
                  (unsigned)(tamanho % TAM_REGISTRO));
  }
  return true;
}

// Lista /h, ordena os números e lê cada segmento.
void carregar() {
  uint32_t t0 = millis();
  tabela.limpar();
  if (!LittleFS.exists(config::HIST_DIR) && !LittleFS.mkdir(config::HIST_DIR)) {
    Serial.printf("[FS] ERRO: nao criou %s\n", config::HIST_DIR);
    return;
  }

  static uint32_t numeros[MAX_ARQUIVOS];
  size_t n = 0;
  File dir = LittleFS.open(config::HIST_DIR);
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    uint32_t numero = 0;
    if (!f.isDirectory() && anel::interpretarNome(f.name(), numero)) {
      if (n < MAX_ARQUIVOS) numeros[n++] = numero;
      else Serial.printf("[FS] mais de %u segmentos; %s ignorado\n", (unsigned)MAX_ARQUIVOS, f.name());
    }
    f.close();
  }
  dir.close();
  std::sort(numeros, numeros + n);

  // Mais arquivos do que cabem na tabela (não deveria acontecer): apaga os mais antigos.
  size_t inicio = 0;
  while (n - inicio > anel::CAPACIDADE) {
    char c[TAM_CAMINHO];
    caminho(numeros[inicio++], c);
    LittleFS.remove(c);
    Serial.printf("[FS] excesso de segmentos: %s apagado\n", c);
  }

  uint32_t invalidos = 0;
  for (size_t i = inicio; i < n; i++) {
    anel::Resumo s;
    if (resumir(numeros[i], s, invalidos)) tabela.adicionar(s);
  }
  Serial.printf("[FS] carga: %u segmentos, %lu registros validos, %lu invalidos, em %lu ms\n",
                (unsigned)tabela.quantidade(), (unsigned long)tabela.totalValidos(), (unsigned long)invalidos,
                (unsigned long)(millis() - t0));
}

bool apagarSegmento(uint32_t numero) {
  char c[TAM_CAMINHO];
  caminho(numero, c);
  bool ok = LittleFS.remove(c);
  Serial.printf("[FS] rotacao: %s apagado%s\n", c, ok ? "" : " (FALHOU)");
  return ok;
}

// "the magic string 'littlefs' will always reside at offset=8 in a valid
// littlefs superblock"; o superbloco fica nos blocos 0 e 1 (littlefs SPEC.md).
// Sem a assinatura nos dois, a partição nunca foi formatada: não há o que preservar.
bool temAssinatura(const esp_partition_t* p) {
  constexpr size_t BLOCO = 4096;  // bloco do LittleFS no ESP32 = setor da flash
  for (size_t b = 0; b < 2; b++) {
    char magica[8];
    if (esp_partition_read(p, b * BLOCO + 8, magica, sizeof(magica)) == ESP_OK && memcmp(magica, "littlefs", 8) == 0)
      return true;
  }
  return false;
}

// Rotação de emergência: antes de abrir um segmento novo, garante a margem
// livre apagando os mais antigos. O segmento novo ainda não existe, então
// nenhum dos apagados é o que vai receber o registro.
void garantirEspaco() {
  size_t total = LittleFS.totalBytes();
  size_t livre = total - LittleFS.usedBytes();
  while (livre < config::HIST_MARGEM_LIVRE && tabela.quantidade() > 0) {
    Serial.printf("[FS] alerta: espaco baixo (%u KB livres); rotacao de emergencia\n", (unsigned)(livre / 1024));
    apagarSegmento(tabela.em(0).numero);
    tabela.removerMaisAntigo();
    rotacoesEmergencia++;
    livre = total - LittleFS.usedBytes();
  }
  espacoBaixo = livre < config::HIST_MARGEM_LIVRE;
  if (espacoBaixo)
    Serial.printf("[FS] ALERTA: espaco baixo mesmo sem historico antigo (%u KB livres)\n", (unsigned)(livre / 1024));
}

void imprimirLinhaEstado() {
  Serial.printf("[FS] estado: %s (rotacoes de emergencia %lu, falhas seguidas %lu)\n", nomeEstado(estado()),
                (unsigned long)rotacoesEmergencia, (unsigned long)falhasSeguidas);
}

registro::Registro sintetico() {
  // MAC localmente administrado (bit 1 do 1º byte): não é de nenhum nó real.
  static const uint8_t MAC_SINTETICO[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
  static uint32_t seq = 0;
  relogio::Instante i = relogio::agora();
  registro::Metadados m{};
  m.horaValida = i.valida;
  m.fonteRelogio = static_cast<uint8_t>(i.fonte);
  m.bootCoord = i.boot;
  m.utc_s = i.utc_s;
  m.desdeBoot_ms = i.desdeBoot_ms;
  memcpy(m.mac, MAC_SINTETICO, 6);
  m.classe = protocolo::Classe::NOVO;
  protocolo::PacoteLeitura p{};
  p.versao = protocolo::VERSAO;
  p.tipo = static_cast<uint8_t>(protocolo::Tipo::LEITURA);
  p.seq = seq++;
  return registro::montar(m, p);
}

void imprimirTabela() {
  Serial.println("[FS] segmento,posicoes,validos,sem_hora,utc_min,utc_max,boot_primeiro,boot_ultimo,fechado");
  for (size_t i = 0; i < tabela.quantidade(); i++) {
    const anel::Resumo& s = tabela.em(i);
    Serial.printf("[FS] %lu,%lu,%lu,%lu,%lu,%lu,%u,%u,%d\n", (unsigned long)s.numero, (unsigned long)s.posicoes,
                  (unsigned long)s.validos, (unsigned long)s.semHora, (unsigned long)s.utcMin,
                  (unsigned long)s.utcMax, s.bootPrimeiro, s.bootUltimo, s.fechado);
  }
}

#ifdef TESTE_GRAVACAO
// Histórico sintético realista para testar consultas sem esperar horas:
// M nós (MAC 02:00:00:00:00:0k), uma leitura por nó a cada 15 min, um "boot do
// coordenador" (1000, 1001, ...) a cada 200 rodadas (50 h). Nos boots múltiplos
// de 3, as 8 primeiras rodadas ficam sem hora (reconstruíveis pelo mesmo boot);
// o boot 1004 fica inteiro sem hora (sem âncora). O arquivo do segmento fica
// aberto enquanto ele enche: um sync por segmento, não por registro.
void encher(uint32_t n, uint32_t m) {
  constexpr uint32_t RODADAS_POR_BOOT = 200;
  relogio::Instante agora = relogio::agora();
  int64_t base = (agora.valida ? agora.utc_s : 1791000000) - static_cast<int64_t>(n / m + 1) * 900;
  uint32_t t0 = millis();
  File f;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t k = i % m + 1, rodada = i / m;
    uint16_t boot = static_cast<uint16_t>(1000 + rodada / RODADAS_POR_BOOT);
    int64_t utc = base + static_cast<int64_t>(rodada) * 900 + (k - 1) * 7;
    int64_t inicioBoot = base + static_cast<int64_t>(rodada / RODADAS_POR_BOOT) * RODADAS_POR_BOOT * 900 - 30;

    registro::Metadados md{};
    md.horaValida = !(boot == 1004 || (boot % 3 == 0 && rodada % RODADAS_POR_BOOT < 8));
    md.fonteRelogio = md.horaValida ? 1 : 0;
    md.bootCoord = boot;
    md.utc_s = utc;
    md.desdeBoot_ms = static_cast<uint64_t>(utc - inicioBoot) * 1000;
    const uint8_t mac[6] = {0x02, 0, 0, 0, 0, static_cast<uint8_t>(k)};
    memcpy(md.mac, mac, 6);
    md.rssi = static_cast<int8_t>(-40 - static_cast<int>(k));
    md.ruido = -96;
    md.classe = protocolo::Classe::NOVO;
    protocolo::PacoteLeitura p{};
    p.versao = protocolo::VERSAO;
    p.tipo = static_cast<uint8_t>(protocolo::Tipo::LEITURA);
    p.boot = 1;
    p.seq = rodada;
    p.temperatura_c100 = static_cast<int16_t>(2000 + (rodada % 96) * 10);
    p.umidade_ar_c100 = 6000;
    p.solo_mv = static_cast<uint16_t>(900 + (rodada % 300));
    p.alimentacao_mv = 3300;
    registro::Registro r = registro::montar(md, p);

    anel::Plano pl = tabela.planejar();
    if ((pl.apagar > 0 || pl.abrirNovo) && f) f.close();
    for (uint32_t a = 0; a < pl.apagar; a++) {
      apagarSegmento(tabela.em(0).numero);
      tabela.removerMaisAntigo();
    }
    if (pl.abrirNovo) tabela.abrirNovo(pl.numeroNovo);
    if (!f) {
      char c[TAM_CAMINHO];
      caminho(tabela.atual()->numero, c);
      f = LittleFS.open(c, FILE_APPEND);
    }
    if (f.write(reinterpret_cast<const uint8_t*>(&r), TAM_REGISTRO) != TAM_REGISTRO) {
      Serial.println("[FS] ERRO no enchimento");
      break;
    }
    tabela.registrarGravacao(r);
    if (observador != nullptr) observador(r);
    if ((i & 255) == 255) delay(1);
  }
  if (f) f.close();
  Serial.printf("[FS] enchimento: %lu registros de %lu nos em %lu ms\n", (unsigned long)n, (unsigned long)m,
                (unsigned long)(millis() - t0));
}
#endif

}  // namespace

void caminhoSegmento(uint32_t numero, char (&saida)[TAM_CAMINHO]) {
  char nome[anel::TAM_NOME];
  anel::nomeSegmento(numero, nome);
  snprintf(saida, TAM_CAMINHO, "%s/%s", config::HIST_DIR, nome);
}

bool iniciar(Observador obs) {
  observador = obs;
  naoMontado = formatadoNoBoot = false;
  // begin(formatOnFail, basePath, maxOpenFiles (ignorado pelo núcleo), partitionLabel).
  // formatOnFail = false: a decisão de formatar é tomada aqui (política do passo 9).
  fsMontado = LittleFS.begin(false, config::FS_PONTO, 5, config::FS_ROTULO);
  if (!fsMontado) {
    const esp_partition_t* p =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, config::FS_ROTULO);
    if (p == nullptr) {
      Serial.printf("[FS] ERRO: particao '%s' nao existe (tabela de particoes errada?)\n", config::FS_ROTULO);
    } else if (!temAssinatura(p)) {
      Serial.println("[FS] particao sem LittleFS (nunca formatada): formatando automaticamente");
      uint32_t t0 = millis();
      // O begin() acima já registrou o rótulo da partição, que o format() usa.
      if (LittleFS.format()) fsMontado = LittleFS.begin(false, config::FS_PONTO, 5, config::FS_ROTULO);
      formatadoNoBoot = fsMontado;
      Serial.printf("[FS] formatacao automatica: %s em %lu ms\n", fsMontado ? "ok" : "FALHOU",
                    (unsigned long)(millis() - t0));
    } else {
      Serial.println("[FS] ALERTA: o LittleFS existe mas nao montou (corrompido). NAO foi formatado: o historico "
                     "fica preservado para diagnostico (esptool read-flash). 'fs formatar' formata.");
    }
  }
  if (!fsMontado) {
    naoMontado = true;
    imprimirLinhaEstado();
    return false;
  }
  Serial.printf("[FS] LittleFS montado: %u KB usados de %u KB\n", (unsigned)(LittleFS.usedBytes() / 1024),
                (unsigned)(LittleFS.totalBytes() / 1024));
  carregar();
  imprimirLinhaEstado();
  return true;
}

Estado estado() {
  if (naoMontado) return Estado::NAO_MONTADO;
  if (falhasSeguidas >= config::HIST_FALHAS_ALERTA) return Estado::FALHAS_DE_GRAVACAO;
  if (espacoBaixo) return Estado::ESPACO_BAIXO;
  if (formatadoNoBoot) return Estado::FORMATADO_NO_BOOT;
  return Estado::OK;
}

const char* nomeEstado(Estado e) {
  switch (e) {
    case Estado::OK: return "ok";
    case Estado::FORMATADO_NO_BOOT: return "formatado_no_boot";
    case Estado::ESPACO_BAIXO: return "espaco_baixo";
    case Estado::FALHAS_DE_GRAVACAO: return "falhas_de_gravacao";
    case Estado::NAO_MONTADO: return "nao_montado";
  }
  return "?";
}

bool montado() { return fsMontado; }

const anel::Indice& indice() { return tabela; }

uint32_t ultimaGravacaoUs() { return ultimaUs; }

bool gravar(const registro::Registro& r) {
  if (!fsMontado) return false;
  uint32_t t0 = micros();

  anel::Plano p = tabela.planejar();
  for (uint32_t i = 0; i < p.apagar; i++) {
    // Mesmo se a remoção falhar, sai da tabela: senão a rotação travaria aqui.
    apagarSegmento(tabela.em(0).numero);
    tabela.removerMaisAntigo();
  }
  if (p.abrirNovo) {
    garantirEspaco();
    tabela.abrirNovo(p.numeroNovo);  // o arquivo nasce no open em modo "a"
  }

  char c[TAM_CAMINHO];
  caminho(tabela.atual()->numero, c);
  File f = LittleFS.open(c, FILE_APPEND);
  size_t escritos = 0;
  if (f) {
    escritos = f.write(reinterpret_cast<const uint8_t*>(&r), TAM_REGISTRO);
    f.close();  // close = sync: o registro só fica na flash depois daqui
  }

  ultimaUs = micros() - t0;
  if (escritos != TAM_REGISTRO) {
    falhas++;
    falhasSeguidas++;
    if (escritos > 0) tabela.fecharAtual();  // sobra parcial no fim: não anexar mais a este
    Serial.printf("[FS] ERRO: gravacao em %s (%u de %u bytes)\n", c, (unsigned)escritos, (unsigned)TAM_REGISTRO);
    return false;
  }
  tabela.registrarGravacao(r);
  if (observador != nullptr) observador(r);
  falhasSeguidas = 0;
  gravacoes++;
  somaUs += ultimaUs;
  if (ultimaUs > maximaUs) maximaUs = ultimaUs;
  return true;
}

void imprimirEstado() {
  if (!fsMontado) {
    Serial.println("[FS] nao montado");
    imprimirLinhaEstado();
    return;
  }
  const anel::Politica& pol = tabela.politica();
  Serial.printf("[FS] %u KB usados de %u KB; politica: %lu registros/segmento, max %lu segmentos\n",
                (unsigned)(LittleFS.usedBytes() / 1024), (unsigned)(LittleFS.totalBytes() / 1024),
                (unsigned long)pol.registrosPorSegmento, (unsigned long)pol.maxSegmentos);
  const anel::Resumo* a = tabela.atual();
  Serial.printf("[FS] segmentos: %u (%lu a %lu), registros validos: %lu\n", (unsigned)tabela.quantidade(),
                (unsigned long)(tabela.quantidade() ? tabela.em(0).numero : 0), (unsigned long)(a ? a->numero : 0),
                (unsigned long)tabela.totalValidos());
  Serial.printf("[FS] gravacoes neste boot: %lu ok, %lu falhas; tempo (us): ultima %lu, media %lu, max %lu\n",
                (unsigned long)gravacoes, (unsigned long)falhas, (unsigned long)ultimaUs,
                (unsigned long)(gravacoes ? somaUs / gravacoes : 0), (unsigned long)maximaUs);
  imprimirLinhaEstado();
}

void comando(const char* argumento) {
  if (argumento == nullptr || argumento[0] == '\0') {
    imprimirEstado();
  } else if (strcmp(argumento, "listar") == 0) {
    imprimirTabela();
  } else if (strcmp(argumento, "formatar") == 0) {
    Serial.println("[FS] formatando a particao (apaga todo o historico)...");
    uint32_t t0 = millis();
    if (fsMontado) LittleFS.end();
    fsMontado = false;
    // format() procura a partição pelo rótulo passado no begin(); chamar begin
    // antes garante o rótulo certo mesmo se a montagem falhar.
    LittleFS.begin(false, config::FS_PONTO, 5, config::FS_ROTULO);
    bool ok = LittleFS.format();
    Serial.printf("[FS] format: %s em %lu ms\n", ok ? "ok" : "FALHOU", (unsigned long)(millis() - t0));
    if (ok) iniciar(observador);
  } else if (strncmp(argumento, "gravar ", 7) == 0) {
    long n = 0;
    char v = 0;
    int lidos = sscanf(argumento + 7, "%ld %c", &n, &v);
    bool detalhar = lidos == 2 && v == 'v';  // "v": uma linha por registro confirmado (teste de queda)
    if (lidos < 1 || n <= 0 || n > 100000 || (lidos == 2 && !detalhar)) {
      Serial.println("[FS] uso: fs gravar N [v]  (1 a 100000 registros sinteticos, MAC 02:00:00:00:00:00)");
      return;
    }
    uint32_t t0 = millis();
    long ok = 0;
    for (long i = 0; i < n; i++) {
      if (gravar(sintetico())) {
        ok++;
        if (detalhar)
          Serial.printf("[FS] gravado,%lu,%lu,%lu\n", (unsigned long)tabela.atual()->numero,
                        (unsigned long)(tabela.atual()->posicoes - 1), (unsigned long)ultimaUs);
      }
      if ((i & 63) == 63) delay(1);  // deixa a tarefa ociosa rodar (watchdog)
    }
    Serial.printf("[FS] %ld de %ld registros sinteticos gravados em %lu ms\n", ok, n, (unsigned long)(millis() - t0));
    imprimirEstado();
#ifdef TESTE_GRAVACAO
  } else if (strncmp(argumento, "encher ", 7) == 0) {
    unsigned long n = 0, m = 0;
    if (sscanf(argumento + 7, "%lu %lu", &n, &m) != 2 || n == 0 || n > 100000 || m == 0 || m > 6) {
      Serial.println("[FS] uso: fs encher N M  (N registros de M nos sinteticos, M de 1 a 6)");
      return;
    }
    encher(n, m);
    imprimirEstado();
  } else if (strncmp(argumento, "ocupar ", 7) == 0) {
    // Arquivo de enchimento fora de /h, para testar o espaço baixo. Fecha
    // (commit) a cada 64 KB: se a escrita bater em ENOSPC, o LittleFS descarta
    // tudo o que não passou pelo commit, inclusive o que já foi escrito.
    long kb = atol(argumento + 7);
    static uint8_t zeros[4096] = {};
    long escritos = 0;
    bool cheio = false;
    while (!cheio && escritos + 64 <= kb) {
      File f = LittleFS.open("/ocupar.bin", FILE_APPEND);
      if (!f) break;
      for (int i = 0; i < 16; i++)
        if (f.write(zeros, sizeof(zeros)) != sizeof(zeros)) {
          cheio = true;
          break;
        }
      f.close();
      if (!cheio) escritos += 64;
    }
    Serial.printf("[FS] /ocupar.bin: +%ld KB\n", escritos);
    imprimirEstado();
  } else if (strcmp(argumento, "liberar") == 0) {
    LittleFS.remove("/ocupar.bin");
    imprimirEstado();
#endif
  } else {
    Serial.println("[FS] uso: fs | fs listar | fs formatar | fs gravar N");
  }
}

}  // namespace historico
