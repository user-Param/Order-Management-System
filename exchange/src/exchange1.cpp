#include "../include/exchange1.h"

namespace oms {

Exchange1::Exchange1() {}

bool Exchange1::connect() {
    connected_ = true;
    return true;
}

bool Exchange1::disconnect() {
    connected_ = false;
    return true;
}

ExchangeResponse Exchange1::placeOrder(const Order& order) {
    if (!connected_) {
        return {false, "Exchange1 not connected", "", OrderState::REJECTED, 0, 0};
    }

    ExchangeResponse resp;
    resp.success = true;
    resp.exchange_order_id = "EX1-" + order.id;
    resp.state = OrderState::FILLED;
    resp.filled_quantity = order.quantity;
    resp.avg_fill_price = order.price;
    resp.message = "Order filled immediately on Exchange1";

    Position pos;
    pos.symbol = order.symbol;
    pos.quantity = (order.side == OrderSide::BUY ? 1 : -1) * order.quantity;
    pos.avg_price = order.price;

    bool found = false;
    for (auto& p : positions_) {
        if (p.symbol == order.symbol) {
            double total_qty = p.quantity + pos.quantity;
            p.avg_price = total_qty != 0
                ? (p.avg_price * std::abs(p.quantity) + pos.avg_price * order.quantity)
                  / std::abs(total_qty)
                : 0.0;
            p.quantity = total_qty;
            found = true;
            break;
        }
    }
    if (!found) {
        positions_.push_back(pos);
    }

    return resp;
}

ExchangeResponse Exchange1::cancelOrder(const std::string& order_id) {
    if (!connected_) {
        return {false, "Exchange1 not connected", "", OrderState::REJECTED, 0, 0};
    }
    return {true, "Order cancelled on Exchange1", order_id, OrderState::CANCELLED, 0, 0};
}

ExchangeResponse Exchange1::modifyOrder(const std::string& order_id, double price, double quantity) {
    if (!connected_) {
        return {false, "Exchange1 not connected", "", OrderState::REJECTED, 0, 0};
    }
    return {true, "Order modified on Exchange1", order_id, OrderState::MODIFIED, 0, 0};
}

double Exchange1::getBalance() {
    return balance_;
}

std::vector<Position> Exchange1::getPositions() {
    return positions_;
}

std::string Exchange1::name() const {
    return "EXCHANGE1";
}

}
