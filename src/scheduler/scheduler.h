#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "domain/types.h"

// ---------------------------------------------------------------------------
// Montagem da fila do dia.
//
// Modulo PURO: "hoje" entra por parametro, nada de relogio nem de banco aqui.
//
// AVISO DELIBERADO: implementado com lacos simples de proposito. Reescrever
// como pipeline de std::ranges e o no `cpp.proj.scheduler-ranges` da trilha de
// C++; os testes em tests/test_core.cpp travam o comportamento esperado.
// ---------------------------------------------------------------------------
namespace st {

struct Config {
    int queue_size      = 5;
    int max_per_subject = 2;

    double w_deadline    = 4.0;
    double w_overdue     = 3.0;
    double w_maintenance = 2.0;
    double w_active      = 1.5;
    double w_unlocked    = 1.0;

    // A REGRA ANTI-DIVIDA. Cada componente e normalizado para [0, 1] dividindo
    // pelo seu cap. Consequencia: um item atrasado ha seis meses nao vale mais
    // que um atrasado ha duas semanas, e a fila nunca vira lista de pendencias.
    double overdue_cap     = 2.0;
    double maintenance_cap = 2.0;

    int deadline_horizon_days = 120;

    std::vector<int> srs_intervals{1, 3, 7, 21, 60};
    double           lapse_penalty = 0.5;
};

Config default_config();
Config parse_config(const std::string& json_text);  // tolerante: campo ausente = default

enum class Reason { Deadline, OverdueReview, Maintenance, Active, NewlyUnlocked, None };

std::string to_string(Reason r);

struct Candidate {
    const Node* node  = nullptr;
    double      score = 0.0;
    Reason      reason = Reason::None;
    std::string detail;        // texto curto para a interface ("3 sessoes sem tocar")
    int         dependents = 0;
};

using SubjectMap = std::unordered_map<std::string, Subject>;

// Pontua todos os nos elegiveis (destravados e nao arquivados), score > 0.
//
// ATENCAO ao tempo de vida: Candidate guarda um ponteiro para dentro de
// `nodes`. O resultado so e valido enquanto o vetor de origem viver. As
// sobrecargas de rvalue abaixo estao deletadas de proposito - passar um vetor
// temporario aqui e ponteiro pendurado garantido, e a primeira rodada de
// testes deste projeto descobriu isso com um SIGSEGV. Melhor virar erro de
// compilacao do que falha em tempo de execucao.
std::vector<Candidate> score_all(const std::vector<Node>& nodes,
                                 const SubjectMap&        subjects,
                                 const Config&            cfg,
                                 const Date&              today);
std::vector<Candidate> score_all(std::vector<Node>&&, const SubjectMap&, const Config&,
                                 const Date&) = delete;

// Aplica diversidade (max_per_subject) e corta em queue_size.
std::vector<Candidate> build_queue(const std::vector<Node>& nodes,
                                   const SubjectMap&        subjects,
                                   const Config&            cfg,
                                   const Date&              today);
std::vector<Candidate> build_queue(std::vector<Node>&&, const SubjectMap&, const Config&,
                                   const Date&) = delete;

// Proximo intervalo de revisao espacada dado o resultado da sessao.
int next_srs_interval(int current_days, Outcome outcome, const Config& cfg);

}  // namespace st
