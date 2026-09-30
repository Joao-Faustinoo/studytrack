#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "domain/types.h"

// ---------------------------------------------------------------------------
// Grafo de pre-requisitos.
//
// Modulo PURO: nao toca em banco, arquivo nem relogio. Nao e purismo - e o que
// permite testar tudo aqui sem um unico mock.
//
// AVISO DELIBERADO: a ordenacao topologica abaixo e ingenua, O(n^2). Ela esta
// assim de proposito. Trocar por Kahn com deteccao de ciclo em O(V+E) e o no
// `cpp.proj.kahn` da trilha de C++ - os testes em tests/test_core.cpp ja
// definem o contrato que a nova versao precisa respeitar.
// ---------------------------------------------------------------------------
namespace st {

using NodeIndex = std::unordered_map<std::string, const Node*>;

NodeIndex index_by_id(const std::vector<Node>& nodes);

// Um no esta destravado quando TODOS os pre-requisitos ja foram dominados.
// "Dominado" inclui Maintenance: habilidade em manutencao continua servindo de
// base, ainda que esteja pedindo rodizio.
bool prereq_satisfied(const Node& prereq);
bool is_unlocked(const Node& n, const NodeIndex& idx);

// Pre-requisitos que ainda faltam (para a interface explicar o bloqueio).
std::vector<std::string> missing_prereqs(const Node& n, const NodeIndex& idx);

// Ordem topologica: pre-requisito sempre antes de quem depende dele.
// Vazio se houver ciclo.
std::vector<std::string> topological_order(const std::vector<Node>& nodes);

bool has_cycle(const std::vector<Node>& nodes);

// Quantos passos ate o no mais distante que depende deste. Usado para desempate:
// entre dois candidatos iguais, o que destrava mais coisa adiante vem primeiro.
std::unordered_map<std::string, int> depth_of_dependents(const std::vector<Node>& nodes);

}  // namespace st
