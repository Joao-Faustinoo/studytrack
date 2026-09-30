#include "store/store.h"

#include <stdexcept>

#include "json.hpp"
#include "schema_sql.h"
#include "sqlite3.h"

namespace st {

namespace {

std::string col_text(sqlite3_stmt* st, int i) {
    const unsigned char* p = sqlite3_column_text(st, i);
    return p ? reinterpret_cast<const char*>(p) : std::string();
}

std::optional<int> col_opt_int(sqlite3_stmt* st, int i) {
    if (sqlite3_column_type(st, i) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_int(st, i);
}

std::optional<Date> col_opt_date(sqlite3_stmt* st, int i) {
    if (sqlite3_column_type(st, i) == SQLITE_NULL) return std::nullopt;
    return parse_date(col_text(st, i));
}

void bind_text(sqlite3_stmt* st, int i, const std::string& v) {
    sqlite3_bind_text(st, i, v.c_str(), -1, SQLITE_TRANSIENT);
}

void bind_opt_int(sqlite3_stmt* st, int i, const std::optional<int>& v) {
    if (v) sqlite3_bind_int(st, i, *v);
    else   sqlite3_bind_null(st, i);
}

void bind_opt_date(sqlite3_stmt* st, int i, const std::optional<Date>& v) {
    if (v) {
        const std::string s = format_date(*v);
        sqlite3_bind_text(st, i, s.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(st, i);
    }
}

constexpr const char* kNodeCols =
    "id, subject_id, title, kind, state, criterion, notes, est_minutes, sort_order,"
    " maintenance_sessions, last_touched_session, srs_interval_days, srs_due_on,"
    " srs_lapses, deadline";

Node read_node(sqlite3_stmt* st) {
    Node n;
    n.id         = col_text(st, 0);
    n.subject_id = col_text(st, 1);
    n.title      = col_text(st, 2);
    n.kind       = kind_from_string(col_text(st, 3)).value_or(Kind::Skill);
    n.state      = state_from_string(col_text(st, 4)).value_or(State::Todo);
    n.criterion  = col_text(st, 5);
    n.notes      = col_text(st, 6);
    n.est_minutes = sqlite3_column_int(st, 7);
    n.sort_order  = sqlite3_column_int(st, 8);
    n.maintenance_sessions = col_opt_int(st, 9);
    n.last_touched_session = col_opt_int(st, 10);
    n.srs_interval_days    = col_opt_int(st, 11);
    n.srs_due_on           = col_opt_date(st, 12);
    n.srs_lapses           = sqlite3_column_int(st, 13);
    n.deadline             = col_opt_date(st, 14);
    return n;
}

}  // namespace

Store::Store(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        const std::string msg = db_ ? sqlite3_errmsg(db_) : "sqlite3_open falhou";
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("nao consegui abrir o banco: " + msg);
    }
    exec("PRAGMA foreign_keys = ON;");
    exec("PRAGMA journal_mode = WAL;");
    exec(kSchemaSql);
}

Store::~Store() {
    if (db_) sqlite3_close(db_);
}

void Store::fail(const char* what) const {
    throw std::runtime_error(std::string(what) + ": " +
                             (db_ ? sqlite3_errmsg(db_) : "sem conexao"));
}

void Store::exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err ? err : "erro desconhecido";
        sqlite3_free(err);
        throw std::runtime_error("SQL falhou: " + msg);
    }
}

bool Store::empty() const {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM node;", -1, &st, nullptr) != SQLITE_OK)
        fail("prepare empty");
    int n = 0;
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n == 0;
}

int Store::seed_from_json(const std::string& json_text) {
    nlohmann::json j = nlohmann::json::parse(json_text, nullptr, false);
    if (j.is_discarded()) throw std::runtime_error("seed JSON invalido");
    if (!j.contains("subject") || !j.contains("nodes"))
        throw std::runtime_error("seed sem 'subject' ou 'nodes'");

    exec("BEGIN;");

    try {
        const auto& sj = j["subject"];

        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
                "INSERT OR IGNORE INTO subject (id, name, color, weekly_minutes, sort_order)"
                " VALUES (?,?,?,?,?);", -1, &st, nullptr) != SQLITE_OK)
            fail("prepare subject");
        bind_text(st, 1, sj.value("id", ""));
        bind_text(st, 2, sj.value("name", ""));
        bind_text(st, 3, sj.value("color", "#888888"));
        sqlite3_bind_int(st, 4, sj.value("weekly_minutes", 0));
        sqlite3_bind_int(st, 5, sj.value("sort_order", 0));
        if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); fail("insert subject"); }
        sqlite3_finalize(st);

        const std::string subject_id = sj.value("id", "");
        int inseridos = 0;
        int ordem     = 0;

        for (const auto& nj : j["nodes"]) {
            sqlite3_stmt* ins = nullptr;
            if (sqlite3_prepare_v2(db_,
                    "INSERT OR IGNORE INTO node"
                    " (id, subject_id, title, kind, state, criterion, notes, est_minutes,"
                    "  sort_order, maintenance_sessions, srs_interval_days, deadline)"
                    " VALUES (?,?,?,?,?,?,?,?,?,?,?,?);", -1, &ins, nullptr) != SQLITE_OK)
                fail("prepare node");

            bind_text(ins, 1, nj.value("id", ""));
            bind_text(ins, 2, subject_id);
            bind_text(ins, 3, nj.value("title", ""));
            bind_text(ins, 4, nj.value("kind", "skill"));
            bind_text(ins, 5, nj.value("state", "todo"));
            bind_text(ins, 6, nj.value("criterion", ""));
            bind_text(ins, 7, nj.value("notes", ""));
            sqlite3_bind_int(ins, 8, nj.value("est_minutes", 20));
            sqlite3_bind_int(ins, 9, ordem++);

            if (nj.contains("maintenance_sessions") && nj["maintenance_sessions"].is_number())
                sqlite3_bind_int(ins, 10, nj["maintenance_sessions"].get<int>());
            else
                sqlite3_bind_null(ins, 10);

            if (nj.contains("srs_interval_days") && nj["srs_interval_days"].is_number())
                sqlite3_bind_int(ins, 11, nj["srs_interval_days"].get<int>());
            else
                sqlite3_bind_null(ins, 11);

            if (nj.contains("deadline") && nj["deadline"].is_string())
                bind_text(ins, 12, nj["deadline"].get<std::string>());
            else
                sqlite3_bind_null(ins, 12);

            if (sqlite3_step(ins) != SQLITE_DONE) { sqlite3_finalize(ins); fail("insert node"); }
            if (sqlite3_changes(db_) > 0) ++inseridos;
            sqlite3_finalize(ins);
        }

        // arestas numa segunda passada: os alvos ja existem
        for (const auto& nj : j["nodes"]) {
            if (!nj.contains("prereqs")) continue;
            for (const auto& p : nj["prereqs"]) {
                sqlite3_stmt* e = nullptr;
                if (sqlite3_prepare_v2(db_,
                        "INSERT OR IGNORE INTO node_edge (prereq_id, node_id) VALUES (?,?);",
                        -1, &e, nullptr) != SQLITE_OK)
                    fail("prepare edge");
                bind_text(e, 1, p.get<std::string>());
                bind_text(e, 2, nj.value("id", ""));
                sqlite3_step(e);
                sqlite3_finalize(e);
            }
        }

        exec("COMMIT;");
        return inseridos;
    } catch (...) {
        exec("ROLLBACK;");
        throw;
    }
}

std::vector<Subject> Store::subjects() const {
    std::vector<Subject> out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
            "SELECT id, name, color, weekly_minutes, session_count, sort_order"
            " FROM subject ORDER BY sort_order, name;", -1, &st, nullptr) != SQLITE_OK)
        fail("prepare subjects");

    while (sqlite3_step(st) == SQLITE_ROW) {
        Subject s;
        s.id             = col_text(st, 0);
        s.name           = col_text(st, 1);
        s.color          = col_text(st, 2);
        s.weekly_minutes = sqlite3_column_int(st, 3);
        s.session_count  = sqlite3_column_int(st, 4);
        s.sort_order     = sqlite3_column_int(st, 5);
        out.push_back(std::move(s));
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<Node> Store::nodes() const {
    std::vector<Node> out;

    sqlite3_stmt* st = nullptr;
    const std::string sql = std::string("SELECT ") + kNodeCols + " FROM node ORDER BY sort_order, id;";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
        fail("prepare nodes");
    while (sqlite3_step(st) == SQLITE_ROW) out.push_back(read_node(st));
    sqlite3_finalize(st);

    // arestas
    sqlite3_stmt* e = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT node_id, prereq_id FROM node_edge;", -1, &e, nullptr) != SQLITE_OK)
        fail("prepare edges");

    std::unordered_map<std::string, std::vector<std::string>> pre;
    while (sqlite3_step(e) == SQLITE_ROW)
        pre[col_text(e, 0)].push_back(col_text(e, 1));
    sqlite3_finalize(e);

    for (auto& n : out) {
        const auto it = pre.find(n.id);
        if (it != pre.end()) n.prereqs = it->second;
    }
    return out;
}

std::optional<Node> Store::node(const std::string& id) const {
    sqlite3_stmt* st = nullptr;
    const std::string sql = std::string("SELECT ") + kNodeCols + " FROM node WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
        fail("prepare node");
    bind_text(st, 1, id);

    std::optional<Node> out;
    if (sqlite3_step(st) == SQLITE_ROW) out = read_node(st);
    sqlite3_finalize(st);

    if (out) {
        sqlite3_stmt* e = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT prereq_id FROM node_edge WHERE node_id = ?;",
                               -1, &e, nullptr) == SQLITE_OK) {
            bind_text(e, 1, id);
            while (sqlite3_step(e) == SQLITE_ROW) out->prereqs.push_back(col_text(e, 0));
            sqlite3_finalize(e);
        }
    }
    return out;
}

long long Store::log_session(const Session& s, const Config& cfg, const Date& today) {
    exec("BEGIN;");
    try {
        // 1. contador de sessoes da materia: a base de toda a manutencao
        {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db_,
                    "UPDATE subject SET session_count = session_count + 1 WHERE id = ?;",
                    -1, &st, nullptr) != SQLITE_OK)
                fail("prepare bump");
            bind_text(st, 1, s.subject_id);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }

        int novo_no = 0;
        {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db_, "SELECT session_count FROM subject WHERE id = ?;",
                                   -1, &st, nullptr) != SQLITE_OK)
                fail("prepare count");
            bind_text(st, 1, s.subject_id);
            if (sqlite3_step(st) == SQLITE_ROW) novo_no = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }

        // 2. a sessao
        long long session_id = 0;
        {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db_,
                    "INSERT INTO session (subject_id, started_at, duration_min, note,"
                    " subject_session_no) VALUES (?,?,?,?,?);", -1, &st, nullptr) != SQLITE_OK)
                fail("prepare session");
            bind_text(st, 1, s.subject_id);
            bind_text(st, 2, s.started_at);
            sqlite3_bind_int(st, 3, s.duration_min);
            bind_text(st, 4, s.note);
            sqlite3_bind_int(st, 5, novo_no);
            if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); fail("insert session"); }
            sqlite3_finalize(st);
            session_id = sqlite3_last_insert_rowid(db_);
        }

        // 3. nos tocados + efeitos
        for (const auto& sn : s.nodes) {
            {
                sqlite3_stmt* st = nullptr;
                if (sqlite3_prepare_v2(db_,
                        "INSERT OR REPLACE INTO session_node (session_id, node_id, outcome)"
                        " VALUES (?,?,?);", -1, &st, nullptr) != SQLITE_OK)
                    fail("prepare session_node");
                sqlite3_bind_int64(st, 1, session_id);
                bind_text(st, 2, sn.node_id);
                bind_text(st, 3, to_string(sn.outcome));
                sqlite3_step(st);
                sqlite3_finalize(st);
            }

            const auto atual = node(sn.node_id);
            if (!atual) continue;

            Node n = *atual;

            n.last_touched_session = novo_no;
            if (n.kind == Kind::Theory) {
                const int base = n.srs_interval_days.value_or(cfg.srs_intervals.empty()
                                                                  ? 1
                                                                  : cfg.srs_intervals.front());
                const int prox = next_srs_interval(base, sn.outcome, cfg);
                n.srs_interval_days = prox;
                n.srs_due_on        = add_days(today, prox);
                if (sn.outcome == Outcome::Struggled) ++n.srs_lapses;
            }
            // Todo -> Learning e automatico. Consolidar continua manual: o
            // criterio de "consolidado" e subjetivo e escrito no proprio no.
            if (n.state == State::Todo) n.state = State::Learning;

            sqlite3_stmt* up = nullptr;
            if (sqlite3_prepare_v2(db_,
                    "UPDATE node SET state = ?, last_touched_session = ?, srs_interval_days = ?,"
                    " srs_due_on = ?, srs_lapses = ?, last_touched_at = ? WHERE id = ?;",
                    -1, &up, nullptr) != SQLITE_OK)
                fail("prepare node update");
            bind_text(up, 1, to_string(n.state));
            bind_opt_int(up, 2, n.last_touched_session);
            bind_opt_int(up, 3, n.srs_interval_days);
            bind_opt_date(up, 4, n.srs_due_on);
            sqlite3_bind_int(up, 5, n.srs_lapses);
            bind_text(up, 6, s.started_at);
            bind_text(up, 7, n.id);
            sqlite3_step(up);
            sqlite3_finalize(up);
        }

        // 4. metricas
        for (const auto& m : s.metrics) {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db_,
                    "INSERT OR REPLACE INTO session_metric (session_id, key, value)"
                    " VALUES (?,?,?);", -1, &st, nullptr) != SQLITE_OK)
                fail("prepare metric");
            sqlite3_bind_int64(st, 1, session_id);
            bind_text(st, 2, m.first);
            sqlite3_bind_double(st, 3, m.second);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }

        exec("COMMIT;");
        return session_id;
    } catch (...) {
        exec("ROLLBACK;");
        throw;
    }
}

void Store::set_node_state(const std::string& node_id, State state, const Date& today) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
            "UPDATE node SET state = ?, consolidated_at = CASE WHEN ? IN"
            " ('consolidated','maintenance') THEN ? ELSE consolidated_at END WHERE id = ?;",
            -1, &st, nullptr) != SQLITE_OK)
        fail("prepare set_state");

    const std::string s = to_string(state);
    bind_text(st, 1, s);
    bind_text(st, 2, s);
    bind_text(st, 3, format_date(today));
    bind_text(st, 4, node_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void Store::set_node_deadline(const std::string& node_id, const std::optional<Date>& deadline) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "UPDATE node SET deadline = ? WHERE id = ?;",
                           -1, &st, nullptr) != SQLITE_OK)
        fail("prepare set_deadline");
    bind_opt_date(st, 1, deadline);
    bind_text(st, 2, node_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

}  // namespace st
