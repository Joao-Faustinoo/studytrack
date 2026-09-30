#include "scheduler/scheduler.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <type_traits>

#include "graph/graph.h"
#include "json.hpp"

namespace st {

namespace {

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// Normaliza para [0, 1] dividindo pelo cap. E aqui que a divida morre.
double saturate(double raw, double cap) {
    if (cap <= 0.0) return 0.0;
    return clamp01(raw / cap);
}

std::string plural(long long n, const char* um, const char* muitos) {
    return std::to_string(n) + " " + (n == 1 ? um : muitos);
}

}  // namespace

Config default_config() { return Config{}; }

std::string to_string(Reason r) {
    switch (r) {
        case Reason::Deadline:      return "prazo";
        case Reason::OverdueReview: return "revisao";
        case Reason::Maintenance:   return "manutencao";
        case Reason::Active:        return "em andamento";
        case Reason::NewlyUnlocked: return "destravado";
        case Reason::None:          break;
    }
    return "";
}

Config parse_config(const std::string& json_text) {
    Config c;
    nlohmann::json j = nlohmann::json::parse(json_text, nullptr, false);
    if (j.is_discarded()) return c;  // config quebrada nao derruba o app

    auto num = [](const nlohmann::json& src, const char* key, auto& dest) {
        if (src.contains(key) && src[key].is_number())
            dest = src[key].get<std::decay_t<decltype(dest)>>();
    };

    if (j.contains("queue")) {
        const auto& q = j["queue"];
        num(q, "size", c.queue_size);
        num(q, "max_per_subject", c.max_per_subject);
    }
    if (j.contains("weights")) {
        const auto& w = j["weights"];
        num(w, "deadline_urgency", c.w_deadline);
        num(w, "overdue_srs", c.w_overdue);
        num(w, "maintenance", c.w_maintenance);
        num(w, "active_project", c.w_active);
        num(w, "newly_unlocked", c.w_unlocked);
    }
    if (j.contains("saturation")) {
        const auto& s = j["saturation"];
        num(s, "overdue_ratio_cap", c.overdue_cap);
        num(s, "maintenance_ratio_cap", c.maintenance_cap);
    }
    if (j.contains("deadline")) num(j["deadline"], "horizon_days", c.deadline_horizon_days);
    if (j.contains("srs")) {
        const auto& s = j["srs"];
        num(s, "lapse_penalty", c.lapse_penalty);
        if (s.contains("intervals_days") && s["intervals_days"].is_array()) {
            std::vector<int> iv;
            for (const auto& e : s["intervals_days"])
                if (e.is_number_integer()) iv.push_back(e.get<int>());
            if (!iv.empty()) c.srs_intervals = std::move(iv);
        }
    }
    return c;
}

std::vector<Candidate> score_all(const std::vector<Node>& nodes,
                                 const SubjectMap&        subjects,
                                 const Config&            cfg,
                                 const Date&              today) {
    const auto idx  = index_by_id(nodes);
    const auto prof = depth_of_dependents(nodes);

    std::vector<Candidate> saida;
    saida.reserve(nodes.size());

    for (const auto& n : nodes) {
        if (n.state == State::Archived) continue;
        if (!is_unlocked(n, idx)) continue;

        double      melhor = 0.0;
        Reason      motivo = Reason::None;
        std::string detalhe;
        double      total = 0.0;

        auto considerar = [&](double contrib, Reason r, std::string texto) {
            if (contrib <= 0.0) return;
            total += contrib;
            if (contrib > melhor) {
                melhor  = contrib;
                motivo  = r;
                detalhe = std::move(texto);
            }
        };

        // --- prazo (projeto com deadline dentro do horizonte) ---------------
        if (n.kind == Kind::Project && n.deadline) {
            const long long faltam = days_between(today, *n.deadline);
            if (faltam <= cfg.deadline_horizon_days) {
                double r;
                std::string texto;
                if (faltam <= 0) {
                    r = 1.0;
                    texto = "prazo vencido";
                } else {
                    r = clamp01(static_cast<double>(cfg.deadline_horizon_days - faltam) /
                                static_cast<double>(cfg.deadline_horizon_days));
                    texto = "faltam " + plural(faltam, "dia", "dias");
                }
                considerar(cfg.w_deadline * r, Reason::Deadline, std::move(texto));
            }
        }

        // --- revisao espacada atrasada (theory) -----------------------------
        if (n.kind == Kind::Theory && n.srs_due_on) {
            const long long atraso = days_between(*n.srs_due_on, today);
            if (atraso >= 0) {
                const int intervalo = std::max(1, n.srs_interval_days.value_or(1));
                const double r = saturate(static_cast<double>(atraso) / intervalo, cfg.overdue_cap);
                const std::string texto =
                    atraso == 0 ? "revisao vence hoje"
                                : "revisao atrasada " + plural(atraso, "dia", "dias");
                // atraso zero ainda merece entrar na fila: vence hoje
                considerar(cfg.w_overdue * std::max(r, 0.05), Reason::OverdueReview, texto);
            }
        }

        // --- manutencao (skill, contada em SESSOES da materia) --------------
        if (n.kind == Kind::Skill && n.maintenance_sessions &&
            (n.state == State::Maintenance || n.state == State::Consolidated)) {
            const auto sub = subjects.find(n.subject_id);
            const int  atual = (sub != subjects.end()) ? sub->second.session_count : 0;
            const int  janela = std::max(1, *n.maintenance_sessions);

            double      r;
            std::string texto;
            if (!n.last_touched_session) {
                // Nunca tocado desde a semeadura: equivale a UMA janela vencida,
                // e passa pela mesma saturacao. Tratar como 1.0 cheio faria o no
                // que nunca entrou no rodizio superar o que esta de fato
                // atrasado - exatamente o contrario do que se quer.
                r = saturate(1.0, cfg.maintenance_cap);
                texto = "ainda nao entrou no rodizio";
            } else {
                const int gap = std::max(0, atual - *n.last_touched_session);
                r = saturate(static_cast<double>(gap) / janela, cfg.maintenance_cap);
                texto = plural(gap, "sessao sem tocar", "sessoes sem tocar");
            }
            considerar(cfg.w_maintenance * r, Reason::Maintenance, std::move(texto));
        }

        // --- em andamento (qualquer tipo em Learning) -----------------------
        // detalhe vazio: o rotulo "em andamento" ja diz tudo, repetir polui
        if (n.state == State::Learning)
            considerar(cfg.w_active, Reason::Active, "");

        // --- recem destravado ----------------------------------------------
        if (n.state == State::Todo)
            considerar(cfg.w_unlocked, Reason::NewlyUnlocked, "pre-requisitos prontos");

        if (total <= 0.0) continue;

        Candidate c;
        c.node   = &n;
        c.score  = total;
        c.reason = motivo;
        c.detail = detalhe;
        const auto p = prof.find(n.id);
        c.dependents = (p != prof.end()) ? p->second : 0;
        saida.push_back(std::move(c));
    }

    std::sort(saida.begin(), saida.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) return a.score > b.score;
        // desempate: destrava mais coisa adiante vem antes
        if (a.dependents != b.dependents) return a.dependents > b.dependents;
        // depois: o mais curto primeiro, para caber na sessao
        if (a.node->est_minutes != b.node->est_minutes)
            return a.node->est_minutes < b.node->est_minutes;
        return a.node->id < b.node->id;  // estavel e reproduzivel
    });

    return saida;
}

std::vector<Candidate> build_queue(const std::vector<Node>& nodes,
                                   const SubjectMap&        subjects,
                                   const Config&            cfg,
                                   const Date&              today) {
    const auto todos = score_all(nodes, subjects, cfg, today);

    std::vector<Candidate>                 fila;
    std::unordered_map<std::string, int>   por_materia;

    for (const auto& c : todos) {
        if (static_cast<int>(fila.size()) >= cfg.queue_size) break;
        int& usado = por_materia[c.node->subject_id];
        if (usado >= cfg.max_per_subject) continue;  // a regra que impede monocultura
        ++usado;
        fila.push_back(c);
    }
    return fila;
}

int next_srs_interval(int current_days, Outcome outcome, const Config& cfg) {
    const auto& iv = cfg.srs_intervals;
    if (iv.empty()) return std::max(1, current_days);

    if (outcome == Outcome::Struggled) {
        // Penaliza sem zerar: zerar depois de um tropeco desmotiva e nao
        // melhora a retencao o bastante para compensar.
        const double reduzido = current_days * cfg.lapse_penalty;
        return std::max(iv.front(), static_cast<int>(std::lround(reduzido)));
    }

    for (size_t i = 0; i < iv.size(); ++i) {
        if (current_days < iv[i]) return iv[i];
    }
    return iv.back();
}

}  // namespace st
