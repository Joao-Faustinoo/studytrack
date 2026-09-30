#include "catch_amalgamated.hpp"

#include <algorithm>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "domain/types.h"
#include "graph/graph.h"
#include "scheduler/scheduler.h"

using namespace st;

namespace {

Node mk(std::string id, std::string subject, Kind k, State s) {
    Node n;
    n.id         = std::move(id);
    n.subject_id = std::move(subject);
    n.title      = n.id;
    n.kind       = k;
    n.state      = s;
    return n;
}

SubjectMap subs(std::initializer_list<std::pair<std::string, int>> ss) {
    SubjectMap m;
    for (const auto& p : ss) {
        Subject s;
        s.id            = p.first;
        s.name          = p.first;
        s.session_count = p.second;
        m.emplace(s.id, s);
    }
    return m;
}

const Candidate* find(const std::vector<Candidate>& v, const std::string& id) {
    for (const auto& c : v)
        if (c.node->id == id) return &c;
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
TEST_CASE("datas: ida e volta civil<->dias") {
    CHECK(days_from_civil(Date{1970, 1, 1}) == 0);
    CHECK(days_from_civil(Date{2000, 3, 1}) == 11017);

    for (long long z = -40000; z < 40000; z += 337) {
        const Date d = civil_from_days(z);
        CHECK(days_from_civil(d) == z);
    }
}

TEST_CASE("datas: bissexto e diferenca") {
    CHECK(days_between(Date{2024, 2, 28}, Date{2024, 3, 1}) == 2);   // 2024 bissexto
    CHECK(days_between(Date{2023, 2, 28}, Date{2023, 3, 1}) == 1);
    CHECK(days_between(Date{1900, 2, 28}, Date{1900, 3, 1}) == 1);   // 1900 NAO e bissexto
    CHECK(days_between(Date{2000, 2, 28}, Date{2000, 3, 1}) == 2);   // 2000 e bissexto

    CHECK(days_between(Date{2026, 9, 30}, Date{2027, 1, 15}) == 107);
}

TEST_CASE("datas: parse rejeita data impossivel") {
    CHECK(parse_date("2026-09-30").has_value());
    CHECK_FALSE(parse_date("2026-02-31").has_value());
    CHECK_FALSE(parse_date("2026-13-01").has_value());
    CHECK_FALSE(parse_date("26-09-30").has_value());
    CHECK_FALSE(parse_date("2026/09/30").has_value());
    CHECK(format_date(*parse_date("2026-09-30")) == "2026-09-30");
}

// ---------------------------------------------------------------------------
TEST_CASE("grafo: ordem topologica poe pre-requisito antes") {
    std::vector<Node> ns{
        mk("c", "s", Kind::Skill, State::Todo),
        mk("a", "s", Kind::Skill, State::Todo),
        mk("b", "s", Kind::Skill, State::Todo),
    };
    ns[0].prereqs = {"b"};  // c depende de b
    ns[2].prereqs = {"a"};  // b depende de a

    const auto ordem = topological_order(ns);
    REQUIRE(ordem.size() == 3);

    auto pos = [&](const std::string& id) {
        return std::find(ordem.begin(), ordem.end(), id) - ordem.begin();
    };
    CHECK(pos("a") < pos("b"));
    CHECK(pos("b") < pos("c"));
    CHECK_FALSE(has_cycle(ns));
}

TEST_CASE("grafo: ciclo e detectado") {
    std::vector<Node> ns{
        mk("a", "s", Kind::Skill, State::Todo),
        mk("b", "s", Kind::Skill, State::Todo),
    };
    ns[0].prereqs = {"b"};
    ns[1].prereqs = {"a"};

    CHECK(has_cycle(ns));
    CHECK(topological_order(ns).empty());
}

TEST_CASE("grafo: destravamento respeita estado do pre-requisito") {
    std::vector<Node> ns{
        mk("base", "s", Kind::Skill, State::Todo),
        mk("dep", "s", Kind::Skill, State::Todo),
    };
    ns[1].prereqs = {"base"};

    auto idx = index_by_id(ns);
    CHECK_FALSE(is_unlocked(ns[1], idx));
    CHECK(missing_prereqs(ns[1], idx).size() == 1);

    // manutencao CONTA como pre-requisito satisfeito
    ns[0].state = State::Maintenance;
    idx = index_by_id(ns);
    CHECK(is_unlocked(ns[1], idx));
    CHECK(missing_prereqs(ns[1], idx).empty());
}

TEST_CASE("grafo: aresta apontando para no inexistente nao trava") {
    std::vector<Node> ns{mk("a", "s", Kind::Skill, State::Todo)};
    ns[0].prereqs = {"fantasma"};

    const auto idx = index_by_id(ns);
    CHECK(is_unlocked(ns[0], idx));
    CHECK(topological_order(ns).size() == 1);
}

// ---------------------------------------------------------------------------
TEST_CASE("scheduler: a regra anti-divida - atraso satura") {
    Config cfg;
    cfg.overdue_cap = 2.0;

    const Date hoje{2026, 9, 30};

    auto teoria = [&](const std::string& id, int dias_atraso) {
        Node n = mk(id, "s", Kind::Theory, State::Consolidated);
        n.srs_interval_days = 7;
        n.srs_due_on        = add_days(hoje, -dias_atraso);
        return n;
    };

    std::vector<Node> ns{
        teoria("atrasado_14d", 14),     // exatamente no cap (14/7 = 2.0)
        teoria("atrasado_1ano", 365),   // absurdamente atrasado
    };

    const auto fila = score_all(ns, subs({{"s", 0}}), cfg, hoje);

    const auto* a = find(fila, "atrasado_14d");
    const auto* b = find(fila, "atrasado_1ano");
    REQUIRE(a);
    REQUIRE(b);

    // ESTE e o teste que decide se o app sobrevive ao segundo mes de uso:
    // um ano parado nao vale mais que duas semanas.
    CHECK(a->score == Catch::Approx(b->score));
    CHECK(b->score == Catch::Approx(cfg.w_overdue));
}

TEST_CASE("scheduler: manutencao conta em sessoes da materia, nao em dias") {
    Config cfg;
    const Date hoje{2026, 9, 30};

    Node n = mk("escala", "fagote", Kind::Skill, State::Maintenance);
    n.maintenance_sessions = 2;
    n.last_touched_session = 5;
    const std::vector<Node> ns{n};

    // a materia avancou 2 sessoes desde que o no foi tocado -> janela cheia
    auto fila = score_all(ns, subs({{"fagote", 7}}), cfg, hoje);
    REQUIRE(find(fila, "escala"));
    const double cheia = find(fila, "escala")->score;

    // mesma data, mas nenhuma sessao de fagote aconteceu -> nao pede rodizio
    fila = score_all(ns, subs({{"fagote", 5}}), cfg, hoje);
    const auto* parado = find(fila, "escala");
    CHECK((parado == nullptr || parado->score < cheia));
}

TEST_CASE("scheduler: sessoes de outra materia nao envelhecem o fagote") {
    Config cfg;
    const Date hoje{2026, 9, 30};

    Node n = mk("escala", "fagote", Kind::Skill, State::Maintenance);
    n.maintenance_sessions = 2;
    n.last_touched_session = 3;
    const std::vector<Node> ns{n};

    SubjectMap m = subs({{"fagote", 3}, {"cpp", 50}});
    const auto fila = score_all(ns, m, cfg, hoje);

    // 50 sessoes de C++ e o fagote continua em dia
    const auto* c = find(fila, "escala");
    CHECK(c == nullptr);
}

TEST_CASE("scheduler: prazo so empurra dentro do horizonte") {
    Config cfg;
    cfg.deadline_horizon_days = 120;
    const Date hoje{2026, 9, 30};

    Node longe = mk("longe", "s", Kind::Project, State::Todo);
    longe.deadline = add_days(hoje, 300);

    Node perto = mk("perto", "s", Kind::Project, State::Todo);
    perto.deadline = add_days(hoje, 10);

    const std::vector<Node> ns{longe, perto};
    const auto fila = score_all(ns, subs({{"s", 0}}), cfg, hoje);

    const auto* l = find(fila, "longe");
    const auto* p = find(fila, "perto");
    REQUIRE(l);
    REQUIRE(p);
    CHECK(p->score > l->score);
    CHECK(p->reason == Reason::Deadline);
    CHECK(l->reason == Reason::NewlyUnlocked);  // fora do horizonte, so destravado
}

TEST_CASE("scheduler: prazo vencido nao passa do teto") {
    Config cfg;
    const Date hoje{2026, 9, 30};

    Node vencido = mk("vencido", "s", Kind::Project, State::Todo);
    vencido.deadline = add_days(hoje, -400);

    Node hoje_mesmo = mk("hoje", "s", Kind::Project, State::Todo);
    hoje_mesmo.deadline = hoje;

    const std::vector<Node> ns{vencido, hoje_mesmo};
    const auto fila = score_all(ns, subs({{"s", 0}}), cfg, hoje);
    REQUIRE(find(fila, "vencido"));
    REQUIRE(find(fila, "hoje"));
    CHECK(find(fila, "vencido")->score == Catch::Approx(find(fila, "hoje")->score));
}

TEST_CASE("scheduler: diversidade impede monocultura na fila") {
    Config cfg;
    cfg.queue_size      = 5;
    cfg.max_per_subject = 2;
    const Date hoje{2026, 9, 30};

    std::vector<Node> ns;
    for (int i = 0; i < 10; ++i) {
        Node n = mk("cpp" + std::to_string(i), "cpp", Kind::Theory, State::Consolidated);
        n.srs_interval_days = 1;
        n.srs_due_on        = add_days(hoje, -30);  // tudo muito atrasado
        ns.push_back(n);
    }
    for (int i = 0; i < 3; ++i)
        ns.push_back(mk("fag" + std::to_string(i), "fagote", Kind::Skill, State::Todo));

    const auto fila = build_queue(ns, subs({{"cpp", 0}, {"fagote", 0}}), cfg, hoje);

    int de_cpp = 0;
    for (const auto& c : fila)
        if (c.node->subject_id == "cpp") ++de_cpp;

    CHECK(de_cpp == 2);  // mesmo com 10 candidatos urgentes de C++

    // 2 materias x 2 por materia = 4, ainda que queue_size seja 5. A diversidade
    // manda mais que o tamanho da fila: melhor 4 itens variados que 5 de C++.
    CHECK(fila.size() == 4);
}

TEST_CASE("scheduler: no travado nunca entra na fila") {
    Config cfg;
    const Date hoje{2026, 9, 30};

    std::vector<Node> ns{
        mk("base", "s", Kind::Skill, State::Todo),
        mk("dep", "s", Kind::Project, State::Todo),
    };
    ns[1].prereqs  = {"base"};
    ns[1].deadline = hoje;  // mesmo com prazo vencendo hoje

    const auto fila = score_all(ns, subs({{"s", 0}}), cfg, hoje);
    CHECK(find(fila, "dep") == nullptr);
    CHECK(find(fila, "base") != nullptr);
}

TEST_CASE("scheduler: no arquivado e ignorado") {
    Config cfg;
    const std::vector<Node> ns{mk("x", "s", Kind::Skill, State::Archived)};
    const auto fila = score_all(ns, subs({{"s", 0}}), cfg, Date{2026, 9, 30});
    CHECK(fila.empty());
}

// ---------------------------------------------------------------------------
TEST_CASE("srs: intervalo avanca na escada e recua sem zerar") {
    Config cfg;  // {1, 3, 7, 21, 60}

    CHECK(next_srs_interval(1, Outcome::Good, cfg) == 3);
    CHECK(next_srs_interval(3, Outcome::Good, cfg) == 7);
    CHECK(next_srs_interval(21, Outcome::Good, cfg) == 60);
    CHECK(next_srs_interval(60, Outcome::Good, cfg) == 60);  // teto

    // tropeco penaliza pela metade, mas nunca volta a zero
    CHECK(next_srs_interval(60, Outcome::Struggled, cfg) == 30);
    CHECK(next_srs_interval(1, Outcome::Struggled, cfg) == 1);
    CHECK(next_srs_interval(3, Outcome::Struggled, cfg) >= cfg.srs_intervals.front());
}

TEST_CASE("config: JSON quebrado cai no default em vez de derrubar o app") {
    const Config c = parse_config("{ isso nao e json");
    CHECK(c.queue_size == Config{}.queue_size);
    CHECK(c.w_deadline == Catch::Approx(Config{}.w_deadline));
}

TEST_CASE("config: campos presentes sobrescrevem o default") {
    const Config c = parse_config(R"({
        "queue": { "size": 9, "max_per_subject": 1 },
        "weights": { "deadline_urgency": 7.5 },
        "deadline": { "horizon_days": 200 },
        "srs": { "intervals_days": [2, 5] }
    })");
    CHECK(c.queue_size == 9);
    CHECK(c.max_per_subject == 1);
    CHECK(c.w_deadline == Catch::Approx(7.5));
    CHECK(c.deadline_horizon_days == 200);
    REQUIRE(c.srs_intervals.size() == 2);
    CHECK(c.srs_intervals[1] == 5);
    // campo ausente mantem o default
    CHECK(c.w_overdue == Catch::Approx(Config{}.w_overdue));
}
