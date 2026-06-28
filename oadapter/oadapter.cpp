#include "oadapter.h"
#include "../include/logger.h"

namespace oms {

OrderAdapter::OrderAdapter() {}

OrderAdapter::~OrderAdapter() = default;

void OrderAdapter::registerExchange(std::unique_ptr<IExchange> exchange) {
    std::string name = exchange->name();
    exchanges_[name] = std::move(exchange);
    Logger::info("Exchange registered: " + name);
}

ExchangeResponse OrderAdapter::routeOrder(const Order& order) {
    IExchange* ex = getExchange(order.exchange);
    if (!ex) {
        return {false, "Exchange not found: " + order.exchange, "", OrderState::REJECTED, 0, 0};
    }
    Logger::info("Routing order " + order.id + " to " + order.exchange);
    return ex->placeOrder(order);
}

ExchangeResponse OrderAdapter::cancelOrder(const std::string& exchange, const std::string& order_id) {
    IExchange* ex = getExchange(exchange);
    if (!ex) {
        return {false, "Exchange not found: " + exchange, "", OrderState::REJECTED, 0, 0};
    }
    return ex->cancelOrder(order_id);
}

ExchangeResponse OrderAdapter::modifyOrder(const std::string& exchange, const std::string& order_id, double price, double quantity) {
    IExchange* ex = getExchange(exchange);
    if (!ex) {
        return {false, "Exchange not found: " + exchange, "", OrderState::REJECTED, 0, 0};
    }
    return ex->modifyOrder(order_id, price, quantity);
}

bool OrderAdapter::connectExchange(const std::string& exchange) {
    IExchange* ex = getExchange(exchange);
    if (!ex) return false;
    bool result = ex->connect();
    if (result) {
        Logger::info("Exchange Connected: " + exchange);
    }
    return result;
}

bool OrderAdapter::disconnectExchange(const std::string& exchange) {
    IExchange* ex = getExchange(exchange);
    if (!ex) return false;
    bool result = ex->disconnect();
    if (result) {
        Logger::info("Exchange Disconnected: " + exchange);
    }
    return result;
}

std::vector<std::string> OrderAdapter::connectedExchanges() const {
    std::vector<std::string> result;
    for (const auto& pair : exchanges_) {
        result.push_back(pair.first);
    }
    return result;
}

bool OrderAdapter::hasExchange(const std::string& name) const {
    return exchanges_.count(name) > 0;
}

IExchange* OrderAdapter::getExchange(const std::string& name) {
    auto it = exchanges_.find(name);
    if (it != exchanges_.end()) {
        return it->second.get();
    }
    return nullptr;
}

}
