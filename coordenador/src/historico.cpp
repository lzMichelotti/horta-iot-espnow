#include "historico.h"

#include <LittleFS.h>
#include <algorithm>
#include <cstring>

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

constexpr size_t TAM_CAMINHO = 32;
void caminho(uint32_t numero, char (&saida)[TAM_CAMINHO]) {
  char nome[anel::TAM_NOME];
  anel::nomeSegmento(numero, nome);
  snprintf(saida, TAM_CAMINHO, "%s/%s", config::HIST_DIR, nome);
}

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

}  // namespace

bool iniciar() {
  // begin(formatOnFail, basePath, maxOpenFiles (ignorado pelo núcleo), partitionLabel)
  fsMontado = LittleFS.begin(false, config::FS_PONTO, 5, config::FS_ROTULO);
  if (!fsMontado) {
    Serial.println("[FS] ERRO: LittleFS nao montou (particao vazia ou corrompida). "
                   "Historico desligado; 'fs formatar' formata e monta.");
    return false;
  }
  Serial.printf("[FS] LittleFS montado: %u KB usados de %u KB\n", (unsigned)(LittleFS.usedBytes() / 1024),
                (unsigned)(LittleFS.totalBytes() / 1024));
  carregar();
  return true;
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
  if (p.abrirNovo) tabela.abrirNovo(p.numeroNovo);  // o arquivo nasce no open em modo "a"

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
    if (escritos > 0) tabela.fecharAtual();  // sobra parcial no fim: não anexar mais a este
    Serial.printf("[FS] ERRO: gravacao em %s (%u de %u bytes)\n", c, (unsigned)escritos, (unsigned)TAM_REGISTRO);
    return false;
  }
  tabela.registrarGravacao(r);
  gravacoes++;
  somaUs += ultimaUs;
  if (ultimaUs > maximaUs) maximaUs = ultimaUs;
  return true;
}

void imprimirEstado() {
  if (!fsMontado) {
    Serial.println("[FS] nao montado");
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
    if (ok) iniciar();
  } else if (strncmp(argumento, "gravar ", 7) == 0) {
    long n = atol(argumento + 7);
    if (n <= 0 || n > 100000) {
      Serial.println("[FS] uso: fs gravar N  (1 a 100000 registros sinteticos, MAC 02:00:00:00:00:00)");
      return;
    }
    uint32_t t0 = millis();
    long ok = 0;
    for (long i = 0; i < n; i++) {
      if (gravar(sintetico())) ok++;
      if ((i & 63) == 63) delay(1);  // deixa a tarefa ociosa rodar (watchdog)
    }
    Serial.printf("[FS] %ld de %ld registros sinteticos gravados em %lu ms\n", ok, n, (unsigned long)(millis() - t0));
    imprimirEstado();
  } else {
    Serial.println("[FS] uso: fs | fs listar | fs formatar | fs gravar N");
  }
}

}  // namespace historico
