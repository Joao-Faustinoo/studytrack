cmake_minimum_required(VERSION 3.20)
project(studytrack LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

# Flags por TARGET, nunca por diretorio.
#
# add_compile_options() aqui em cima parecia mais pratico e custou uma hora de
# depuracao: o /permissive- vazava para o sqlite3 e para o Catch2, e o Catch2
# 3.16 nao compila com /permissive- neste MSVC 19.29 (22 erros de operator<<
# ambiguo; sem a flag, zero). Codigo de terceiros nao deve herdar o nosso rigor.
add_library(st_strict INTERFACE)
add_library(st_relaxed INTERFACE)   # para TUs que incluem Catch2
add_library(st_vendor INTERFACE)    # para codigo de terceiros

if(MSVC)
    # /utf-8 e obrigatorio em todos: os seeds e as mensagens tem acento.
    # /external:templates- e necessario junto do /external:W0: sem ele o MSVC
    # usa o nivel de advertencia do ponto onde o template e INSTANCIADO (nosso
    # codigo) em vez de onde ele e DEFINIDO (o header de terceiros).
    target_compile_options(st_strict  INTERFACE /utf-8 /W4 /permissive- /Zc:__cplusplus
                                                /external:W0 /external:templates-)
    target_compile_options(st_relaxed INTERFACE /utf-8 /W4 /Zc:__cplusplus
                                                /external:W0 /external:templates-)
    target_compile_options(st_vendor  INTERFACE /utf-8 /W0)
endif()

# ---------------------------------------------------------------------------
# schema.sql vira header embutido, para o binario nao depender de arquivo solto
# ---------------------------------------------------------------------------
set(SCHEMA_SQL "${CMAKE_CURRENT_SOURCE_DIR}/src/store/schema.sql")
file(READ "${SCHEMA_SQL}" SCHEMA_CONTENT)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
file(WRITE "${CMAKE_BINARY_DIR}/generated/schema_sql.h"
"#pragma once
// GERADO PELO CMAKE a partir de src/store/schema.sql - nao editar.
namespace st {
inline constexpr const char* kSchemaSql = R\"SCHEMA(
${SCHEMA_CONTENT}
)SCHEMA\";
}
")
# faz o CMake reconfigurar sozinho quando o schema mudar
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SCHEMA_SQL}")

# ---------------------------------------------------------------------------
# dependencias embutidas (third_party/) - nenhuma rede durante o build
# ---------------------------------------------------------------------------
add_library(sqlite3 STATIC third_party/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_THREADSAFE=1
    SQLITE_DQS=0)
target_link_libraries(sqlite3 PRIVATE st_vendor)

# ---------------------------------------------------------------------------
# nucleo
# ---------------------------------------------------------------------------
add_library(st_core STATIC
    src/domain/types.cpp
    src/graph/graph.cpp
    src/scheduler/scheduler.cpp
    src/store/store.cpp
    src/api/server.cpp)

target_include_directories(st_core PUBLIC
    src
    "${CMAKE_BINARY_DIR}/generated")

# SYSTEM: as advertencias do httplib.h e do json.hpp nao sao problema nosso e
# nao devem poluir todo build. Com MSVC isso vira /external:I, que so faz
# efeito junto do /external:W0 definido em st_strict.
target_include_directories(st_core SYSTEM PUBLIC third_party)

target_link_libraries(st_core PUBLIC sqlite3 PRIVATE st_strict)

# C4127 vem de dentro do httplib.h. O par /external:W0 + /external:templates-
# acima deveria bastar, mas nao cobre este caso no MSVC 19.29 - testado. Entao
# a supressao e cirurgica: so este codigo, so esta advertencia. O resto do
# projeto continua em /W4 sem excecao.
if(MSVC)
    set_source_files_properties(src/api/server.cpp PROPERTIES COMPILE_OPTIONS /wd4127)
endif()
if(WIN32)
    target_link_libraries(st_core PUBLIC ws2_32)
endif()

# ---------------------------------------------------------------------------
# executavel
# ---------------------------------------------------------------------------
add_executable(studytrack src/cli/main.cpp)
target_link_libraries(studytrack PRIVATE st_core st_strict)

# Recursos ao lado do binario.
#
# Alvo proprio com ALL em vez de POST_BUILD: o POST_BUILD so dispara quando o
# executavel religa, entao mexer apenas no CSS ou num seed deixava a copia
# desatualizada e o navegador servindo a versao antiga.
add_custom_target(st_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/web" "$<TARGET_FILE_DIR:studytrack>/web"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/seeds" "$<TARGET_FILE_DIR:studytrack>/seeds"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config.json" "$<TARGET_FILE_DIR:studytrack>/config.json"
    COMMENT "copiando web/, seeds/ e config.json"
    VERBATIM)
add_dependencies(st_assets studytrack)

# ---------------------------------------------------------------------------
# testes
# ---------------------------------------------------------------------------
# Catch2 3.16 neste MSVC 19.29 (VS2019) so compila com C++17 E sem
# /permissive-. Matriz medida nesta maquina, erros C2593 de operator<< ambiguo:
#
#                sem /permissive-   com /permissive-
#   C++17              0                  22
#   C++20             22                  22
#
# Qualquer uma das duas flags sozinha ja quebra. O st_core e o executavel
# seguem em C++20 com /permissive-; so os alvos que tocam no Catch2 recuam.
# Misturar C++17 e C++20 entre TUs do mesmo toolset e seguro aqui: os headers
# de src/domain usam apenas recursos de C++17.
add_library(catch2 STATIC third_party/catch_amalgamated.cpp)
target_include_directories(catch2 PUBLIC third_party)
target_link_libraries(catch2 PRIVATE st_vendor)
set_target_properties(catch2 PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

add_executable(st_tests tests/test_core.cpp)
target_link_libraries(st_tests PRIVATE st_core catch2 st_relaxed)
set_target_properties(st_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

enable_testing()
add_test(NAME unit COMMAND st_tests)
