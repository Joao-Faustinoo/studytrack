-- studytrack — esquema SQLite
--
-- Duas decisões de design que explicam quase tudo aqui:
--
-- 1. MANUTENÇÃO é contada em SESSÕES DA MATÉRIA, não em dias.
--    "escala não tocada há 3 sessões" e não "há 3 dias". Quem estuda de
--    forma irregular não pode ser punido pelo calendário: cinco sessões de
--    C++ não devem envelhecer nada do fagote.
--
-- 2. REVISÃO ESPAÇADA é contada em DIAS reais, porque memória decai com
--    tempo de relógio, não com esforço. O acúmulo é neutralizado no
--    scheduler por saturação, nunca deixando a fila virar lista de dívida.

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS subject (
    id             TEXT PRIMARY KEY,
    name           TEXT NOT NULL,
    color          TEXT NOT NULL DEFAULT '#888888',
    weekly_minutes INTEGER NOT NULL DEFAULT 0,
    -- contador monotônico de sessões desta matéria; base da manutenção
    session_count  INTEGER NOT NULL DEFAULT 0,
    sort_order     INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS node (
    id          TEXT PRIMARY KEY,
    subject_id  TEXT NOT NULL REFERENCES subject(id) ON DELETE CASCADE,
    title       TEXT NOT NULL,

    -- theory  : consolida e entra em revisão espaçada (decai em dias)
    -- skill   : nunca termina, entra em manutenção (decai em sessões)
    -- project : só fecha com artefato; não decai
    kind        TEXT NOT NULL CHECK (kind IN ('theory','skill','project')),

    state       TEXT NOT NULL DEFAULT 'todo'
                CHECK (state IN ('locked','todo','learning','consolidated','maintenance','archived')),

    -- sem professor, o critério de "consolidado" precisa estar escrito
    criterion   TEXT NOT NULL DEFAULT '',
    notes       TEXT NOT NULL DEFAULT '',
    est_minutes INTEGER NOT NULL DEFAULT 20,
    sort_order  INTEGER NOT NULL DEFAULT 0,

    -- kind='skill'
    maintenance_sessions INTEGER,  -- janela: quantas sessões da matéria até voltar à fila
    last_touched_session INTEGER,  -- valor de subject.session_count na última vez

    -- kind='theory'
    srs_interval_days INTEGER,
    srs_due_on        TEXT,        -- ISO-8601 date
    srs_lapses        INTEGER NOT NULL DEFAULT 0,

    -- kind='project'
    deadline    TEXT,              -- ISO-8601 date; NULL = sem prazo

    consolidated_at TEXT,
    last_touched_at TEXT,
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

-- o grafo de pré-requisitos (DAG)
CREATE TABLE IF NOT EXISTS node_edge (
    prereq_id TEXT NOT NULL REFERENCES node(id) ON DELETE CASCADE,
    node_id   TEXT NOT NULL REFERENCES node(id) ON DELETE CASCADE,
    PRIMARY KEY (prereq_id, node_id)
);

CREATE TABLE IF NOT EXISTS session (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    subject_id         TEXT NOT NULL REFERENCES subject(id) ON DELETE CASCADE,
    started_at         TEXT NOT NULL,
    duration_min       INTEGER NOT NULL,
    note               TEXT NOT NULL DEFAULT '',
    subject_session_no INTEGER NOT NULL  -- snapshot de subject.session_count
);

CREATE TABLE IF NOT EXISTS session_node (
    session_id INTEGER NOT NULL REFERENCES session(id) ON DELETE CASCADE,
    node_id    TEXT NOT NULL REFERENCES node(id) ON DELETE CASCADE,
    outcome    TEXT NOT NULL DEFAULT 'worked'
               CHECK (outcome IN ('worked','good','struggled')),
    PRIMARY KEY (session_id, node_id)
);

-- métrica numérica por sessão: 'bpm', 'frames', 'problems', ...
CREATE TABLE IF NOT EXISTS session_metric (
    session_id INTEGER NOT NULL REFERENCES session(id) ON DELETE CASCADE,
    key        TEXT NOT NULL,
    value      REAL NOT NULL,
    PRIMARY KEY (session_id, key)
);

-- v3: gravações, GIFs, commits. A tabela nasce agora porque custa nada.
CREATE TABLE IF NOT EXISTS artifact (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    node_id    TEXT NOT NULL REFERENCES node(id) ON DELETE CASCADE,
    session_id INTEGER REFERENCES session(id) ON DELETE SET NULL,
    kind       TEXT NOT NULL CHECK (kind IN ('audio','video','image','link','file')),
    path       TEXT NOT NULL,
    label      TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS config (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_node_subject   ON node(subject_id);
CREATE INDEX IF NOT EXISTS idx_node_state     ON node(state);
CREATE INDEX IF NOT EXISTS idx_edge_node      ON node_edge(node_id);
CREATE INDEX IF NOT EXISTS idx_edge_prereq    ON node_edge(prereq_id);
CREATE INDEX IF NOT EXISTS idx_session_subj   ON session(subject_id, started_at);
CREATE INDEX IF NOT EXISTS idx_sess_node_node ON session_node(node_id);
