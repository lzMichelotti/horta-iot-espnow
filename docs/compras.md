# Compras para o campo (Etapa 5)

Lista do que falta para levar os nós à horta com bateria. Arquitetura adotada (versão simples): **uma célula 18650 → módulo de carga com proteção → regulador de 3,3 V de baixo consumo → pino `3V3` da placa**; o `VIN` fica desligado; o divisor mede a tensão da célula. Justificativa em [`energia.md`](energia.md).

Quantidades para **2 nós + 1 de reserva**. Preço e loja em branco, para preencher.

## Nó sensor

| # | Item | Especificação mínima | Qtd. | Para quê | Etapa | Preço |
|---|---|---|---|---|---|---|
| 1 | Bateria 18650 Li-ion | Marca com datasheet publicado (Samsung, LG, Molicel, Murata/Sony). Evitar células sem marca ou com capacidade irreal ("9800 mAh"): a conta de autonomia depende da capacidade real | 3 | Alimentação do nó | 5, 12 | |
| 2 | Suporte para uma 18650 | Com fios ou terminais para solda; mola firme | 3 | Prender a célula | 12 | |
| 3 | Módulo de carga Li-ion de 1 célula **com proteção** | Carga a 4,2 V por USB; proteção contra descarga profunda, sobrecorrente e curto (ex.: módulos com TP4056 + DW01A; conferir que têm a saída separada da bateria, `OUT+`/`OUT−`) | 3 | Recarregar a célula e protegê-la | 5, 12 | |
| 4 | Regulador de 3,3 V de baixo consumo | Corrente quiescente ≤ 10 µA; saída ≥ 500 mA (exigência do módulo ESP32: [ESP32-WROOM-32 Datasheet v3.8](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf), tab. 13); entrada até ≥ 4,5 V. Referência que atende: Richtek RT9080-33 (2 µA típ., 600 mA; [DS9080-09](https://www.richtek.com/assets/product_file/RT9080/DS9080-09.pdf)). **Não serve** um módulo com AMS1117/1117: gasta 2–5 mA sozinho e precisa de ≥ 4,6 V na entrada ([BL1117C](https://www.belling.com.cn/media/file_object/bel_product/BL1117C/datasheet/BL1117C_V.F1.9_en.pdf), p. 4) | 3 (+ capacitores pedidos no datasheet do regulador) | Baixar a tensão da célula para a placa | 5, 12 | |
| 5 | Resistor 100 kΩ | 1 % (ou 5 %), 1/4 W | 10 (2 por nó + reserva) | Divisor que mede a bateria no GPIO35 | 5, 9 (alerta de alimentação) | |
| 6 | Capacitor cerâmico 100 nF | Qualquer tensão ≥ 10 V | 5 | Filtro no ponto médio do divisor (opcional, [`sensores.md`](sensores.md) 5.1) | 5 | |
| 7 | Placa perfurada (ilhada) para solda | Tamanho que caiba na caixa | 3 | Montagem definitiva, fora da protoboard (trilhos com mau contato) | 12 | |
| 8 | Sensor AHT20 (módulo) | Igual ao atual | 2 | Nó 2 + reserva | 12 | |
| 9 | Sensor de umidade do solo capacitivo V1.2 | Igual ao atual | 2 | Nó 2 + reserva | 12 | |
| 10 | Placa ESP32 DOIT DevKit V1 | Igual às atuais (CH9102X) | 1 | Reserva (TODO: confirmar se o ESP32 que sobrou vira o nó 2) | 12 | |

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
- **MOSFET para desligar os sensores entre leituras e retirada do LED da placa:** melhoram a autonomia, mas não são necessários para medi-la. Ficam como melhoria futura.
- **Painel solar:** não é necessário para 30 dias; fica para uma instalação permanente.
