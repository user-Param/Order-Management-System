#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <functional>

namespace oms {

enum class OrderState {
    NEW,
    VALIDATING,
    RISK_APPROVED,
    RISK_REJECTED,
    ROUTING,
    SENT,
    ACKNOWLEDGED,
    PARTIAL_FILL,
    FILLED,
    REJECTED,
    CANCELLED,
    MODIFIED
};

enum class OrderType {
    MARKET,
    LIMIT,
    STOP,
    STOP_LIMIT
};

enum class OrderSide {
    BUY,
    SELL
};

enum class RiskDecision {
    APPROVED,
    REJECTED
};

using OrderId = std::string;
using ClientId = std::string;

struct Order {
    OrderId id;
    ClientId client_id;
    std::string client_order_id;
    std::string exchange_order_id;
    std::string strategy_id;
    std::string account_id;
    std::string symbol;
    OrderSide side = OrderSide::BUY;
    OrderType type = OrderType::LIMIT;
    double quantity = 0.0;
    double price = 0.0;
    double stop_price = 0.0;
    std::string exchange;
    OrderState state = OrderState::NEW;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    std::string reject_reason;
    double filled_quantity = 0.0;
    double remaining_quantity = 0.0;
    double avg_fill_price = 0.0;
    std::string tif = "DAY";
};

struct OrderRequest {
    ClientId client_id;
    std::string symbol;
    OrderSide side = OrderSide::BUY;
    OrderType type = OrderType::LIMIT;
    double quantity = 0.0;
    double price = 0.0;
    double stop_price = 0.0;
    std::string exchange;
};

struct OrderResponse {
    OrderId id;
    OrderState state = OrderState::NEW;
    std::string message;
    std::string reject_reason;
};

struct ExecutionReport {
    OrderId order_id;
    std::string exec_id;
    OrderSide side = OrderSide::BUY;
    std::string symbol;
    double quantity = 0.0;
    double price = 0.0;
    double leaves_qty = 0.0;
    double cum_qty = 0.0;
    double avg_price = 0.0;
    OrderState state = OrderState::NEW;
    std::chrono::system_clock::time_point timestamp;
};

struct Position {
    std::string symbol;
    double quantity = 0.0;
    double avg_price = 0.0;
    double realized_pnl = 0.0;
    double unrealized_pnl = 0.0;
};

struct Portfolio {
    ClientId client_id;
    double total_equity = 0.0;
    double used_margin = 0.0;
    double free_margin = 0.0;
    double unrealized_pnl = 0.0;
    double realized_pnl = 0.0;
    double buying_power = 0.0;
    double leverage = 1.0;
    std::vector<Position> positions;
};

struct RiskResult {
    RiskDecision decision = RiskDecision::REJECTED;
    std::string reason;
};

struct ExchangeResponse {
    bool success = false;
    std::string message;
    std::string exchange_order_id;
    OrderState state = OrderState::NEW;
    double filled_quantity = 0.0;
    double avg_fill_price = 0.0;
};

struct Account {
    ClientId client_id;
    std::string api_key;
    bool authenticated = false;
    double balance = 0.0;
    double margin = 0.0;
};

struct AuditEntry {
    std::string action;
    std::string details;
    std::chrono::system_clock::time_point timestamp;
};

using ExecutionCallback = std::function<void(const ExecutionReport&)>;

inline std::string stateToString(OrderState state) {
    switch (state) {
        case OrderState::NEW: return "NEW";
        case OrderState::VALIDATING: return "VALIDATING";
        case OrderState::RISK_APPROVED: return "RISK_APPROVED";
        case OrderState::RISK_REJECTED: return "RISK_REJECTED";
        case OrderState::ROUTING: return "ROUTING";
        case OrderState::SENT: return "SENT";
        case OrderState::ACKNOWLEDGED: return "ACKNOWLEDGED";
        case OrderState::PARTIAL_FILL: return "PARTIAL_FILL";
        case OrderState::FILLED: return "FILLED";
        case OrderState::REJECTED: return "REJECTED";
        case OrderState::CANCELLED: return "CANCELLED";
        case OrderState::MODIFIED: return "MODIFIED";
    }
    return "UNKNOWN";
}

inline std::string sideToString(OrderSide side) {
    return side == OrderSide::BUY ? "BUY" : "SELL";
}

inline std::string typeToString(OrderType type) {
    switch (type) {
        case OrderType::MARKET: return "MARKET";
        case OrderType::LIMIT: return "LIMIT";
        case OrderType::STOP: return "STOP";
        case OrderType::STOP_LIMIT: return "STOP_LIMIT";
    }
    return "UNKNOWN";
}

}
