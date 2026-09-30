#include "api/server.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>

#include "graph/graph.h"
#include "httplib.h"
#include "json.hpp"

namespace st {

namespace {

using json = nlohmann::json;

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

json node_json(const Node& n, const NodeIndex& idx) {
    json j;
    j["id"]          = n.id;
    j["subject_id"]  = n.subject_id;
    j["title"]       = n.title;
    j["kind"]        = to_string(n.kind);
    j["state"]       = to_string(n.state);
    j["criterion"]   = n.criterion;
    j["notes"]       = n.notes;
    j["est_minutes"] = n.est_minutes;
    j["prereqs"]     = n.prereqs;
    j["unlocked"]    = is_unlocked(n, idx);
    j["missing"]     = missing_prereqs(n, idx);

    if (n.maintenance_sessions) j["maintenance_sessions"] = *n.maintenance_sessions;
    if (n.last_touched_session) j["last_touched_session"] = *n.last_touched_session;
    if (n.srs_interval_days)    j["srs_interval_days"]    = *n.srs_interval_days;
    if (n.srs_due_on)           j["srs_due_on"]           = format_date(*n.srs_due_on);
    if (n.deadline)             j["deadline"]             = format_date(*n.deadline);
    j["srs_lapses"] = n.srs_lapses;
    return j;
}

SubjectMap subject_map(const std::vector<Subject>& v) {
    SubjectMap m;
    for (const auto& s : v) m.emplace(s.id, s);
    return m;
}

}  // namespace

std::string state_json(const Store& store, const Config& cfg, const Date& today) {
    const auto nodes = store.nodes();
    const auto subs  = store.subjects();
    const auto smap  = subject_map(subs);
    const auto idx   = index_by_id(nodes);
    const auto fila  = build_queue(nodes, smap, cfg, today);

    json j;
    j["today"] = format_date(today);

    j["subjects"] = json::array();
    for (const auto& s : subs) {
        json sj;
        sj["id"]             = s.id;
        sj["name"]           = s.name;
        sj["color"]          = s.color;
        sj["weekly_minutes"] = s.weekly_minutes;
        sj["session_count"]  = s.session_count;
        j["subjects"].push_back(std::move(sj));
    }

    j["nodes"] = json::array();
    for (const auto& n : nodes) j["nodes"].push_back(node_json(n, idx));

    j["queue"] = json::array();
    for (const auto& c : fila) {
        json q;
        q["node_id"] = c.node->id;
        q["title"]   = c.node->title;
        q["subject"] = c.node->subject_id;
        q["kind"]    = to_string(c.node->kind);
        q["minutes"] = c.node->est_minutes;
        q["score"]   = c.score;
        q["reason"]  = to_string(c.reason);
        q["detail"]  = c.detail;
        j["queue"].push_back(std::move(q));
    }
    return j.dump();
}

std::string queue_text(const Store& store, const Config& cfg, const Date& today) {
    const auto nodes = store.nodes();
    const auto subs  = store.subjects();
    const auto smap  = subject_map(subs);
    const auto fila  = build_queue(nodes, smap, cfg, today);

    std::unordered_map<std::string, std::string> nome;
    for (const auto& s : subs) nome[s.id] = s.name;

    std::ostringstream out;
    out << "fila de " << format_date(today) << "\n\n";
    if (fila.empty()) {
        out << "  nada na fila. Tudo em dia.\n";
        return out.str();
    }
    int i = 1;
    for (const auto& c : fila) {
        out << "  " << i++ << ". [" << nome[c.node->subject_id] << "] " << c.node->title << "\n";
        out << "     " << c.node->est_minutes << " min  -  " << to_string(c.reason);
        if (!c.detail.empty()) out << ": " << c.detail;
        out << "\n";
        if (!c.node->criterion.empty()) out << "     criterio: " << c.node->criterion << "\n";
        out << "\n";
    }
    return out.str();
}

int run_server(const ServerOptions& opts) {
    Store store(opts.db_path);

    Config cfg = default_config();
    const std::string cfg_text = read_file(opts.config_path);
    if (!cfg_text.empty()) cfg = parse_config(cfg_text);

    httplib::Server svr;

    svr.set_mount_point("/", opts.web_dir);

    svr.Get("/api/state", [&](const httplib::Request&, httplib::Response& res) {
        try {
            res.set_content(state_json(store, cfg, today_local()), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post("/api/session", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            json b = json::parse(req.body, nullptr, false);
            if (b.is_discarded()) throw std::runtime_error("JSON invalido");

            Session s;
            s.subject_id   = b.value("subject_id", "");
            s.duration_min = b.value("duration_min", 0);
            s.note         = b.value("note", "");
            s.started_at   = b.value("started_at", format_date(today_local()) + " 00:00:00");

            if (s.subject_id.empty()) throw std::runtime_error("subject_id obrigatorio");

            if (b.contains("nodes"))
                for (const auto& n : b["nodes"]) {
                    SessionNode sn;
                    if (n.is_string()) {
                        sn.node_id = n.get<std::string>();
                    } else {
                        sn.node_id = n.value("id", "");
                        sn.outcome = outcome_from_string(n.value("outcome", "worked"))
                                         .value_or(Outcome::Worked);
                    }
                    if (!sn.node_id.empty()) s.nodes.push_back(sn);
                }

            if (b.contains("metrics") && b["metrics"].is_object())
                for (auto it = b["metrics"].begin(); it != b["metrics"].end(); ++it)
                    if (it.value().is_number())
                        s.metrics.emplace_back(it.key(), it.value().get<double>());

            const long long id = store.log_session(s, cfg, today_local());
            res.set_content(json{{"ok", true}, {"session_id", id}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post("/api/node/state", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            json b = json::parse(req.body, nullptr, false);
            if (b.is_discarded()) throw std::runtime_error("JSON invalido");
            const auto st8 = state_from_string(b.value("state", ""));
            if (!st8) throw std::runtime_error("estado invalido");
            store.set_node_state(b.value("id", ""), *st8, today_local());
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post("/api/node/deadline", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            json b = json::parse(req.body, nullptr, false);
            if (b.is_discarded()) throw std::runtime_error("JSON invalido");
            std::optional<Date> d;
            if (b.contains("deadline") && b["deadline"].is_string()) {
                d = parse_date(b["deadline"].get<std::string>());
                if (!d) throw std::runtime_error("data invalida (use AAAA-MM-DD)");
            }
            store.set_node_deadline(b.value("id", ""), d);
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    std::cout << "studytrack em http://localhost:" << opts.port << "\n"
              << "banco: " << opts.db_path << "\n"
              << "web:   " << opts.web_dir << "\n"
              << "(o celular alcanca pelo IP desta maquina na mesma rede)\n\n"
              << "Ctrl+C para parar.\n";

    if (!svr.listen(opts.host.c_str(), opts.port)) {
        std::cerr << "nao consegui abrir a porta " << opts.port << "\n";
        return 1;
    }
    return 0;
}

}  // namespace st
