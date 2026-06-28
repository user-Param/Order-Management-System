#pragma once

#include "iexchange.h"

namespace oms {

class Exchange3 : public IExchange {
public:
    Exchange3();

    bool connect() override;
    bool disconnect() override;

    ExchangeResponse placeOrder(const Order& order) override;
    ExchangeResponse cancelOrder(const std::string& order_id) override;
    ExchangeResponse modifyOrder(const std::string& order_id, double price, double quantity) override;

    double getBalance() override;
    std::vector<Position> getPositions() override;

    std::string name() const override;

private:
    bool connected_ = false;
    double balance_ = 2000000.0;
    std::vector<Position> positions_;
};

}
