#include "graph/graph.h"

#include <algorithm>
#include <unordered_set>

namespace st {

NodeIndex index_by_id(const std::vector<Node>& nodes) {
    NodeIndex idx;
    idx.reserve(nodes.size());
    for (const auto& n : nodes) idx.emplace(n.id, &n);
    return idx;
}

bool prereq_satisfied(const Node& prereq) {
    // Maintenance conta como satisfeito: uma escala em rodizio nao deixa de ser
    // base para o estudo que depende dela.
    return prereq.state == State::Consolidated ||
           prereq.state == State::Maintenance  ||
           prereq.state == State::Archived;
}

bool is_unlocked(const Node& n, const NodeIndex& idx) {
    for (const auto& pid : n.prereqs) {
        const auto it = idx.find(pid);
        if (it == idx.end()) continue;  // pre-requisito inexistente nao trava
        if (!prereq_satisfied(*it->second)) return false;
    }
    return true;
}

std::vector<std::string> missing_prereqs(const Node& n, const NodeIndex& idx) {
    std::vector<std::string> faltando;
    for (const auto& pid : n.prereqs) {
        const auto it = idx.find(pid);
        if (it == idx.end()) continue;
        if (!prereq_satisfied(*it->second)) faltando.push_back(pid);
    }
    return faltando;
}

// --- ordenacao topologica INGENUA (ver aviso no header) --------------------
//
// Varre repetidamente a lista inteira procurando um no cujos pre-requisitos ja
// sairam. Cada passada e O(n), e sao ate n passadas -> O(n^2). Com 110 nos
// roda em microssegundos, entao nao ha urgencia; a urgencia e pedagogica.
std::vector<std::string> topological_order(const std::vector<Node>& nodes) {
    std::vector<std::string>        ordem;
    std::unordered_set<std::string> prontos;
    std::vector<bool>               usado(nodes.size(), false);

    ordem.reserve(nodes.size());

    const auto conhecidos = index_by_id(nodes);

    bool progrediu = true;
    while (progrediu) {
        progrediu = false;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (usado[i]) continue;

            bool livre = true;
            for (const auto& pid : nodes[i].prereqs) {
                if (conhecidos.find(pid) == conhecidos.end()) continue;  // aresta solta
                if (prontos.find(pid) == prontos.end()) { livre = false; break; }
            }
            if (!livre) continue;

            ordem.push_back(nodes[i].id);
            prontos.insert(nodes[i].id);
            usado[i]  = true;
            progrediu = true;
        }
    }

    // sobrou no => ciclo
    if (ordem.size() != nodes.size()) return {};
    return ordem;
}

bool has_cycle(const std::vector<Node>& nodes) {
    return topological_order(nodes).empty() && !nodes.empty();
}

std::unordered_map<std::string, int> depth_of_dependents(const std::vector<Node>& nodes) {
    // arestas invertidas: pre-requisito -> quem depende dele
    std::unordered_map<std::string, std::vector<std::string>> dependentes;
    for (const auto& n : nodes)
        for (const auto& p : n.prereqs) dependentes[p].push_back(n.id);

    std::unordered_map<std::string, int> prof;
    const auto ordem = topological_order(nodes);

    // de tras para frente na ordem topologica, cada no ja encontra seus
    // dependentes calculados
    for (auto it = ordem.rbegin(); it != ordem.rend(); ++it) {
        int maior = 0;
        const auto dep = dependentes.find(*it);
        if (dep != dependentes.end()) {
            for (const auto& filho : dep->second) {
                const auto f = prof.find(filho);
                if (f != prof.end()) maior = std::max(maior, f->second + 1);
            }
        }
        prof[*it] = maior;
    }

    // se havia ciclo a ordem veio vazia; devolve tudo em zero
    if (ordem.empty())
        for (const auto& n : nodes) prof[n.id] = 0;

    return prof;
}

}  // namespace st
