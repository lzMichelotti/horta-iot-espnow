# Compras para o campo (Etapa 5)

Lista do que falta para levar os nós à horta com bateria. Arquitetura adotada em 26/11/2026:

- **Alimentação:** uma célula 18650 → módulo de carga com proteção → regulador de 3,3 V de baixo consumo → pino `3V3` da placa; o `VIN` fica desligado.
- **Sensores:** AHT20 e sensor de solo alimentados por um P-MOSFET (lado positivo, gate no GPIO25 com pull-up de 100 kΩ), ligados só durante a leitura.
- **Medição da bateria:** divisor 100k + 100k da célula para o GPIO35.
- **Placa:** LED de alimentação retirado.

Justificativa em [`energia.md`](energia.md). Quantidades para **2 nós** (+ reserva onde indicado). Preço e loja em branco, para preencher.

## Bateria e alimentação do nó

| # | Item | Especificação mínima | Qtd. | Para quê | Etapa | Preço |
|---|---|---|---|---|---|---|
| 1 | Bateria 18650 Li-ion | Marca com datasheet publicado (Samsung, LG, Molicel, Murata/Sony). Evitar células sem marca ou com capacidade irreal ("9800 mAh"): a conta de autonomia depende da capacidade real | 2 (+1 reserva) | Alimentação do nó | 5, 12 | |
| 2 | Suporte para uma 18650 | Com fios ou terminais para solda; mola firme | 2 | Prender a célula | 12 | |
| 3 | Módulo de carga TP4056 **com proteção** | Carga a 4,2 V por USB; proteção contra descarga profunda, sobrecorrente e curto (TP4056 + DW01A); 6 terminais, com a saída separada da bateria (`IN±`, `B±`, `OUT±`) | 2 (+1 reserva) | Recarregar a célula e protegê-la | 5, 12 | |
| 4 | Regulador RT9080-33 (TSOT-23-5) | Corrente quiescente de 2 µA típ./4 máx.; 600 mA; entrada de 1,2 a 5,5 V ([DS9080-09](https://www.richtek.com/assets/product_file/RT9080/DS9080-09.pdf)); atende aos ≥ 0,5 A exigidos pelo módulo ESP32 ([ESP32-WROOM-32 Datasheet v3.8](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf), tab. 13). Pino EN ligado ao VIN. **Não serve** um módulo com AMS1117/1117: gasta 2–5 mA sozinho e precisa de ≥ 4,6 V na entrada ([BL1117C](https://www.belling.com.cn/media/file_object/bel_product/BL1117C/datasheet/BL1117C_V.F1.9_en.pdf), p. 4) | 2 (+1 reserva) | Baixar a tensão da célula para 3,3 V | 5, 12 | |
| 5 | P-MOSFET AO3401A (SOT-23) | VGS(th) de −0,5 a −1,3 V; RDS(on) < 85 mΩ com VGS = −2,5 V ([datasheet rev. 3.1](https://www.aosmd.com/res/datasheets/AO3401A.pdf)) | 1 pacote (2 usados) | Ligar os sensores só durante a leitura | 5 | |
| 6 | Adaptador SOT-23 → DIP | Para soldar os itens 4 e 5 na placa perfurada | 1 pacote (4 usados) | Montagem | 12 | |
| 7 | Capacitor cerâmico 10 µF | ≥ 10 V (X5R ou X7R). O datasheet do RT9080 pede ≥ 1 µF efetivo na entrada e na saída ("Capacitor Selection"); 10 µF dá folga à perda por tensão aplicada | 1 pacote (4 usados) | Estabilidade do regulador | 5 | |
| 8 | Capacitor cerâmico 100 nF | ≥ 10 V | 1 pacote | Filtro no ponto médio do divisor (opcional, [`sensores.md`](sensores.md) 5.1) | 5 | |
| — | Resistor 100 kΩ | 1/4 W | 6 (3 por nó) | Divisor (2) e pull-up do gate (1) | 5, 9 | **já comprado** (kit Ryndack CR25, 20 valores; conferir se inclui 100 kΩ) |

## Montagem

| # | Item | Especificação mínima | Qtd. | Para quê | Etapa | Preço |
|---|---|---|---|---|---|---|
| 9 | Placa perfurada (ilhada) para solda | Tamanho que caiba na caixa | 3 | Montagem definitiva, fora da protoboard (trilhos com mau contato) | 12 | |
| — | Sensores (AHT20, solo V1.2) e ESP32 do nó 2 | Iguais aos atuais | — | — | 12 | já tem (informado em 26/11/2026) |

## Caixas e passagem dos cabos

| # | Item | Especificação mínima | Qtd. | Para quê | Etapa | Preço |
|---|---|---|---|---|---|---|
| 11 | Caixa de proteção | **IP65 no mínimo**: 6 = vedada contra poeira; 5 = protegida contra jatos d'água de qualquer direção (chuva com vento, mangueira de rega). IP67 (imersão) não é necessário. Graus da norma IEC 60529. Cor clara, para esquentar menos ao sol | 3 (2 nós + coordenador) | Proteger placa, bateria e módulos | 12 | |
| 12 | Prensa-cabo | Compatível com o diâmetro do cabo dos sensores e com a parede da caixa | 6 (2 por caixa) | Passar os cabos do solo e do AHT20 sem perder a vedação | 12 | |
| 13 | Sachês de sílica gel | Pequenos | 6 | Reduzir a condensação dentro da caixa | 12 | |
| 14 | Fios e conectores travados | Cabo de 3 vias para o solo, 4 vias para o AHT20 (I²C) | conforme montagem | Ligações de campo (fio solto já deixou o AHT20 em erro) | 12 | |

## Coordenador

| # | Item | Especificação mínima | Qtd. | Para quê | Etapa | Preço |
|---|---|---|---|---|---|---|
| 15 | Alimentação do coordenador | TODO: depende de haver tomada na horta. Sem tomada: power bank (serve aqui, porque o coordenador puxa corrente alta e constante e a tensão da bateria dele não é medida), com troca/recarga periódica | TODO | Manter o coordenador ligado 24 h no campo | 12 | |

## Fora da lista (decisão de 26/11/2026)

- **Multímetro, INA219, fonte de bancada:** não serão usados. O consumo é estimado (tempo medido × datasheet) e a autonomia é medida em campo pela tensão da célula ([`energia.md`](energia.md)).
- **Painel solar:** não é necessário para 30 dias; fica para uma instalação permanente.
