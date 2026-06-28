#include "../include/exchange2.h"

namespace oms {

Exchange2::Exchange2() {}

bool Exchange2::connect() {
    connected_ = true;
    return true;
}

bool Exchange2::disconnect() {
    connected_ = false;
    return true;
}

ExchangeResponse Exchange2::placeOrder(const Order& order) {
    if (!connected_) {
        return {false, "Exchange2 not connected", "", OrderState::REJECTED, 0, 0};
    }

    ExchangeResponse resp;
    resp.success = true;
    resp.exchange_order_id = "EX2-" + order.id;

    if (order.type == OrderType::MARKET) {
        resp.state = OrderState::FILLED;
        resp.filled_quantity = order.quantity;
        resp.avg_fill_price = order.price;
        resp.message = "Market order filled on Exchange2";
    } else {
        resp.state = OrderState::PARTIAL_FILL;
        resp.filled_quantity = order.quantity * 0.5;
        resp.avg_fill_price = order.price;
        resp.message = "Limit order partially filled on Exchange2";
    }

    open_orders_[resp.exchange_order_id] = order;

    Position pos;
    pos.symbol = order.symbol;
    pos.quantity = (order.side == OrderSide::BUY ? 1 : -1) * resp.filled_quantity;
    pos.avg_price = resp.avg_fill_price;

    bool found = false;
    for (auto& p : positions_) {
        if (p.symbol == order.symbol) {
            double total_qty = p.quantity + pos.quantity;
            p.avg_price = total_qty != 0
                ? (p.avg_price * std::abs(p.quantity) + pos.avg_price * resp.filled_quantity)
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

ExchangeResponse Exchange2::cancelOrder(const std::string& order_id) {
    if (!connected_) {
        return {false, "Exchange2 not connected", "", OrderState::REJECTED, 0, 0};
    }
    return {true, "Order cancelled on Exchange2", order_id, OrderState::CANCELLED, 0, 0};
}

ExchangeResponse Exchange2::modifyOrder(const std::string& order_id, double price, double quantity) {
    if (!connected_) {
        return {false, "Exchange2 not connected", "", OrderState::REJECTED, 0, 0};
    }
    return {true, "Order modified on Exchange2", order_id, OrderState::MODIFIED, 0, 0};
}

double Exchange2::getBalance() {
    return balance_;
}

std::vector<Position> Exchange2::getPositions() {
    return positions_;
}

std::string Exchange2::name() const {
    return "EXCHANGE2";
}

}
