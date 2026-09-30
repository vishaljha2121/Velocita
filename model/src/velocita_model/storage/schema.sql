-- Velocita model research store.
--
-- Ordering principle: raw evidence is written before anything derived from it.
-- If the process dies between the two, the recoverable state is "we saw this and
-- have not yet interpreted it", which is replayable. The reverse - a metric with
-- no surviving evidence - is not, and would silently become an unfalsifiable
-- number in a report.
--
-- Everything is UTC nanoseconds since the Unix epoch, stored as INTEGER.

PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA foreign_keys = ON;

-- One row per collector process lifetime. Every raw record points at the
-- session that produced it, so a restart is visible in the data rather than
-- being smoothed over.
CREATE TABLE IF NOT EXISTS collector_session (
    session_id       INTEGER PRIMARY KEY,
    venue            TEXT    NOT NULL,
    started_at_ns    INTEGER NOT NULL,
    ended_at_ns      INTEGER,
    config_digest    TEXT    NOT NULL,   -- sha256 of the resolved config
    schema_digest    TEXT    NOT NULL,   -- sha256 of the feature schema
    code_version     TEXT    NOT NULL,
    clock_synced     INTEGER NOT NULL,   -- 0/1 at session start
    stop_reason      TEXT
);

-- Venue/pair metadata, every field carrying its own provenance. `confidence` is
-- one of 'verified' | 'inferred' | 'unverified' and is never defaulted to
-- 'verified'. A tick size we guessed must not be indistinguishable from one the
-- venue documented.
CREATE TABLE IF NOT EXISTS pair_metadata (
    pair_id          TEXT    NOT NULL,
    field            TEXT    NOT NULL,
    value            TEXT,
    confidence       TEXT    NOT NULL CHECK (confidence IN ('verified','inferred','unverified')),
    source_url       TEXT,
    collected_at_ns  INTEGER NOT NULL,
    PRIMARY KEY (pair_id, field, collected_at_ns)
);

-- Raw top-of-book / depth snapshots exactly as received, normalised only into
-- integer atoms. `raw_digest` names the untouched source payload.
CREATE TABLE IF NOT EXISTS book_snapshot (
    id                  INTEGER PRIMARY KEY,
    session_id          INTEGER NOT NULL REFERENCES collector_session(session_id),
    pair_id             TEXT    NOT NULL,
    source_sequence     INTEGER NOT NULL,
    venue_sequence      INTEGER,
    event_time_ns       INTEGER,          -- venue-supplied; NULL when absent
    receive_time_ns     INTEGER NOT NULL, -- local monotonic-derived wall time
    best_bid_atoms      INTEGER,
    best_ask_atoms      INTEGER,
    bid_size_atoms      INTEGER,
    ask_size_atoms      INTEGER,
    depth_json          TEXT,             -- bounded ladder, as received
    status              TEXT    NOT NULL, -- ready|stale|recovering|crossed|incomplete
    raw_digest          TEXT    NOT NULL
);
CREATE INDEX IF NOT EXISTS book_snapshot_pair_time
    ON book_snapshot (pair_id, receive_time_ns);

-- Public trade prints. `direction` is NULL unless the venue states it; an
-- inferred direction is stored in `direction_inferred` so an analysis can
-- choose to exclude inference rather than unknowingly depend on it.
CREATE TABLE IF NOT EXISTS trade_print (
    id                  INTEGER PRIMARY KEY,
    session_id          INTEGER NOT NULL REFERENCES collector_session(session_id),
    pair_id             TEXT    NOT NULL,
    venue_trade_id      TEXT,
    event_time_ns       INTEGER,
    receive_time_ns     INTEGER NOT NULL,
    price_atoms         INTEGER NOT NULL,
    size_atoms          INTEGER NOT NULL,
    direction           TEXT,
    direction_inferred  TEXT,
    raw_digest          TEXT    NOT NULL,
    UNIQUE (pair_id, venue_trade_id)
);
CREATE INDEX IF NOT EXISTS trade_print_pair_time
    ON trade_print (pair_id, receive_time_ns);

-- Reference / oracle / cross-venue prices, kept separate from venue data so a
-- divergence analysis cannot accidentally compare a feed against itself.
CREATE TABLE IF NOT EXISTS reference_price (
    id                INTEGER PRIMARY KEY,
    session_id        INTEGER NOT NULL REFERENCES collector_session(session_id),
    pair_id           TEXT    NOT NULL,
    source            TEXT    NOT NULL,
    event_time_ns     INTEGER,
    receive_time_ns   INTEGER NOT NULL,
    price_atoms       INTEGER NOT NULL,
    raw_digest        TEXT    NOT NULL
);

-- Intervals during which data is known to be untrustworthy. Analyses exclude
-- these by default. Nothing is ever backfilled into them.
CREATE TABLE IF NOT EXISTS data_quality_interval (
    id                INTEGER PRIMARY KEY,
    session_id        INTEGER NOT NULL REFERENCES collector_session(session_id),
    pair_id           TEXT,
    kind              TEXT    NOT NULL,  -- gap|disconnect|stale|clock_unsync|rate_limit|reorder
    started_at_ns     INTEGER NOT NULL,
    ended_at_ns       INTEGER,
    detail            TEXT
);
CREATE INDEX IF NOT EXISTS data_quality_interval_time
    ON data_quality_interval (pair_id, started_at_ns);

-- Every canonical DecisionFrame the edge produced, stored as the exact bytes.
-- This is the replay corpus: a day of shadow simulation must be reproducible
-- from these rows alone, which is the Stage 4 gate.
CREATE TABLE IF NOT EXISTS decision_frame (
    id                INTEGER PRIMARY KEY,
    session_id        INTEGER NOT NULL REFERENCES collector_session(session_id),
    pair_id           TEXT    NOT NULL,
    source_sequence   INTEGER NOT NULL,
    event_time_ns     INTEGER NOT NULL,
    receive_time_ns   INTEGER NOT NULL,
    payload           BLOB    NOT NULL,
    payload_digest    TEXT    NOT NULL UNIQUE,
    emitted           INTEGER NOT NULL DEFAULT 0  -- reserved: proposed to cluster
);
CREATE INDEX IF NOT EXISTS decision_frame_pair_time
    ON decision_frame (pair_id, event_time_ns);

-- Shadow decisions: what the mirrored evaluator concluded for a frame under a
-- named policy. Never an order.
CREATE TABLE IF NOT EXISTS shadow_decision (
    id                  INTEGER PRIMARY KEY,
    frame_id            INTEGER NOT NULL REFERENCES decision_frame(id),
    policy_digest       TEXT    NOT NULL,
    evaluation_code     INTEGER NOT NULL,
    directive           INTEGER NOT NULL,
    effective_directive INTEGER NOT NULL,
    control_outcome     INTEGER NOT NULL,
    matched_rule_index  INTEGER NOT NULL,
    rules_visited       INTEGER NOT NULL
);

-- Hypothetical quotes. The column names say "shadow" on purpose: no report
-- generated from this table can accidentally read as an execution record.
CREATE TABLE IF NOT EXISTS shadow_quote (
    id                INTEGER PRIMARY KEY,
    decision_id       INTEGER NOT NULL REFERENCES shadow_decision(id),
    pair_id           TEXT    NOT NULL,
    side              INTEGER NOT NULL,
    price_atoms       INTEGER NOT NULL,
    size_atoms        INTEGER NOT NULL,
    fair_value_atoms  INTEGER NOT NULL,
    placed_at_ns      INTEGER NOT NULL,
    withdrawn_at_ns   INTEGER,
    queue_ahead_atoms INTEGER
);

-- Modelled fill opportunities. `assumption` is optimistic|base|conservative and
-- is always populated: a fill count without its queue assumption is not a
-- result, it is a number.
CREATE TABLE IF NOT EXISTS shadow_fill (
    id                  INTEGER PRIMARY KEY,
    quote_id            INTEGER NOT NULL REFERENCES shadow_quote(id),
    assumption          TEXT    NOT NULL CHECK (assumption IN ('optimistic','base','conservative')),
    filled_at_ns        INTEGER NOT NULL,
    filled_size_atoms   INTEGER NOT NULL,
    fill_probability_ppm INTEGER,
    partial             INTEGER NOT NULL DEFAULT 0
);

-- Fill-conditioned markouts. Horizons are stored per row so a missing horizon
-- is visibly missing rather than silently averaged away.
CREATE TABLE IF NOT EXISTS shadow_markout (
    fill_id             INTEGER NOT NULL REFERENCES shadow_fill(id),
    horizon_ms          INTEGER NOT NULL,
    fair_value_atoms    INTEGER NOT NULL,
    markout_bps_micros  INTEGER NOT NULL,
    PRIMARY KEY (fill_id, horizon_ms)
);

-- Risk stops recorded as events, matching the cluster's habit of treating a
-- limit breach as a retained transition rather than a log line.
CREATE TABLE IF NOT EXISTS risk_event (
    id            INTEGER PRIMARY KEY,
    session_id    INTEGER NOT NULL REFERENCES collector_session(session_id),
    pair_id       TEXT,
    at_ns         INTEGER NOT NULL,
    kind          TEXT    NOT NULL,
    detail        TEXT
);

-- Host telemetry: a Pi that thermally throttles mid-run produces latency data
-- that means something different, and the report must be able to say so.
CREATE TABLE IF NOT EXISTS system_health (
    id              INTEGER PRIMARY KEY,
    session_id      INTEGER NOT NULL REFERENCES collector_session(session_id),
    at_ns           INTEGER NOT NULL,
    cpu_percent     REAL,
    mem_percent     REAL,
    disk_percent    REAL,
    db_bytes        INTEGER,
    load1           REAL,
    clock_offset_ms REAL,
    clock_synced    INTEGER,
    throttled_hex   TEXT,
    temp_celsius    REAL
);

-- Market-data stream messages as received (collectors/market_stream.py). Raw
-- evidence first: `body` is the exact text, except for price_candles, where only
-- watched symbols' rows are kept (`filtered` = 1) and `full_digest` names the
-- complete original message. Units are NOT interpreted here; the atom tables
-- above stay empty until tick sizes are verified.
CREATE TABLE IF NOT EXISTS market_message (
    id                INTEGER PRIMARY KEY,
    session_id        INTEGER NOT NULL REFERENCES collector_session(session_id),
    type              TEXT    NOT NULL,   -- venue message type, or 'unparseable'
    topics            TEXT,               -- JSON array as sent by the venue
    server_ts_s       INTEGER,            -- venue `timestamp` (seconds), when present
    receive_time_ns   INTEGER NOT NULL,
    body              TEXT    NOT NULL,
    body_digest       TEXT    NOT NULL,   -- sha256 of body
    full_digest       TEXT    NOT NULL,   -- sha256 of the message as received
    filtered          INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS market_message_type_time ON market_message (type, receive_time_ns);
