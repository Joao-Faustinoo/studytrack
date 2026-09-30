#include "domain/types.h"

#include <array>
#include <cstdio>
#include <ctime>

namespace st {

// Algoritmo de Howard Hinnant (days_from_civil / civil_from_days), dominio
// proleptic Gregorian. Vale a pena ler: a rotacao do ano para marco faz o ano
// bissexto cair no fim do ciclo e elimina todo caso especial de fevereiro.
long long days_from_civil(const Date& dt) {
    long long y = dt.y;
    const unsigned m = static_cast<unsigned>(dt.m);
    const unsigned d = static_cast<unsigned>(dt.d);

    y -= (m <= 2);
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned  yoe = static_cast<unsigned>(y - era * 400);              // [0, 399]
    const unsigned  mp  = (m > 2 ? m - 3u : m + 9u);                         // marco = 0
    const unsigned  doy = (153u * mp + 2u) / 5u + d - 1u;                    // [0, 365]
    const unsigned  doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;          // [0, 146096]
    return era * 146097LL + static_cast<long long>(doe) - 719468LL;
}

Date civil_from_days(long long z) {
    z += 719468LL;
    const long long era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    const unsigned  doe = static_cast<unsigned>(z - era * 146097LL);         // [0, 146096]
    const unsigned  yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const long long y   = static_cast<long long>(yoe) + era * 400LL;
    const unsigned  doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);        // [0, 365]
    const unsigned  mp  = (5u * doy + 2u) / 153u;                            // [0, 11]
    const unsigned  d   = doy - (153u * mp + 2u) / 5u + 1u;                  // [1, 31]
    const unsigned  m   = (mp < 10u ? mp + 3u : mp - 9u);                    // [1, 12]

    Date out;
    out.y = static_cast<int>(y + (m <= 2u));
    out.m = static_cast<int>(m);
    out.d = static_cast<int>(d);
    return out;
}

long long days_between(const Date& from, const Date& to) {
    return days_from_civil(to) - days_from_civil(from);
}

Date add_days(const Date& dt, long long n) {
    return civil_from_days(days_from_civil(dt) + n);
}

std::optional<Date> parse_date(std::string_view s) {
    // aceita exatamente YYYY-MM-DD
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return std::nullopt;

    auto num = [&](size_t pos, size_t len) -> std::optional<int> {
        int v = 0;
        for (size_t i = pos; i < pos + len; ++i) {
            if (s[i] < '0' || s[i] > '9') return std::nullopt;
            v = v * 10 + (s[i] - '0');
        }
        return v;
    };

    const auto y = num(0, 4);
    const auto m = num(5, 2);
    const auto d = num(8, 2);
    if (!y || !m || !d) return std::nullopt;
    if (*m < 1 || *m > 12 || *d < 1 || *d > 31) return std::nullopt;

    Date out{*y, *m, *d};
    // rejeita 31 de fevereiro e afins: a ida e volta so bate em data real
    if (!(civil_from_days(days_from_civil(out)) == out)) return std::nullopt;
    return out;
}

std::string format_date(const Date& dt) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", dt.y, dt.m, dt.d);
    return std::string(buf);
}

Date today_local() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return Date{tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
}

// ---------------------------------------------------------------------------

namespace {

struct KindName  { Kind  v; const char* s; };
struct StateName { State v; const char* s; };
struct OutName   { Outcome v; const char* s; };

constexpr std::array<KindName, 3> kKinds{{
    {Kind::Theory, "theory"}, {Kind::Skill, "skill"}, {Kind::Project, "project"}}};

constexpr std::array<StateName, 6> kStates{{
    {State::Locked, "locked"}, {State::Todo, "todo"}, {State::Learning, "learning"},
    {State::Consolidated, "consolidated"}, {State::Maintenance, "maintenance"},
    {State::Archived, "archived"}}};

constexpr std::array<OutName, 3> kOutcomes{{
    {Outcome::Worked, "worked"}, {Outcome::Good, "good"}, {Outcome::Struggled, "struggled"}}};

}  // namespace

std::string to_string(Kind k) {
    for (const auto& e : kKinds)
        if (e.v == k) return e.s;
    return "skill";
}

std::string to_string(State s) {
    for (const auto& e : kStates)
        if (e.v == s) return e.s;
    return "todo";
}

std::string to_string(Outcome o) {
    for (const auto& e : kOutcomes)
        if (e.v == o) return e.s;
    return "worked";
}

std::optional<Kind> kind_from_string(std::string_view s) {
    for (const auto& e : kKinds)
        if (s == e.s) return e.v;
    return std::nullopt;
}

std::optional<State> state_from_string(std::string_view s) {
    for (const auto& e : kStates)
        if (s == e.s) return e.v;
    return std::nullopt;
}

std::optional<Outcome> outcome_from_string(std::string_view s) {
    for (const auto& e : kOutcomes)
        if (s == e.s) return e.v;
    return std::nullopt;
}

}  // namespace st
