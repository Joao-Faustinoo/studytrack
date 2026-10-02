#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "api/server.h"
#include "domain/types.h"
#include "scheduler/scheduler.h"
#include "store/store.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

fs::path exe_dir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
#else
    return fs::current_path();
#endif
}

std::optional<fs::path> variavel_ambiente(const char* nome) {
#if defined(_WIN32)
    // GetEnvironmentVariableW em vez de getenv: o getenv dispara C4996 no MSVC
    // e o projeto compila em /W4 sem advertencia nenhuma.
    const std::wstring wnome(nome, nome + std::strlen(nome));
    const DWORD n = GetEnvironmentVariableW(wnome.c_str(), nullptr, 0);
    if (n == 0) return std::nullopt;
    std::wstring buf(n, L'\0');
    const DWORD m = GetEnvironmentVariableW(wnome.c_str(), buf.data(), n);
    if (m == 0 || m >= n) return std::nullopt;
    buf.resize(m);
    return fs::path(buf);
#else
    const char* v = std::getenv(nome);
    if (!v || !*v) return std::nullopt;
    return fs::path(v);
#endif
}

// O banco mora FORA de build/, de proposito.
//
// Ele ja morou em build/studytrack.db, ao lado do executavel, e aquilo era uma
// armadilha: `build.ps1 -Clean` apaga build/ inteira. Todo o resto ali dentro
// se regenera em 33 segundos; o banco, nao. Misturar o unico arquivo
// insubstituivel do projeto com lixo de compilacao so adia o acidente.
//
// Padrao: <raiz-do-projeto>/data/studytrack.db, ou seja, a pasta data/ irma do
// diretorio do executavel. Sobrevive ao -Clean, fica obvio para backup, e o
// .gitignore ja o cobre por *.db. STUDYTRACK_DB sobrescreve.
fs::path caminho_banco(const fs::path& dir_exe) {
    if (const auto v = variavel_ambiente("STUDYTRACK_DB")) return *v;
    return dir_exe.parent_path() / "data" / "studytrack.db";
}

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

st::Config load_config(const fs::path& p) {
    const std::string t = read_file(p);
    return t.empty() ? st::default_config() : st::parse_config(t);
}

void usage() {
    std::cout <<
        "studytrack - rastreador de estudo\n\n"
        "  studytrack serve [porta]   sobe o servidor (padrao 8080)\n"
        "  studytrack today           mostra a fila do dia no terminal\n"
        "  studytrack seed            importa seeds/*.json (idempotente)\n"
        "  studytrack status          resumo por materia\n"
        "  studytrack deadline <no> <AAAA-MM-DD|-->   define ou limpa prazo\n"
        "  studytrack state <no> <estado>             muda o estado de um no\n\n"
        "estados: locked todo learning consolidated maintenance archived\n\n"
        "O banco fica em data/studytrack.db, FORA de build/, para sobreviver a\n"
        "um `build.ps1 -Clean`. Defina STUDYTRACK_DB para usar outro caminho.\n"
        "Backup = copiar a pasta data/. O git nao versiona o banco.\n";
}

int cmd_seed(st::Store& store, const fs::path& seeds) {
    if (!fs::exists(seeds)) {
        std::cerr << "pasta de seeds nao encontrada: " << seeds.string() << "\n";
        return 1;
    }
    int total = 0;
    for (const auto& e : fs::directory_iterator(seeds)) {
        if (e.path().extension() != ".json") continue;
        const std::string t = read_file(e.path());
        if (t.empty()) continue;
        const int n = store.seed_from_json(t);
        std::cout << "  " << e.path().filename().string() << ": " << n << " nos novos\n";
        total += n;
    }
    std::cout << total << " nos importados no total\n";
    return 0;
}

int cmd_status(const st::Store& store) {
    const auto subs  = store.subjects();
    const auto nodes = store.nodes();

    for (const auto& s : subs) {
        int por_estado[6] = {0, 0, 0, 0, 0, 0};
        int total = 0;
        for (const auto& n : nodes) {
            if (n.subject_id != s.id) continue;
            ++total;
            ++por_estado[static_cast<int>(n.state)];
        }
        std::cout << s.name << "  (" << total << " nos, " << s.session_count << " sessoes, "
                  << s.weekly_minutes << " min/semana)\n";
        std::cout << "    todo " << por_estado[static_cast<int>(st::State::Todo)]
                  << " | estudando " << por_estado[static_cast<int>(st::State::Learning)]
                  << " | consolidado " << por_estado[static_cast<int>(st::State::Consolidated)]
                  << " | manutencao " << por_estado[static_cast<int>(st::State::Maintenance)]
                  << "\n\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif

    const fs::path raiz    = exe_dir();
    const fs::path db      = caminho_banco(raiz);
    const fs::path web     = raiz / "web";
    const fs::path seeds   = raiz / "seeds";
    const fs::path cfgpath = raiz / "config.json";

    std::vector<std::string> args(argv + 1, argv + argc);
    const std::string cmd = args.empty() ? "serve" : args[0];

    try {
        if (cmd == "-h" || cmd == "--help" || cmd == "help") {
            usage();
            return 0;
        }

        // a pasta precisa existir antes do sqlite3_open, que nao a cria
        std::error_code ec;
        fs::create_directories(db.parent_path(), ec);
        if (ec)
            throw std::runtime_error("nao consegui criar " + db.parent_path().string() +
                                     ": " + ec.message());

        st::Store store(db.string());

        // primeira execucao: semeia sozinho, senao o app abre vazio e inutil
        if (store.empty() && fs::exists(seeds)) {
            std::cout << "banco vazio, importando trilhas...\n";
            cmd_seed(store, seeds);
            std::cout << "\n";
        }

        const st::Config cfg   = load_config(cfgpath);
        const st::Date   hoje  = st::today_local();

        if (cmd == "serve") {
            st::ServerOptions o;
            o.db_path     = db.string();
            o.web_dir     = web.string();
            o.config_path = cfgpath.string();
            if (args.size() > 1) o.port = std::stoi(args[1]);
            return st::run_server(o);
        }
        if (cmd == "today") {
            std::cout << st::queue_text(store, cfg, hoje);
            return 0;
        }
        if (cmd == "seed")   return cmd_seed(store, seeds);
        if (cmd == "status") return cmd_status(store);

        if (cmd == "deadline") {
            if (args.size() < 3) { usage(); return 1; }
            if (args[2] == "-") {
                store.set_node_deadline(args[1], std::nullopt);
                std::cout << "prazo removido de " << args[1] << "\n";
                return 0;
            }
            const auto d = st::parse_date(args[2]);
            if (!d) { std::cerr << "data invalida, use AAAA-MM-DD\n"; return 1; }
            store.set_node_deadline(args[1], d);
            std::cout << "prazo de " << args[1] << " = " << st::format_date(*d) << "\n";
            return 0;
        }

        if (cmd == "state") {
            if (args.size() < 3) { usage(); return 1; }
            const auto s = st::state_from_string(args[2]);
            if (!s) { std::cerr << "estado invalido\n"; return 1; }
            store.set_node_state(args[1], *s, hoje);
            std::cout << args[1] << " -> " << args[2] << "\n";
            return 0;
        }

        std::cerr << "comando desconhecido: " << cmd << "\n\n";
        usage();
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "erro: " << e.what() << "\n";
        return 1;
    }
}
