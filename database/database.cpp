#include "database.h"

#include <sstream>
#include <iomanip>
#include <tuple>
#include <fstream>
#include <set>

namespace oms {

Database::Database(const DbConfig& config)
    : config_(config)
{
    connections_.reserve(config.pool_size);
    available_.reserve(config.pool_size);
}

Database::~Database() {
    disconnect();
}

bool Database::connect() {
    std::string conn_str = "host=" + config_.host
        + " port=" + std::to_string(config_.port)
        + " dbname=" + config_.dbname
        + " user=" + config_.user
        + " password=" + config_.password;

    for (unsigned int i = 0; i < config_.pool_size; ++i) {
        try {
            auto conn = std::make_unique<pqxx::connection>(conn_str);
            if (!conn->is_open()) {
                Logger::error("Database: failed to open connection " + std::to_string(i));
                continue;
            }
            initPreparedStatements(*conn);
            {
                std::lock_guard<std::mutex> lock(pool_mutex_);
                available_.push_back(conn.get());
                connections_.push_back(std::move(conn));
            }
        } catch (const std::exception& e) {
            Logger::error("Database: connection error: " + std::string(e.what()));
            return false;
        }
    }

    connected_ = !connections_.empty();
    if (connected_) {
        Logger::info("Database: connected with " + std::to_string(connections_.size()) + " connections");
        runMigrations();
    }
    return connected_;
}

void Database::disconnect() {
    connected_ = false;
    std::lock_guard<std::mutex> lock(pool_mutex_);
    available_.clear();
    connections_.clear();
    Logger::info("Database: disconnected");
}

bool Database::health() {
    if (!connected_) return false;
    try {
        auto* conn = getConnection();
        if (!conn) return false;
        {
            pqxx::nontransaction tx(*conn);
            tx.exec("SELECT 1");
        }
        returnConnection(conn);
        return true;
    } catch (...) {
        return false;
    }
}

bool Database::runMigrations() {
    try {
        auto* conn = getConnection();
        if (!conn) return false;

        std::vector<std::string> expected = {
            "accounts", "orders", "executions", "positions", "portfolio",
            "risk_events", "exchange_connections", "audit_logs", "sessions", "metrics"
        };

        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec(
                "SELECT table_name FROM information_schema.tables "
                "WHERE table_schema = 'public'");
            std::set<std::string> existing;
            for (const auto& row : r) {
                existing.insert(row[0].as<std::string>(""));
            }
            bool all_exist = true;
            for (const auto& t : expected) {
                if (existing.find(t) == existing.end()) {
                    all_exist = false;
                    break;
                }
            }
            if (all_exist) {
                returnConnection(conn);
                Logger::info("Database: schema detected, " + std::to_string(expected.size()) + " tables present");
                return true;
            }
        }

        Logger::info("Database: schema missing, executing database/schema.sql...");

        std::ifstream file("database/schema.sql");
        if (!file) {
            Logger::error("Database: cannot open database/schema.sql for auto-migration");
            returnConnection(conn);
            return false;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string schema_sql = buffer.str();

        {
            pqxx::work tx(*conn);
            tx.exec(schema_sql);
            tx.commit();
        }

        returnConnection(conn);
        Logger::info("Database: schema initialized successfully");
        return true;

    } catch (const std::exception& e) {
        Logger::error("Database: migration failed: " + std::string(e.what()));
        returnConnection(nullptr);
        return false;
    }
}

void Database::initPreparedStatements(pqxx::connection& conn) {
    conn.prepare("insert_order",
        "INSERT INTO orders (id, client_order_id, exchange_order_id, strategy_id, "
        "account_id, exchange, symbol, side, order_type, quantity, price, stop_price, "
        "filled_quantity, remaining_quantity, average_fill_price, status, reject_reason, "
        "tif, created_at, updated_at) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, "
        "$16, $17, $18, $19, $20)");

    conn.prepare("update_order",
        "UPDATE orders SET exchange_order_id=$2, filled_quantity=$3, "
        "remaining_quantity=$4, average_fill_price=$5, status=$6, "
        "reject_reason=$7, updated_at=NOW() WHERE id=$1");

    conn.prepare("get_order",
        "SELECT id, client_order_id, exchange_order_id, strategy_id, "
        "account_id, exchange, symbol, side, order_type, quantity, price, stop_price, "
        "filled_quantity, remaining_quantity, average_fill_price, status, reject_reason, "
        "tif, created_at, updated_at FROM orders WHERE id=$1");

    conn.prepare("get_orders_by_status",
        "SELECT id, client_order_id, exchange_order_id, strategy_id, "
        "account_id, exchange, symbol, side, order_type, quantity, price, stop_price, "
        "filled_quantity, remaining_quantity, average_fill_price, status, reject_reason, "
        "tif, created_at, updated_at FROM orders WHERE status=$1 ORDER BY created_at DESC");

    conn.prepare("get_all_orders",
        "SELECT id, client_order_id, exchange_order_id, strategy_id, "
        "account_id, exchange, symbol, side, order_type, quantity, price, stop_price, "
        "filled_quantity, remaining_quantity, average_fill_price, status, reject_reason, "
        "tif, created_at, updated_at FROM orders ORDER BY created_at DESC");

    conn.prepare("order_exists",
        "SELECT COUNT(*) FROM orders WHERE id=$1");

    conn.prepare("get_orders_by_client",
        "SELECT id, client_order_id, exchange_order_id, strategy_id, "
        "account_id, exchange, symbol, side, order_type, quantity, price, stop_price, "
        "filled_quantity, remaining_quantity, average_fill_price, status, reject_reason, "
        "tif, created_at, updated_at FROM orders WHERE account_id=$1 ORDER BY created_at DESC");

    conn.prepare("insert_execution",
        "INSERT INTO executions (order_id, execution_id, exchange, price, quantity, fee, liquidity, timestamp) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8)");

    conn.prepare("get_executions",
        "SELECT id, order_id, execution_id, exchange, price, quantity, fee, liquidity, timestamp "
        "FROM executions WHERE order_id=$1 ORDER BY timestamp");

    conn.prepare("upsert_position",
        "INSERT INTO positions (account, exchange, symbol, quantity, average_price, "
        "unrealized_pnl, realized_pnl, updated_at) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, NOW()) "
        "ON CONFLICT (account, exchange, symbol) DO UPDATE SET "
        "quantity=$4, average_price=$5, unrealized_pnl=$6, realized_pnl=$7, updated_at=NOW()");

    conn.prepare("get_positions",
        "SELECT account, exchange, symbol, quantity, average_price, "
        "unrealized_pnl, realized_pnl, updated_at FROM positions ORDER BY symbol");

    conn.prepare("get_position",
        "SELECT account, exchange, symbol, quantity, average_price, "
        "unrealized_pnl, realized_pnl, updated_at FROM positions WHERE symbol=$1 LIMIT 1");

    conn.prepare("upsert_portfolio",
        "INSERT INTO portfolio (account, balance, equity, margin_used, margin_available, "
        "buying_power, leverage, pnl, updated_at) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, NOW()) "
        "ON CONFLICT (account) DO UPDATE SET "
        "balance=$2, equity=$3, margin_used=$4, margin_available=$5, "
        "buying_power=$6, leverage=$7, pnl=$8, updated_at=NOW()");

    conn.prepare("get_portfolio",
        "SELECT account, balance, equity, margin_used, margin_available, "
        "buying_power, leverage, pnl, updated_at FROM portfolio WHERE account=$1");

    conn.prepare("insert_risk_event",
        "INSERT INTO risk_events (order_id, event, reason, severity, timestamp) "
        "VALUES ($1, $2, $3, $4, NOW())");

    conn.prepare("get_risk_events",
        "SELECT order_id, event, reason, severity FROM risk_events ORDER BY timestamp DESC");

    conn.prepare("insert_audit",
        "INSERT INTO audit_logs (action, details, timestamp) VALUES ($1, $2, NOW())");

    conn.prepare("get_audit_logs",
        "SELECT action, details, timestamp FROM audit_logs ORDER BY timestamp DESC LIMIT 1000");

    conn.prepare("upsert_exchange_conn",
        "INSERT INTO exchange_connections (exchange, connected, latency_ms, reconnects, updated_at) "
        "VALUES ($1, $2, $3, $4, NOW()) "
        "ON CONFLICT (exchange) DO UPDATE SET "
        "connected=$2, latency_ms=$3, reconnects=$4, updated_at=NOW()");

    conn.prepare("get_exchange_conns",
        "SELECT exchange, connected FROM exchange_connections");

    conn.prepare("insert_session",
        "INSERT INTO sessions (session_id, engine_id, authenticated, connected_at, ip, websocket) "
        "VALUES ($1, $2, TRUE, NOW(), $3, $4)");

    conn.prepare("disconnect_session",
        "UPDATE sessions SET disconnected_at=NOW(), authenticated=FALSE WHERE session_id=$1");

    conn.prepare("insert_metrics",
        "INSERT INTO metrics (cpu, memory, latency_us, active_orders, "
        "websocket_clients, requests_per_second, timestamp) "
        "VALUES ($1, $2, $3, $4, $5, $6, NOW())");

    conn.prepare("count_orders",
        "SELECT COUNT(*) FROM orders");
}

pqxx::connection* Database::getConnection() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    if (available_.empty()) {
        Logger::warn("Database: no available connections, creating temporary one");
        std::string conn_str = "host=" + config_.host
            + " port=" + std::to_string(config_.port)
            + " dbname=" + config_.dbname
            + " user=" + config_.user
            + " password=" + config_.password;
        try {
            auto conn = std::make_unique<pqxx::connection>(conn_str);
            if (conn->is_open()) {
                initPreparedStatements(*conn);
                auto* ptr = conn.get();
                connections_.push_back(std::move(conn));
                return ptr;
            }
        } catch (const std::exception& e) {
            Logger::error("Database: temp connection failed: " + std::string(e.what()));
        }
        return nullptr;
    }
    auto* conn = available_.back();
    available_.pop_back();
    return conn;
}

void Database::returnConnection(pqxx::connection* conn) {
    if (!conn) return;
    std::lock_guard<std::mutex> lock(pool_mutex_);
    available_.push_back(conn);
}

std::string Database::toDbTime(const std::chrono::system_clock::time_point& tp) const {
    auto t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&t), "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count() << "+00";
    return ss.str();
}

std::chrono::system_clock::time_point Database::fromDbTime(const std::string& ts) const {
    std::tm tm = {};
    std::stringstream ss(ts);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    auto tp = std::chrono::system_clock::from_time_t(timegm(&tm));
    auto dot = ts.find('.');
    if (dot != std::string::npos) {
        try {
            int ms = std::stoi(ts.substr(dot + 1, 3));
            tp += std::chrono::milliseconds(ms);
        } catch (...) {}
    }
    return tp;
}

Order Database::rowToOrder(const pqxx::row& row) const {
    Order o;
    o.id = row["id"].as<std::string>("");
    o.client_order_id = row["client_order_id"].as<std::string>("");
    o.exchange_order_id = row["exchange_order_id"].as<std::string>("");
    o.strategy_id = row["strategy_id"].as<std::string>("");
    o.account_id = row["account_id"].as<std::string>("");
    o.client_id = o.account_id;
    o.exchange = row["exchange"].as<std::string>("");
    o.symbol = row["symbol"].as<std::string>("");
    o.side = row["side"].as<std::string>("") == "SELL" ? OrderSide::SELL : OrderSide::BUY;

    std::string type = row["order_type"].as<std::string>("");
    if (type == "MARKET") o.type = OrderType::MARKET;
    else if (type == "STOP") o.type = OrderType::STOP;
    else if (type == "STOP_LIMIT") o.type = OrderType::STOP_LIMIT;
    else o.type = OrderType::LIMIT;

    o.quantity = row["quantity"].as<double>(0);
    o.price = row["price"].as<double>(0);
    o.stop_price = row["stop_price"].as<double>(0);
    o.filled_quantity = row["filled_quantity"].as<double>(0);
    o.remaining_quantity = row["remaining_quantity"].as<double>(0);
    o.avg_fill_price = row["average_fill_price"].as<double>(0);
    o.tif = row["tif"].as<std::string>("DAY");
    o.reject_reason = row["reject_reason"].as<std::string>("");

    std::string status = row["status"].as<std::string>("");
    if (status == "NEW") o.state = OrderState::NEW;
    else if (status == "VALIDATING") o.state = OrderState::VALIDATING;
    else if (status == "RISK_APPROVED") o.state = OrderState::RISK_APPROVED;
    else if (status == "RISK_REJECTED") o.state = OrderState::RISK_REJECTED;
    else if (status == "ROUTING") o.state = OrderState::ROUTING;
    else if (status == "SENT") o.state = OrderState::SENT;
    else if (status == "ACKNOWLEDGED") o.state = OrderState::ACKNOWLEDGED;
    else if (status == "PARTIAL_FILL") o.state = OrderState::PARTIAL_FILL;
    else if (status == "FILLED") o.state = OrderState::FILLED;
    else if (status == "REJECTED") o.state = OrderState::REJECTED;
    else if (status == "CANCELLED") o.state = OrderState::CANCELLED;
    else if (status == "MODIFIED") o.state = OrderState::MODIFIED;

    try {
        o.created_at = fromDbTime(row["created_at"].as<std::string>(""));
        o.updated_at = fromDbTime(row["updated_at"].as<std::string>(""));
    } catch (...) {
        o.created_at = std::chrono::system_clock::now();
        o.updated_at = o.created_at;
    }

    return o;
}

ExecutionReport Database::rowToExecution(const pqxx::row& row) const {
    ExecutionReport e;
    e.order_id = row["order_id"].as<std::string>("");
    e.exec_id = row["execution_id"].as<std::string>("");
    e.symbol = "";
    e.price = row["price"].as<double>(0);
    e.quantity = row["quantity"].as<double>(0);
    e.avg_price = e.price;
    e.cum_qty = e.quantity;
    try {
        e.timestamp = fromDbTime(row["timestamp"].as<std::string>(""));
    } catch (...) {
        e.timestamp = std::chrono::system_clock::now();
    }
    return e;
}

Position Database::rowToPosition(const pqxx::row& row) const {
    Position p;
    p.symbol = row["symbol"].as<std::string>("");
    p.quantity = row["quantity"].as<double>(0);
    p.avg_price = row["average_price"].as<double>(0);
    p.realized_pnl = row["realized_pnl"].as<double>(0);
    p.unrealized_pnl = row["unrealized_pnl"].as<double>(0);
    return p;
}

// ===========================================================================
// ORDER METHODS
// ===========================================================================

void Database::storeOrder(const Order& order) {
    try {
        auto* conn = getConnection();
        if (!conn) {
            Logger::error("Database storeOrder: no connection available");
            return;
        }
        {
            pqxx::work tx(*conn);
            tx.exec_prepared("insert_order",
                order.id,
                order.client_order_id.empty() ? order.id : order.client_order_id,
                order.exchange_order_id,
                order.strategy_id,
                order.client_id.empty() ? "default" : order.client_id,
                order.exchange,
                order.symbol,
                sideToString(order.side),
                typeToString(order.type),
                order.quantity,
                order.price,
                order.stop_price,
                order.filled_quantity,
                order.remaining_quantity > 0 ? order.remaining_quantity : order.quantity,
                order.avg_fill_price,
                stateToString(order.state),
                order.reject_reason,
                order.tif,
                toDbTime(order.created_at),
                toDbTime(order.updated_at)
            );
            tx.commit();
        }
        returnConnection(conn);
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            order_cache_[order.id] = order;
        }
    } catch (const std::exception& e) {
        Logger::error("Database storeOrder failed: " + std::string(e.what()));
        storeAudit({"Database Error", "storeOrder: " + std::string(e.what())});
    }
}

void Database::updateOrder(const Order& order) {
    try {
        auto* conn = getConnection();
        if (!conn) {
            Logger::error("Database updateOrder: no connection available");
            return;
        }
        {
            pqxx::work tx(*conn);
            tx.exec_prepared("update_order",
                order.id,
                order.exchange_order_id,
                order.filled_quantity,
                order.remaining_quantity,
                order.avg_fill_price,
                stateToString(order.state),
                order.reject_reason
            );
            tx.commit();
        }
        returnConnection(conn);
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            order_cache_[order.id] = order;
            if (order.state == OrderState::FILLED ||
                order.state == OrderState::REJECTED ||
                order.state == OrderState::CANCELLED) {
                active_orders_.erase(order.id);
                completed_orders_.insert(order.id);
            }
        }
    } catch (const std::exception& e) {
        Logger::error("Database updateOrder failed: " + std::string(e.what()));
        storeAudit({"Database Error", "updateOrder: " + std::string(e.what())});
    }
}

std::vector<Order> Database::getOrders() const {
    std::vector<Order> result;
    try {
        auto* conn = const_cast<Database*>(this)->getConnection();
        if (!conn) return result;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("get_all_orders");
            for (const auto& row : r) {
                result.push_back(rowToOrder(row));
            }
        }
        const_cast<Database*>(this)->returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database getOrders failed: " + std::string(e.what()));
    }
    return result;
}

std::vector<Order> Database::getOrdersByClient(const ClientId& client_id) const {
    std::vector<Order> result;
    try {
        auto* conn = const_cast<Database*>(this)->getConnection();
        if (!conn) return result;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("get_orders_by_client", client_id);
            for (const auto& row : r) {
                result.push_back(rowToOrder(row));
            }
        }
        const_cast<Database*>(this)->returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database getOrdersByClient failed: " + std::string(e.what()));
    }
    return result;
}

std::vector<Order> Database::getOrdersByStatus(const std::string& status) const {
    std::vector<Order> result;
    try {
        auto* conn = const_cast<Database*>(this)->getConnection();
        if (!conn) return result;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("get_orders_by_status", status);
            for (const auto& row : r) {
                result.push_back(rowToOrder(row));
            }
        }
        const_cast<Database*>(this)->returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database getOrdersByStatus failed: " + std::string(e.what()));
    }
    return result;
}

Order Database::getOrder(const OrderId& id) const {
    try {
        auto* conn = const_cast<Database*>(this)->getConnection();
        if (!conn) return {};
        Order o;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("get_order", id);
            if (!r.empty()) {
                o = rowToOrder(r[0]);
            }
        }
        const_cast<Database*>(this)->returnConnection(conn);
        if (!o.id.empty()) {
            return o;
        }
    } catch (const std::exception& e) {
        Logger::error("Database getOrder failed: " + std::string(e.what()));
    }
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = order_cache_.find(id);
        if (it != order_cache_.end()) return it->second;
    }
    return {};
}

bool Database::orderExists(const OrderId& id) const {
    try {
        auto* conn = const_cast<Database*>(this)->getConnection();
        if (!conn) return false;
        bool exists = false;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("order_exists", id);
            if (!r.empty()) {
                exists = r[0][0].as<int>(0) > 0;
            }
        }
        const_cast<Database*>(this)->returnConnection(conn);
        return exists;
    } catch (const std::exception& e) {
        Logger::error("Database orderExists failed: " + std::string(e.what()));
    }
    return false;
}

// ===========================================================================
// ACTIVE / COMPLETED ORDER TRACKING
// ===========================================================================

void Database::addActiveOrder(const OrderId& id) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    active_orders_.insert(id);
    completed_orders_.erase(id);
}

void Database::removeActiveOrder(const OrderId& id) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    active_orders_.erase(id);
}

std::vector<OrderId> Database::getActiveOrders() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return {active_orders_.begin(), active_orders_.end()};
}

bool Database::isActiveOrder(const OrderId& id) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return active_orders_.count(id) > 0;
}

size_t Database::activeOrderCount() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return active_orders_.size();
}

std::vector<Order> Database::loadOpenOrders() {
    std::vector<Order> result;
    try {
        auto* conn = getConnection();
        if (!conn) return result;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("get_orders_by_status", "SENT");
            auto r2 = tx.exec_prepared("get_orders_by_status", "ACKNOWLEDGED");
            auto r3 = tx.exec_prepared("get_orders_by_status", "PARTIAL_FILL");
            auto r4 = tx.exec_prepared("get_orders_by_status", "ROUTING");
            auto r5 = tx.exec_prepared("get_orders_by_status", "RISK_APPROVED");

            for (const auto& row : r) result.push_back(rowToOrder(row));
            for (const auto& row : r2) result.push_back(rowToOrder(row));
            for (const auto& row : r3) result.push_back(rowToOrder(row));
            for (const auto& row : r4) result.push_back(rowToOrder(row));
            for (const auto& row : r5) result.push_back(rowToOrder(row));
        }
        returnConnection(conn);

        std::lock_guard<std::mutex> lock(cache_mutex_);
        for (const auto& o : result) {
            active_orders_.insert(o.id);
            order_cache_[o.id] = o;
        }
        Logger::info("Database: loaded " + std::to_string(result.size()) + " open orders");
    } catch (const std::exception& e) {
        Logger::error("Database loadOpenOrders failed: " + std::string(e.what()));
    }
    return result;
}

void Database::addCompletedOrder(const Order& order) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    active_orders_.erase(order.id);
    completed_orders_.insert(order.id);
    order_cache_[order.id] = order;
}

std::vector<Order> Database::getCompletedOrders() const {
    std::vector<Order> result;
    try {
        auto* conn = const_cast<Database*>(this)->getConnection();
        if (!conn) return result;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("get_orders_by_status", "FILLED");
            auto r2 = tx.exec_prepared("get_orders_by_status", "REJECTED");
            auto r3 = tx.exec_prepared("get_orders_by_status", "CANCELLED");
            for (const auto& row : r) result.push_back(rowToOrder(row));
            for (const auto& row : r2) result.push_back(rowToOrder(row));
            for (const auto& row : r3) result.push_back(rowToOrder(row));
        }
        const_cast<Database*>(this)->returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database getCompletedOrders failed: " + std::string(e.what()));
    }
    return result;
}

bool Database::isCompletedOrder(const OrderId& id) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return completed_orders_.count(id) > 0;
}

// ===========================================================================
// EXECUTIONS
// ===========================================================================

void Database::storeExecution(const ExecutionReport& exec) {
    try {
        auto* conn = getConnection();
        if (!conn) {
            Logger::error("Database storeExecution: no connection available");
            return;
        }
        {
            pqxx::work tx(*conn);
            tx.exec_prepared("insert_execution",
                exec.order_id,
                exec.exec_id,
                "",
                exec.price,
                exec.quantity,
                0.0,
                "",
                toDbTime(exec.timestamp)
            );
            tx.commit();
        }
        returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database storeExecution failed: " + std::string(e.what()));
        storeAudit({"Database Error", "storeExecution: " + std::string(e.what())});
    }
}

std::vector<ExecutionReport> Database::getExecutions(const OrderId& order_id) const {
    std::vector<ExecutionReport> result;
    try {
        auto* conn = const_cast<Database*>(this)->getConnection();
        if (!conn) return result;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("get_executions", order_id);
            for (const auto& row : r) {
                result.push_back(rowToExecution(row));
            }
        }
        const_cast<Database*>(this)->returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database getExecutions failed: " + std::string(e.what()));
    }
    return result;
}

// ===========================================================================
// POSITIONS
// ===========================================================================

void Database::updatePosition(const Position& pos) {
    try {
        auto* conn = getConnection();
        if (!conn) {
            Logger::error("Database updatePosition: no connection available");
            return;
        }
        {
            pqxx::work tx(*conn);
            tx.exec_prepared("upsert_position",
                "default", "", pos.symbol,
                pos.quantity, pos.avg_price,
                pos.unrealized_pnl, pos.realized_pnl
            );
            tx.commit();
        }
        returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database updatePosition failed: " + std::string(e.what()));
        storeAudit({"Database Error", "updatePosition: " + std::string(e.what())});
    }
}

Position Database::getPosition(const std::string& symbol) const {
    try {
        auto* conn = const_cast<Database*>(this)->getConnection();
        if (!conn) return {};
        Position p;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("get_position", symbol);
            if (!r.empty()) {
                p = rowToPosition(r[0]);
            }
        }
        const_cast<Database*>(this)->returnConnection(conn);
        if (!p.symbol.empty()) return p;
    } catch (const std::exception& e) {
        Logger::error("Database getPosition failed: " + std::string(e.what()));
    }
    return {};
}

std::vector<Position> Database::getPositions() const {
    std::vector<Position> result;
    try {
        auto* conn = const_cast<Database*>(this)->getConnection();
        if (!conn) return result;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("get_positions");
            for (const auto& row : r) {
                result.push_back(rowToPosition(row));
            }
        }
        const_cast<Database*>(this)->returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database getPositions failed: " + std::string(e.what()));
    }
    return result;
}

std::vector<Position> Database::loadPositions() {
    auto result = getPositions();
    Logger::info("Database: loaded " + std::to_string(result.size()) + " positions");
    return result;
}

// ===========================================================================
// PORTFOLIO
// ===========================================================================

void Database::setPortfolio(const Portfolio& portfolio) {
    try {
        auto* conn = getConnection();
        if (!conn) {
            Logger::error("Database setPortfolio: no connection available");
            return;
        }
        {
            pqxx::work tx(*conn);
            tx.exec_prepared("upsert_portfolio",
                portfolio.client_id.empty() ? "default" : portfolio.client_id,
                portfolio.total_equity,
                portfolio.total_equity,
                portfolio.used_margin,
                portfolio.free_margin,
                portfolio.buying_power,
                portfolio.leverage,
                portfolio.realized_pnl
            );
            tx.commit();
        }
        returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database setPortfolio failed: " + std::string(e.what()));
        storeAudit({"Database Error", "setPortfolio: " + std::string(e.what())});
    }
}

Portfolio Database::getPortfolio(const ClientId& client_id) const {
    try {
        auto* conn = const_cast<Database*>(this)->getConnection();
        if (!conn) {
            Portfolio p;
            p.client_id = client_id;
            p.total_equity = 100000.0;
            p.buying_power = 100000.0;
            p.leverage = 1.0;
            p.free_margin = 100000.0;
            return p;
        }
        Portfolio p;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("get_portfolio", client_id.empty() ? "default" : client_id);
            if (!r.empty()) {
                const auto& row = r[0];
                p.client_id = row["account"].as<std::string>("");
                p.total_equity = row["equity"].as<double>(0);
                p.used_margin = row["margin_used"].as<double>(0);
                p.free_margin = row["margin_available"].as<double>(0);
                p.buying_power = row["buying_power"].as<double>(0);
                p.leverage = row["leverage"].as<double>(1.0);
                p.realized_pnl = row["pnl"].as<double>(0);
            }
        }
        const_cast<Database*>(this)->returnConnection(conn);
        if (!p.client_id.empty()) {
            p.positions = getPositions();
            return p;
        }
    } catch (const std::exception& e) {
        Logger::error("Database getPortfolio failed: " + std::string(e.what()));
    }
    Portfolio p;
    p.client_id = client_id;
    p.total_equity = 100000.0;
    p.buying_power = 100000.0;
    p.leverage = 1.0;
    p.free_margin = 100000.0;
    return p;
}

Portfolio Database::loadPortfolio(const ClientId& client_id) {
    return getPortfolio(client_id);
}

// ===========================================================================
// RISK EVENTS
// ===========================================================================

void Database::storeRiskEvent(const std::string& order_id, const std::string& event,
                              const std::string& reason, const std::string& severity) {
    try {
        auto* conn = getConnection();
        if (!conn) {
            Logger::error("Database storeRiskEvent: no connection available");
            return;
        }
        {
            pqxx::work tx(*conn);
            tx.exec_prepared("insert_risk_event", order_id, event, reason, severity);
            tx.commit();
        }
        returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database storeRiskEvent failed: " + std::string(e.what()));
    }
}

std::vector<std::tuple<std::string, std::string, std::string, std::string>> Database::getRiskEvents() const {
    std::vector<std::tuple<std::string, std::string, std::string, std::string>> result;
    try {
        auto* conn = const_cast<Database*>(this)->getConnection();
        if (!conn) return result;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("get_risk_events");
            for (const auto& row : r) {
                result.emplace_back(
                    row["order_id"].as<std::string>(""),
                    row["event"].as<std::string>(""),
                    row["reason"].as<std::string>(""),
                    row["severity"].as<std::string>("")
                );
            }
        }
        const_cast<Database*>(this)->returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database getRiskEvents failed: " + std::string(e.what()));
    }
    return result;
}

// ===========================================================================
// AUDIT
// ===========================================================================

void Database::storeAudit(const AuditEntry& entry) {
    try {
        auto* conn = getConnection();
        if (!conn) return;
        {
            pqxx::work tx(*conn);
            tx.exec_prepared("insert_audit", entry.action, entry.details);
            tx.commit();
        }
        returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database storeAudit failed: " + std::string(e.what()));
    }
}

std::vector<AuditEntry> Database::getAuditLog() const {
    std::vector<AuditEntry> result;
    try {
        auto* conn = const_cast<Database*>(this)->getConnection();
        if (!conn) return result;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("get_audit_logs");
            for (const auto& row : r) {
                AuditEntry e;
                e.action = row["action"].as<std::string>("");
                e.details = row["details"].as<std::string>("");
                try {
                    e.timestamp = fromDbTime(row["timestamp"].as<std::string>(""));
                } catch (...) {
                    e.timestamp = std::chrono::system_clock::now();
                }
                result.push_back(e);
            }
        }
        const_cast<Database*>(this)->returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database getAuditLog failed: " + std::string(e.what()));
    }
    return result;
}

// ===========================================================================
// SYSTEM EVENTS (in-memory)
// ===========================================================================

void Database::storeSystemEvent(const std::string& event) {
    Logger::info("System: " + event);
    std::lock_guard<std::mutex> lock(cache_mutex_);
    system_events_.push_back(event);
    storeAudit({"System Event", event});
}

std::vector<std::string> Database::getSystemEvents() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return system_events_;
}

// ===========================================================================
// EXCHANGE CONNECTIONS
// ===========================================================================

void Database::updateExchangeConnection(const std::string& exchange, bool connected,
                                        double latency_ms, int reconnects) {
    try {
        auto* conn = getConnection();
        if (!conn) return;
        {
            pqxx::work tx(*conn);
            tx.exec_prepared("upsert_exchange_conn", exchange, connected, latency_ms, reconnects);
            tx.commit();
        }
        returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database updateExchangeConnection failed: " + std::string(e.what()));
    }
}

std::unordered_map<std::string, bool> Database::getExchangeConnections() const {
    std::unordered_map<std::string, bool> result;
    try {
        auto* conn = const_cast<Database*>(this)->getConnection();
        if (!conn) return result;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("get_exchange_conns");
            for (const auto& row : r) {
                result[row["exchange"].as<std::string>("")] = row["connected"].as<bool>(false);
            }
        }
        const_cast<Database*>(this)->returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database getExchangeConnections failed: " + std::string(e.what()));
    }
    return result;
}

// ===========================================================================
// SESSIONS
// ===========================================================================

void Database::storeSession(const std::string& session_id, const std::string& engine_id,
                            const std::string& ip, bool websocket) {
    try {
        auto* conn = getConnection();
        if (!conn) return;
        {
            pqxx::work tx(*conn);
            tx.exec_prepared("insert_session", session_id, engine_id, ip, websocket);
            tx.commit();
        }
        returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database storeSession failed: " + std::string(e.what()));
    }
}

void Database::updateSessionDisconnect(const std::string& session_id) {
    try {
        auto* conn = getConnection();
        if (!conn) return;
        {
            pqxx::work tx(*conn);
            tx.exec_prepared("disconnect_session", session_id);
            tx.commit();
        }
        returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database updateSessionDisconnect failed: " + std::string(e.what()));
    }
}

// ===========================================================================
// METRICS
// ===========================================================================

void Database::storeMetrics(double cpu, double memory, int latency_us,
                            int active_orders, int ws_clients, double rps) {
    try {
        auto* conn = getConnection();
        if (!conn) return;
        {
            pqxx::nontransaction tx(*conn);
            tx.exec_prepared("insert_metrics", cpu, memory, latency_us,
                             active_orders, ws_clients, rps);
        }
        returnConnection(conn);
    } catch (const std::exception& e) {
        Logger::error("Database storeMetrics failed: " + std::string(e.what()));
    }
}

// ===========================================================================
// TRANSACTIONS (passthrough - use individual methods for real transactions)
// ===========================================================================

void Database::beginTransaction() {
    storeSystemEvent("Transaction started");
}

void Database::commit() {
    storeSystemEvent("Transaction committed");
}

void Database::rollback() {
    storeSystemEvent("Transaction rolled back");
}

// ===========================================================================
// COUNTS
// ===========================================================================

size_t Database::totalOrderCount() {
    try {
        auto* conn = getConnection();
        if (!conn) return 0;
        size_t count = 0;
        {
            pqxx::nontransaction tx(*conn);
            auto r = tx.exec_prepared("count_orders");
            if (!r.empty()) {
                count = r[0][0].as<size_t>(0);
            }
        }
        returnConnection(conn);
        return count;
    } catch (const std::exception& e) {
        Logger::error("Database totalOrderCount failed: " + std::string(e.what()));
    }
    return 0;
}

// backward compatibility wrapper
void Database::storeRiskEvent(const std::string& order_id, const std::string& reason) {
    storeRiskEvent(order_id, "RISK_REJECTED", reason, "WARNING");
}

}
