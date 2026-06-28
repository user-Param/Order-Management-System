#pragma once

#include "../../include/models.h"

namespace oms {

class IExchange {
public:
    virtual ~IExchange() = default;

    virtual bool connect() = 0;
    virtual bool disconnect() = 0;

    virtual ExchangeResponse placeOrder(const Order& order) = 0;
    virtual ExchangeResponse cancelOrder(const std::string& order_id) = 0;
    virtual ExchangeResponse modifyOrder(const std::string& order_id, double price, double quantity) = 0;

    virtual double getBalance() = 0;
    virtual std::vector<Position> getPositions() = 0;

    virtual std::string name() const = 0;
};

}
