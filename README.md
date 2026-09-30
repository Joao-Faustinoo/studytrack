# studytrack

Rastreador pessoal de estudo para três matérias com naturezas diferentes: **fagote**,
**C++** e **animação 2D**.

Motor em C++ servindo uma API local e uma interface web fina. Banco em SQLite,
arquivo único, ao lado do executável.

---

## O problema que ele resolve

Rastreadores de estudo genéricos modelam tudo como lista de tarefas com checkbox.
Isso quebra em duas das três matérias aqui:

| Matéria | Natureza | O que "progresso" significa |
| --- | --- | --- |
| Fagote — técnica | **cíclica, decai** | escala não se conclui, se mantém |
| Fagote — teoria | linear, decai | precisa de revisão espaçada |
| Animação 2D | **linear, currículo canônico** | exercícios entregues, portfólio |
| C++ | **project-driven** | conceito só conta quando virou artefato |

Daí os três tipos de nó — `theory`, `skill`, `project` — e não um único "tópico".

## Três decisões de design que explicam o resto

**1. Manutenção conta em sessões, revisão conta em dias.**
Habilidade que decai por falta de rodízio (`skill`) usa janela em *sessões da
matéria*: cinco sessões de C++ não envelhecem nada do fagote. Memória de
conteúdo (`theory`) decai com tempo de relógio, então usa dias.

**2. Sem acúmulo de dívida.**
O atraso satura (`saturation` em `config.json`). Depois de um mês parado a fila
continua com 3–5 itens, nunca uma lista de 40 pendências vermelhas. Essa é a
regra que decide se o app sobrevive ao segundo mês de uso.

**3. Sem streak.**
Com estudo deliberadamente irregular, streak é só um gerador de culpa que
termina em abandono.

## A fila do dia

Cada candidato recebe uma pontuação; a fila é o topo, com no máximo 2 itens por
matéria:

```
score = 4.0 × urgência_de_prazo      (projeto com deadline dentro do horizonte)
      + 3.0 × atraso_de_revisão      (saturado)
      + 2.0 × defasagem_manutenção   (saturado)
      + 1.5 × é_projeto_ativo
      + 1.0 × recém_desbloqueado     (pré-requisitos consolidados agora)
```

Pesos em `config.json`, nunca no código.

## Estrutura

```
src/domain/     tipos de valor — Node, Session, Metric, NodeState
src/graph/      DAG, ordenação topológica, detecção de ciclo, nós desbloqueados
src/scheduler/  decaimento, SRS, pontuação, montagem da fila
src/store/      SQLite + schema.sql
src/api/        rotas HTTP + JSON
src/cli/        subcomandos
tests/          Catch2
web/            interface estática
seeds/          as três trilhas (110 nós)
```

`graph/` e `scheduler/` não tocam em I/O nem em banco. Não é purismo: são os
dois únicos módulos com lógica de verdade, e assim ficam testáveis sem mock.

## Ambiente de build

Esta máquina tem MSVC 14.29 (VS 2019 Build Tools, compilador 19.29) e Windows
SDK 10.0.19041 — porém **o `vswhere.exe` está ausente** e o registro do
instalador está quebrado, então o CMake não detecta o MSVC pelo gerador
"Visual Studio 16 2019".

Solução: gerador **Ninja** a partir de um shell `vcvars64`, onde o CMake só
precisa achar o `cl` no PATH. O `build.ps1` faz isso sozinho.

Consequências de usar 19.29 em vez do VS 2022:

- sem `<format>` — usar streams ou fmt
- sem `<expected>` — o projeto usa um `Result<T,E>` próprio (ver nó
  `cpp.proj.result`; escrever o seu ensina mais do que usar o da biblioteca)
- `<ranges>` e concepts funcionam (testados nesta máquina)

## Sobre o código do motor

As implementações de `graph/` e `scheduler/` no v1 são **deliberadamente
ingênuas** — ordenação topológica O(n²), laços simples no lugar de views. Um
tracker quebrado não é usado, então ele nasce funcionando; mas a trilha de C++
tem nós concretos (`cpp.proj.kahn`, `cpp.proj.scheduler-ranges`,
`cpp.proj.sqlite-raii`) que consistem exatamente em substituir esse código.

A ferramenta funciona hoje e continua sendo campo de treino.

## Roadmap

- **v1** — grafo, log de sessões, fila do dia, três trilhas semeadas
- **v2** — SRS e manutenção ativos; **log de palhetas** (data, perfil, vida útil)
- **v3** — artefatos: gravações, GIFs, links de commit
- **v4** — estatísticas e histórico

Estatística é a parte mais divertida de programar e a menos útil no começo, por
isso fica por último de propósito.
