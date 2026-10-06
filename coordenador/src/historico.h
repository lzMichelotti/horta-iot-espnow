// Histórico de leituras no LittleFS: segmentos numerados em anel
// (/littlefs/h/NNNNNNNN.seg), registros de 48 bytes (lib/registro).
// A lógica de rotação fica em lib/anel (testada no PC); aqui só as operações
// de arquivo. Detalhes e justificativas: docs/persistencia.md.
//
// LittleFS do Arduino-ESP32 (componente joltwallet/littlefs):
// https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-guides/file-system-considerations.html
#pragma once

#include <Arduino.h>

#include <anel.h>
#include <registro.h>

namespace historico {

// Chamado para cada registro válido, na ordem de gravação: na carga do boot e
// depois de cada gravação. Mantém as âncoras de hora e a última leitura por nó.
using Observador = void (*)(const registro::Registro& r);

// Monta o LittleFS e lê todos os segmentos para montar a tabela de resumos.
// Chamar no setup() ANTES de ligar o rádio (a leitura pode levar segundos).
// false se não montou: nada é formatado automaticamente (política: passo 9);
// o comando "fs formatar" formata à mão.
bool iniciar(Observador observador);

bool montado();

// Anexa um registro ao segmento atual, abrindo/apagando segmentos conforme a
// política. false se o LittleFS não está montado ou a escrita falhou.
bool gravar(const registro::Registro& r);

const anel::Indice& indice();

// Caminho completo do arquivo de um segmento ("/h/00000123.seg").
constexpr size_t TAM_CAMINHO = 32;
void caminhoSegmento(uint32_t numero, char (&saida)[TAM_CAMINHO]);

// Duração da última gravação (abrir + escrever + fechar), µs.
uint32_t ultimaGravacaoUs();

// Comando "fs" da serial: estado | listar | formatar | gravar N
// (e, só no env teste_gravacao, "encher N M").
void comando(const char* argumento);

void imprimirEstado();

}  // namespace historico
