#pragma once

#include "../include/models.h"
#include "../exchange/include/iexchange.h"
#include <memory>
#include <unordered_map>

namespace oms {

class OrderAdapter {
public:
    OrderAdapter();
    ~OrderAdapter();

    void registerExchange(std::unique_ptr<IExchange> exchange);
    ExchangeResponse routeOrder(const Order& order);
    ExchangeResponse cancelOrder(const std::string& exchange, const std::string& order_id);
    ExchangeResponse modifyOrder(const std::string& exchange, const std::string& order_id, double price, double quantity);
    bool connectExchange(const std::string& exchange);
    bool disconnectExchange(const std::string& exchange);
    std::vector<std::string> connectedExchanges() const;
    bool hasExchange(const std::string& name) const;

private:
    IExchange* getExchange(const std::string& name);
    std::unordered_map<std::string, std::unique_ptr<IExchange>> exchanges_;
};

}
