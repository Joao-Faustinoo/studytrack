#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace st {

// ---------------------------------------------------------------------------
// Datas
//
// Sem <chrono> calendar aqui: o MSVC 19.29 desta maquina tem suporte parcial,
// e o algoritmo civil<->dias e curto o bastante para valer a pena ser explicito.
// Bonus: deixa o scheduler puro e testavel, porque "hoje" vira parametro.
// ---------------------------------------------------------------------------
struct Date {
    int y = 1970;
    int m = 1;
    int d = 1;

    friend bool operator==(const Date& a, const Date& b) {
        return a.y == b.y && a.m == b.m && a.d == b.d;
    }
};

long long days_from_civil(const Date& dt);
Date      civil_from_days(long long z);

// dias de `from` ate `to` (positivo se `to` for depois)
long long days_between(const Date& from, const Date& to);

Date                add_days(const Date& dt, long long n);
std::optional<Date> parse_date(std::string_view s);   // "YYYY-MM-DD"
std::string         format_date(const Date& dt);
Date                today_local();

// ---------------------------------------------------------------------------
// Nos
// ---------------------------------------------------------------------------

// theory  : consolida e entra em revisao espacada  -> decai em DIAS
// skill   : nunca termina, entra em manutencao     -> decai em SESSOES
// project : so fecha com artefato                  -> nao decai
enum class Kind { Theory, Skill, Project };

enum class State { Locked, Todo, Learning, Consolidated, Maintenance, Archived };

std::string          to_string(Kind k);
std::string          to_string(State s);
std::optional<Kind>  kind_from_string(std::string_view s);
std::optional<State> state_from_string(std::string_view s);

struct Node {
    std::string id;
    std::string subject_id;
    std::string title;
    Kind        kind  = Kind::Skill;
    State       state = State::Todo;
    std::string criterion;
    std::string notes;
    int         est_minutes = 20;
    int         sort_order  = 0;

    std::vector<std::string> prereqs;

    // kind == Skill
    std::optional<int> maintenance_sessions;  // janela, em sessoes da materia
    std::optional<int> last_touched_session;  // subject.session_count da ultima vez

    // kind == Theory
    std::optional<int>  srs_interval_days;
    std::optional<Date> srs_due_on;
    int                 srs_lapses = 0;

    // kind == Project
    std::optional<Date> deadline;
};

struct Subject {
    std::string id;
    std::string name;
    std::string color = "#888888";
    int         weekly_minutes = 0;
    int         session_count  = 0;   // contador monotonico; base da manutencao
    int         sort_order     = 0;
};

enum class Outcome { Worked, Good, Struggled };

std::string             to_string(Outcome o);
std::optional<Outcome>  outcome_from_string(std::string_view s);

struct SessionNode {
    std::string node_id;
    Outcome     outcome = Outcome::Worked;
};

struct Session {
    long long                id = 0;
    std::string              subject_id;
    std::string              started_at;    // ISO-8601 datetime
    int                      duration_min = 0;
    std::string              note;
    int                      subject_session_no = 0;
    std::vector<SessionNode> nodes;
    std::vector<std::pair<std::string, double>> metrics;
};

}  // namespace st
