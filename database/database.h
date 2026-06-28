#pragma once

#include "../include/models.h"
#include "../include/logger.h"

#include <pqxx/pqxx>

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <string>
#include <atomic>
#include <chrono>

namespace oms {

struct DbConfig {
    std::string host = "127.0.0.1";
    unsigned short port = 5432;
    std::string dbname = "oms";
    std::string user = "oms_user";
    std::string password = "oms_pass";
    unsigned int pool_size = 4;
    unsigned int max_retries = 3;
};

class Database {
public:
    explicit Database(const DbConfig& config = DbConfig{});
    ~Database();

    // lifecycle
    bool connect();
    void disconnect();
    bool health();
    bool runMigrations();

    // orders
    void storeOrder(const Order& order);
    void updateOrder(const Order& order);
    std::vector<Order> getOrders() const;
    std::vector<Order> getOrdersByClient(const ClientId& client_id) const;
    std::vector<Order> getOrdersByStatus(const std::string& status) const;
    Order getOrder(const OrderId& id) const;
    bool orderExists(const OrderId& id) const;

    // active order tracking (in-memory cache)
    void addActiveOrder(const OrderId& id);
    void removeActiveOrder(const OrderId& id);
    std::vector<OrderId> getActiveOrders() const;
    bool isActiveOrder(const OrderId& id) const;
    size_t activeOrderCount() const;
    std::vector<Order> loadOpenOrders();

    // completed order tracking
    void addCompletedOrder(const Order& order);
    std::vector<Order> getCompletedOrders() const;
    bool isCompletedOrder(const OrderId& id) const;

    // executions
    void storeExecution(const ExecutionReport& exec);
    std::vector<ExecutionReport> getExecutions(const OrderId& order_id) const;

    // positions
    void updatePosition(const Position& pos);
    Position getPosition(const std::string& symbol) const;
    std::vector<Position> getPositions() const;
    std::vector<Position> loadPositions();

    // portfolio
    void setPortfolio(const Portfolio& portfolio);
    Portfolio getPortfolio(const ClientId& client_id) const;
    Portfolio loadPortfolio(const ClientId& client_id);

    // risk events
    void storeRiskEvent(const std::string& order_id, const std::string& event,
                        const std::string& reason, const std::string& severity = "WARNING");
    void storeRiskEvent(const std::string& order_id, const std::string& reason);
    std::vector<std::tuple<std::string, std::string, std::string, std::string>> getRiskEvents() const;

    // audit
    void storeAudit(const AuditEntry& entry);
    std::vector<AuditEntry> getAuditLog() const;

    // system events
    void storeSystemEvent(const std::string& event);
    std::vector<std::string> getSystemEvents() const;

    // exchange connections
    void updateExchangeConnection(const std::string& exchange, bool connected,
                                  double latency_ms = 0, int reconnects = 0);
    std::unordered_map<std::string, bool> getExchangeConnections() const;

    // sessions
    void storeSession(const std::string& session_id, const std::string& engine_id,
                      const std::string& ip, bool websocket);
    void updateSessionDisconnect(const std::string& session_id);

    // metrics
    void storeMetrics(double cpu, double memory, int latency_us,
                      int active_orders, int ws_clients, double rps);

    // transactions
    void beginTransaction();
    void commit();
    void rollback();

    // count queries
    size_t totalOrderCount();

private:
    pqxx::connection* getConnection();
    void returnConnection(pqxx::connection* conn);
    void initPreparedStatements(pqxx::connection& conn);
    Order rowToOrder(const pqxx::row& row) const;
    ExecutionReport rowToExecution(const pqxx::row& row) const;
    Position rowToPosition(const pqxx::row& row) const;
    std::string toDbTime(const std::chrono::system_clock::time_point& tp) const;
    std::chrono::system_clock::time_point fromDbTime(const std::string& ts) const;

    DbConfig config_;
    std::vector<std::unique_ptr<pqxx::connection>> connections_;
    std::vector<pqxx::connection*> available_;
    mutable std::mutex pool_mutex_;
    std::atomic<bool> connected_{false};

    // in-memory caches for active/completed orders (fast lookups)
    mutable std::mutex cache_mutex_;
    std::unordered_set<OrderId> active_orders_;
    std::unordered_set<OrderId> completed_orders_;
    std::vector<std::string> system_events_;
    std::unordered_map<std::string, Order> order_cache_;
};

}
