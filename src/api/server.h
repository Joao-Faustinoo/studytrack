#pragma once

#include <string>

#include "domain/types.h"
#include "scheduler/scheduler.h"
#include "store/store.h"

namespace st {

struct ServerOptions {
    std::string db_path;
    std::string web_dir;
    std::string config_path;
    std::string host = "0.0.0.0";  // 0.0.0.0 para o celular alcancar pela rede local
    int         port = 8080;
};

// Monta o JSON completo que a interface consome numa unica requisicao.
std::string state_json(const Store& store, const Config& cfg, const Date& today);

// Texto da fila do dia para o terminal.
std::string queue_text(const Store& store, const Config& cfg, const Date& today);

int run_server(const ServerOptions& opts);

}  // namespace st
