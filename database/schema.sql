-- OMS V1 Database Schema
-- PostgreSQL production schema for institutional order management

BEGIN;

-- ============================================================================
-- ACCOUNTS
-- ============================================================================
CREATE TABLE IF NOT EXISTS accounts (
    id              VARCHAR(64) PRIMARY KEY,
    broker          VARCHAR(64) NOT NULL DEFAULT '',
    exchange        VARCHAR(64) NOT NULL DEFAULT '',
    api_key_name    VARCHAR(128) NOT NULL DEFAULT '',
    enabled         BOOLEAN NOT NULL DEFAULT true,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ============================================================================
-- ORDERS
-- ============================================================================
CREATE TABLE IF NOT EXISTS orders (
    id                  VARCHAR(64) PRIMARY KEY,
    client_order_id     VARCHAR(64) NOT NULL DEFAULT '',
    exchange_order_id   VARCHAR(128) NOT NULL DEFAULT '',
    strategy_id         VARCHAR(64) NOT NULL DEFAULT '',
    account_id          VARCHAR(64) NOT NULL DEFAULT '',
    exchange            VARCHAR(64) NOT NULL,
    symbol              VARCHAR(32) NOT NULL,
    side                VARCHAR(8) NOT NULL CHECK (side IN ('BUY', 'SELL')),
    order_type          VARCHAR(16) NOT NULL CHECK (order_type IN ('MARKET', 'LIMIT', 'STOP', 'STOP_LIMIT')),
    quantity            NUMERIC(20, 8) NOT NULL CHECK (quantity > 0),
    price               NUMERIC(20, 8) NOT NULL DEFAULT 0,
    stop_price          NUMERIC(20, 8) NOT NULL DEFAULT 0,
    filled_quantity     NUMERIC(20, 8) NOT NULL DEFAULT 0 CHECK (filled_quantity >= 0),
    remaining_quantity  NUMERIC(20, 8) NOT NULL DEFAULT 0 CHECK (remaining_quantity >= 0),
    average_fill_price  NUMERIC(20, 8) NOT NULL DEFAULT 0,
    status              VARCHAR(32) NOT NULL DEFAULT 'NEW'
                        CHECK (status IN (
                            'NEW', 'VALIDATING', 'RISK_APPROVED', 'RISK_REJECTED',
                            'ROUTING', 'SENT', 'ACKNOWLEDGED', 'PARTIAL_FILL',
                            'FILLED', 'REJECTED', 'CANCELLED', 'MODIFIED'
                        )),
    reject_reason       TEXT NOT NULL DEFAULT '',
    tif                 VARCHAR(8) NOT NULL DEFAULT 'DAY'
                        CHECK (tif IN ('DAY', 'GTC', 'IOC', 'FOK', 'GTD')),
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_orders_account ON orders(account_id);
CREATE INDEX IF NOT EXISTS idx_orders_exchange ON orders(exchange);
CREATE INDEX IF NOT EXISTS idx_orders_symbol ON orders(symbol);
CREATE INDEX IF NOT EXISTS idx_orders_status ON orders(status);
CREATE INDEX IF NOT EXISTS idx_orders_created ON orders(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_orders_client ON orders(client_order_id);
CREATE INDEX IF NOT EXISTS idx_orders_exchange_oid ON orders(exchange_order_id);

-- ============================================================================
-- EXECUTIONS
-- ============================================================================
CREATE TABLE IF NOT EXISTS executions (
    id              BIGSERIAL PRIMARY KEY,
    order_id        VARCHAR(64) NOT NULL REFERENCES orders(id) ON DELETE CASCADE,
    execution_id    VARCHAR(128) NOT NULL,
    exchange        VARCHAR(64) NOT NULL,
    price           NUMERIC(20, 8) NOT NULL,
    quantity        NUMERIC(20, 8) NOT NULL CHECK (quantity > 0),
    fee             NUMERIC(20, 8) NOT NULL DEFAULT 0,
    liquidity       VARCHAR(4) NOT NULL DEFAULT ''
                    CHECK (liquidity IN ('', 'MAKER', 'TAKER')),
    timestamp       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_exec_order ON executions(order_id);
CREATE INDEX IF NOT EXISTS idx_exec_exchange ON executions(exchange);
CREATE INDEX IF NOT EXISTS idx_exec_time ON executions(timestamp DESC);
CREATE UNIQUE INDEX IF NOT EXISTS idx_exec_unique ON executions(execution_id);

-- ============================================================================
-- POSITIONS
-- ============================================================================
CREATE TABLE IF NOT EXISTS positions (
    id              BIGSERIAL PRIMARY KEY,
    account         VARCHAR(64) NOT NULL DEFAULT '',
    exchange        VARCHAR(64) NOT NULL DEFAULT '',
    symbol          VARCHAR(32) NOT NULL,
    quantity        NUMERIC(20, 8) NOT NULL DEFAULT 0,
    average_price   NUMERIC(20, 8) NOT NULL DEFAULT 0,
    unrealized_pnl  NUMERIC(20, 8) NOT NULL DEFAULT 0,
    realized_pnl    NUMERIC(20, 8) NOT NULL DEFAULT 0,
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_pos_symbol ON positions(account, exchange, symbol);
CREATE INDEX IF NOT EXISTS idx_pos_account ON positions(account);

-- ============================================================================
-- PORTFOLIO
-- ============================================================================
CREATE TABLE IF NOT EXISTS portfolio (
    account             VARCHAR(64) PRIMARY KEY,
    balance             NUMERIC(20, 8) NOT NULL DEFAULT 0,
    equity              NUMERIC(20, 8) NOT NULL DEFAULT 0,
    margin_used         NUMERIC(20, 8) NOT NULL DEFAULT 0,
    margin_available    NUMERIC(20, 8) NOT NULL DEFAULT 0,
    buying_power        NUMERIC(20, 8) NOT NULL DEFAULT 0,
    leverage            NUMERIC(8, 2) NOT NULL DEFAULT 1.00,
    pnl                 NUMERIC(20, 8) NOT NULL DEFAULT 0,
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ============================================================================
-- RISK EVENTS
-- ============================================================================
CREATE TABLE IF NOT EXISTS risk_events (
    id              BIGSERIAL PRIMARY KEY,
    order_id        VARCHAR(64) NOT NULL DEFAULT '',
    event           VARCHAR(64) NOT NULL,
    reason          TEXT NOT NULL DEFAULT '',
    severity        VARCHAR(16) NOT NULL DEFAULT 'WARNING'
                    CHECK (severity IN ('INFO', 'WARNING', 'ERROR', 'CRITICAL')),
    timestamp       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_risk_order ON risk_events(order_id);
CREATE INDEX IF NOT EXISTS idx_risk_event ON risk_events(event);
CREATE INDEX IF NOT EXISTS idx_risk_severity ON risk_events(severity);
CREATE INDEX IF NOT EXISTS idx_risk_time ON risk_events(timestamp DESC);

-- ============================================================================
-- EXCHANGE CONNECTIONS
-- ============================================================================
CREATE TABLE IF NOT EXISTS exchange_connections (
    exchange        VARCHAR(64) PRIMARY KEY,
    connected       BOOLEAN NOT NULL DEFAULT false,
    latency_ms      NUMERIC(10, 2) NOT NULL DEFAULT 0,
    reconnects      INTEGER NOT NULL DEFAULT 0,
    last_heartbeat  TIMESTAMPTZ,
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ============================================================================
-- AUDIT LOGS
-- ============================================================================
CREATE TABLE IF NOT EXISTS audit_logs (
    id              BIGSERIAL PRIMARY KEY,
    action          VARCHAR(128) NOT NULL,
    details         TEXT NOT NULL DEFAULT '',
    timestamp       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_audit_action ON audit_logs(action);
CREATE INDEX IF NOT EXISTS idx_audit_time ON audit_logs(timestamp DESC);

-- ============================================================================
-- SESSIONS
-- ============================================================================
CREATE TABLE IF NOT EXISTS sessions (
    session_id      VARCHAR(128) PRIMARY KEY,
    engine_id       VARCHAR(64) NOT NULL DEFAULT '',
    authenticated   BOOLEAN NOT NULL DEFAULT false,
    connected_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    disconnected_at TIMESTAMPTZ,
    ip              VARCHAR(45) NOT NULL DEFAULT '',
    websocket       BOOLEAN NOT NULL DEFAULT false
);

CREATE INDEX IF NOT EXISTS idx_sessions_engine ON sessions(engine_id);
CREATE INDEX IF NOT EXISTS idx_sessions_auth ON sessions(authenticated);

-- ============================================================================
-- METRICS
-- ============================================================================
CREATE TABLE IF NOT EXISTS metrics (
    id                      BIGSERIAL PRIMARY KEY,
    cpu                     NUMERIC(8, 2) NOT NULL DEFAULT 0,
    memory                  NUMERIC(12, 2) NOT NULL DEFAULT 0,
    latency_us              INTEGER NOT NULL DEFAULT 0,
    active_orders           INTEGER NOT NULL DEFAULT 0,
    websocket_clients       INTEGER NOT NULL DEFAULT 0,
    requests_per_second     NUMERIC(10, 2) NOT NULL DEFAULT 0,
    timestamp               TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_metrics_time ON metrics(timestamp DESC);

-- ============================================================================
-- GRANTS
-- ============================================================================
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO oms_user;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO oms_user;

COMMIT;
