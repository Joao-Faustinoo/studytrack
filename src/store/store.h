#pragma once

#include <optional>
#include <string>
#include <vector>

#include "domain/types.h"
#include "scheduler/scheduler.h"

struct sqlite3;

// ---------------------------------------------------------------------------
// Persistencia em SQLite.
//
// AVISO DELIBERADO: este modulo usa a API C do SQLite no modo cru, com
// sqlite3_finalize manual em cada consulta. E feio de proposito. Envolver tudo
// em tipos RAII (conexao e statement se fechando sozinhos) e o no
// `cpp.proj.sqlite-raii` da trilha de C++ - a API do SQLite e cheia de recursos
// para vazar, o que faz dela um exercicio de RAII quase perfeito.
// ---------------------------------------------------------------------------
namespace st {

class Store {
public:
    explicit Store(const std::string& db_path);
    ~Store();

    Store(const Store&)            = delete;
    Store& operator=(const Store&) = delete;
    Store(Store&&)                 = delete;
    Store& operator=(Store&&)      = delete;

    bool empty() const;

    // Importa um seeds/*.json. Idempotente: no ja existente e ignorado, entao
    // reimportar nao apaga progresso.
    int seed_from_json(const std::string& json_text);

    std::vector<Subject> subjects() const;
    std::vector<Node>    nodes() const;
    std::optional<Node>  node(const std::string& id) const;

    // Registra a sessao e aplica os efeitos colaterais no grafo:
    //  - incrementa o contador de sessoes da materia (base da manutencao)
    //  - marca os nos tocados
    //  - avanca a revisao espacada dos nos de teoria
    //  - promove Todo -> Learning (consolidar continua sendo decisao manual)
    long long log_session(const Session& s, const Config& cfg, const Date& today);

    void set_node_state(const std::string& node_id, State state, const Date& today);
    void set_node_deadline(const std::string& node_id, const std::optional<Date>& deadline);

private:
    void exec(const char* sql);
    [[noreturn]] void fail(const char* what) const;

    sqlite3* db_ = nullptr;
};

}  // namespace st
