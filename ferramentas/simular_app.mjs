// Simula as telas de sensores e de avisos do app com respostas REAIS do
// coordenador, usando o próprio código do app (repositório Hortas_Urbanas,
// branch feat/coordenador): as funções de classificacao.ts e os títulos de
// avisos-sensores.tsx. Não depende do celular.
//
//   ~/.local/node/bin/node --experimental-strip-types ferramentas/simular_app.mjs \
//       ~/Hortas_Urbanas dados/etapa9/brutos/app
//
// A pasta de respostas vem de ferramentas/ponte_http.py (nos.json, alertas.json).
import { readFileSync } from "node:fs"
import { join } from "node:path"

const [appDir, respostas] = process.argv.slice(2)
const coord = join(appDir, "frontend/src/features/coordenador")
const app = await import(join(coord, "classificacao.ts"))

// Títulos da tela de avisos, lidos do componente (não copiados à mão).
const avisos = readFileSync(join(appDir, "frontend/src/pages/painel/avisos-sensores.tsx"), "utf8")
const bloco = avisos.match(/const TITULO[^{]*\{([^}]*)\}/)
const TITULO = {}
for (const m of (bloco?.[1] ?? "").matchAll(/(\w+):\s*"([^"]*)"/g)) TITULO[m[1]] = m[2]
const PADRAO = avisos.match(/TITULO\[alerta\.tipo\] \?\? "([^"]*)"/)?.[1] ?? "?"

const nos = JSON.parse(readFileSync(join(respostas, "nos.json"), "utf8")).nos
const alertas = JSON.parse(readFileSync(join(respostas, "alertas.json"), "utf8")).alertas

let problemas = 0
const conferir = (ok, msg) => {
  if (!ok) {
    problemas++
    console.log(`  ✗ ${msg}`)
  }
}

console.log("=== Tela de sensores (um cartão por nó)")
for (const no of nos) {
  const l = no.ultima_leitura
  console.log(`- ${no.cadastrado ? no.nome : "Sensor sem nome"} (${no.mac})`)
  console.log(`    ${app.textoVisto(no)}${app.estaParado(no) ? " · PARADO" : ""}`)
  const bateria = app.textoBateria(no)
  console.log(`    bateria: ${bateria ?? "(não mostra)"}   [modo_energia=${JSON.stringify(no.modo_energia)}]`)
  conferir(no.modo_energia == null || bateria != null, `modo_energia "${no.modo_energia}" sem texto no app`)
  if (l) {
    const solo = app.textoSolo(l)
    console.log(`    solo: ${solo}   [solo_classe=${JSON.stringify(l.solo_classe)}, solo_pct=${l.solo_pct}]`)
    conferir(typeof solo === "string" && solo.length > 0, `solo_classe "${l.solo_classe}" sem texto no app`)
    conferir(l.solo_classe == null || app.classeSolo(l) === l.solo_classe, "o app não usou a classe do coordenador")
  }
}

console.log("\n=== Tela de avisos")
if (alertas.length === 0) console.log("  Nenhum aviso por enquanto. A horta está tranquila!")
for (const a of alertas) {
  const no = nos.find((n) => n.mac === a.mac)
  console.log(`- ${TITULO[a.tipo] ?? PADRAO}  —  ${no ? (no.cadastrado ? no.nome : "Sensor sem nome") : "(nó não listado)"}   [tipo=${a.tipo}]`)
  conferir(typeof a.tipo === "string", "alerta sem tipo")
}

// Todos os valores que o coordenador pode mandar (lib/alertas: nomeClasseSolo;
// comum/protocolo: nomeModoBateria), não só os desta captura.
console.log("\n=== Valores possíveis do coordenador × textos do app")
for (const classe of ["seco", "umido", "muito_umido"]) {
  const t = app.TEXTO_SOLO[classe]
  console.log(`  solo_classe=${classe.padEnd(12)} → ${t ?? "(sem texto)"}`)
  conferir(typeof t === "string", `solo_classe "${classe}" sem texto no app`)
}
for (const modo of ["normal", "economia", "critico"]) {
  const t = app.textoBateria({ modo_energia: modo })
  console.log(`  modo_energia=${modo.padEnd(11)} → ${t ?? "(sem texto)"}`)
  conferir(typeof t === "string", `modo_energia "${modo}" sem texto no app`)
}

console.log(`\n${problemas === 0 ? "OK: nenhum problema de contrato" : `${problemas} problema(s) de contrato`}`)
process.exit(problemas === 0 ? 0 : 1)
