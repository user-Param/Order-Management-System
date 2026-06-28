#include "../include/exchange3.h"

namespace oms {

Exchange3::Exchange3() {}

bool Exchange3::connect() {
    connected_ = true;
    return true;
}

bool Exchange3::disconnect() {
    connected_ = false;
    return true;
}

ExchangeResponse Exchange3::placeOrder(const Order& order) {
    if (!connected_) {
        return {false, "Exchange3 not connected", "", OrderState::REJECTED, 0, 0};
    }

    ExchangeResponse resp;
    resp.success = true;
    resp.exchange_order_id = "EX3-" + order.id;
    resp.state = OrderState::ACKNOWLEDGED;
    resp.filled_quantity = 0.0;
    resp.avg_fill_price = 0.0;
    resp.message = "Order acknowledged by Exchange3, pending execution";

    return resp;
}

ExchangeResponse Exchange3::cancelOrder(const std::string& order_id) {
    if (!connected_) {
        return {false, "Exchange3 not connected", "", OrderState::REJECTED, 0, 0};
    }
    return {true, "Order cancelled on Exchange3", order_id, OrderState::CANCELLED, 0, 0};
}

ExchangeResponse Exchange3::modifyOrder(const std::string& order_id, double price, double quantity) {
    if (!connected_) {
        return {false, "Exchange3 not connected", "", OrderState::REJECTED, 0, 0};
    }
    return {true, "Order modified on Exchange3", order_id, OrderState::MODIFIED, 0, 0};
}

double Exchange3::getBalance() {
    return balance_;
}

std::vector<Position> Exchange3::getPositions() {
    return positions_;
}

std::string Exchange3::name() const {
    return "EXCHANGE3";
}

}
