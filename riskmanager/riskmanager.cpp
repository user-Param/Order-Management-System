#include "riskmanager.h"

namespace oms {

RiskResult RiskManager::validateOrder(const OrderRequest& request, const Portfolio& portfolio) {
    if (!checkRequiredFields(request)) {
        return {RiskDecision::REJECTED, "Missing required fields"};
    }

    if (!isExchangeSupported(request.exchange)) {
        return {RiskDecision::REJECTED, "Exchange not supported: " + request.exchange};
    }

    if (!isSymbolValid(request.symbol, request.exchange)) {
        return {RiskDecision::REJECTED, "Symbol not found: " + request.symbol};
    }

    if (!isQuantityValid(request.quantity, request.type)) {
        return {RiskDecision::REJECTED, "Invalid quantity: " + std::to_string(request.quantity)};
    }

    if (!isPriceValid(request.price, request.type)) {
        return {RiskDecision::REJECTED, "Invalid price: " + std::to_string(request.price)};
    }

    if (!hasBuyingPower(request, portfolio)) {
        return {RiskDecision::REJECTED, "Insufficient buying power"};
    }

    if (!withinPositionLimits(request, portfolio)) {
        return {RiskDecision::REJECTED, "Position limit exceeded for " + request.symbol};
    }

    if (!withinExposureLimits(request, portfolio)) {
        return {RiskDecision::REJECTED, "Exposure limit exceeded"};
    }

    if (!withinDailyLoss(portfolio)) {
        return {RiskDecision::REJECTED, "Daily loss limit exceeded"};
    }

    return {RiskDecision::APPROVED, "Risk approved"};
}

bool RiskManager::checkRequiredFields(const OrderRequest& request) {
    if (request.client_id.empty()) return false;
    if (request.symbol.empty()) return false;
    if (request.exchange.empty()) return false;
    if (request.quantity <= 0) return false;
    if (request.type == OrderType::LIMIT && request.price <= 0) return false;
    if (request.type == OrderType::STOP && request.stop_price <= 0) return false;
    if (request.type == OrderType::STOP_LIMIT && (request.price <= 0 || request.stop_price <= 0)) return false;
    return true;
}

bool RiskManager::isExchangeSupported(const std::string& exchange) {
    return supported_exchanges_.count(exchange) > 0;
}

bool RiskManager::isSymbolValid(const std::string& symbol, const std::string& exchange) {
    return valid_symbols_.count(symbol) > 0;
}

bool RiskManager::isQuantityValid(double quantity, OrderType type) {
    if (quantity <= 0) return false;
    if (quantity > 100000) return false;
    return true;
}

bool RiskManager::isPriceValid(double price, OrderType type) {
    if (type == OrderType::MARKET) return true;
    if (price <= 0) return false;
    if (price > 1000000) return false;
    return true;
}

bool RiskManager::hasBuyingPower(const OrderRequest& request, const Portfolio& portfolio) {
    double estimated_cost = request.price * request.quantity;
    if (request.type == OrderType::MARKET) {
        estimated_cost = 100000.0 * request.quantity;
    }
    return estimated_cost <= portfolio.buying_power;
}

bool RiskManager::withinPositionLimits(const OrderRequest& request, const Portfolio& portfolio) {
    for (const auto& pos : portfolio.positions) {
        if (pos.symbol == request.symbol) {
            double new_qty = std::abs(pos.quantity) + request.quantity;
            if (new_qty > max_position_per_symbol_) {
                return false;
            }
            break;
        }
    }
    return request.quantity <= max_position_per_symbol_;
}

bool RiskManager::withinExposureLimits(const OrderRequest& request, const Portfolio& portfolio) {
    double current_exposure = 0.0;
    for (const auto& pos : portfolio.positions) {
        current_exposure += std::abs(pos.quantity) * pos.avg_price;
    }
    double new_exposure = current_exposure + (request.price * request.quantity);
    return new_exposure <= max_exposure_;
}

bool RiskManager::withinDailyLoss(const Portfolio& portfolio) {
    return portfolio.realized_pnl >= -max_daily_loss_;
}

}
