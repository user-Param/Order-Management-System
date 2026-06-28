#include "oms.h"
#include "../exchange/include/exchange1.h"
#include "../exchange/include/exchange2.h"
#include "../exchange/include/exchange3.h"

#include <boost/asio/signal_set.hpp>
#include <thread>
#include <tuple>
#include <algorithm>

namespace oms {

OMS::OMS()
    : database_(std::make_unique<Database>(DbConfig{
          "127.0.0.1", 5432, "oms", "oms_user", "oms_pass", 4, 3
      }))
    , risk_manager_(std::make_unique<RiskManager>())
    , adapter_(std::make_unique<OrderAdapter>())
{
    Logger::info("OMS initializing...");
    if (database_->connect()) {
        Logger::info("Database connected successfully");
        auto open_orders = database_->loadOpenOrders();
        auto positions = database_->loadPositions();
        auto portfolio = database_->loadPortfolio("default");
        Logger::info("Recovery: loaded " + std::to_string(open_orders.size())
                     + " open orders, " + std::to_string(positions.size())
                     + " positions");
    } else {
        Logger::warn("Database connection failed - running without persistence");
    }
    database_->storeSystemEvent("OMS initialized");
}

OMS::~OMS() {
    stop();
}

void OMS::start() {
    if (running_) return;
    running_ = true;

    auto ex1 = std::make_unique<Exchange1>();
    auto ex2 = std::make_unique<Exchange2>();
    auto ex3 = std::make_unique<Exchange3>();

    adapter_->registerExchange(std::move(ex1));
    adapter_->registerExchange(std::move(ex2));
    adapter_->registerExchange(std::move(ex3));

    adapter_->connectExchange("EXCHANGE1");
    adapter_->connectExchange("EXCHANGE2");
    adapter_->connectExchange("EXCHANGE3");

    Portfolio default_portfolio;
    default_portfolio.client_id = "default";
    default_portfolio.total_equity = 100000.0;
    default_portfolio.buying_power = 100000.0;
    default_portfolio.free_margin = 100000.0;
    default_portfolio.leverage = 1.0;
    database_->setPortfolio(default_portfolio);

    Logger::info("Starting OMS server on port " + std::to_string(port_));

    try {
        asio::io_context io;
        tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), port_));
        acceptor.set_option(tcp::acceptor::reuse_address(true));
        Logger::info("OMS listening on port " + std::to_string(port_));

        database_->storeSystemEvent("OMS server started on port " + std::to_string(port_));
        Logger::info("Server started");

        while (running_) {
            beast::error_code ec;
            tcp::socket socket(io);
            acceptor.accept(socket, ec);
            if (ec) {
                if (ec == asio::error::operation_aborted) break;
                Logger::error("Accept error: " + ec.message());
                continue;
            }

            Logger::info("Client connected: " + socket.remote_endpoint().address().to_string());
            std::thread([this, s = std::move(socket)]() mutable {
                beast::tcp_stream stream(std::move(s));
                this->handleConnection(std::move(stream));
            }).detach();
        }
    } catch (const std::exception& e) {
        Logger::error("Server error: " + std::string(e.what()));
    }

    running_ = false;
    Logger::info("OMS server stopped");
}

void OMS::stop() {
    running_ = false;
}

bool OMS::isRunning() const {
    return running_;
}

void OMS::handleConnection(beast::tcp_stream stream) {
    beast::flat_buffer buffer;
    http::request<http::string_body> req;

    beast::error_code ec;
    http::read(stream, buffer, req, ec);
    if (ec) {
        Logger::error("Failed to read HTTP request: " + ec.message());
        return;
    }

    if (websocket::is_upgrade(req)) {
        handleWebSocketUpgrade(std::move(stream), req);
    } else {
        handleHttpRequest(stream, req);
    }
}

void OMS::handleHttpRequest(beast::tcp_stream& stream, http::request<http::string_body>& req) {
    http::response<http::string_body> res;
    res.version(req.version());
    res.keep_alive(false);
    res.set(http::field::server, "OMS/1.0");
    res.set(http::field::content_type, "application/json");

    std::string path(req.target());
    std::string method(req.method_string());

    try {
        if (path == "/health" && method == "GET") {
            buildResponse(res, http::status::ok, jsonSuccess(R"({"status":"healthy"})"));
        } else if (path == "/status" && method == "GET") {
            std::string data = R"({"status":"running","active_orders":)" + std::to_string(database_->activeOrderCount()) + R"(,"port":)" + std::to_string(port_) + "}";
            buildResponse(res, http::status::ok, jsonSuccess(data));
        } else if (path == "/metrics" && method == "GET") {
            std::string data = R"({"active_orders":)" + std::to_string(database_->activeOrderCount())
                + R"(,"total_orders":)" + std::to_string(database_->getOrders().size())
                + R"(,"completed_orders":)" + std::to_string(database_->getCompletedOrders().size())
                + R"(,"audit_entries":)" + std::to_string(database_->getAuditLog().size()) + "}";
            buildResponse(res, http::status::ok, jsonSuccess(data));
        } else if (path == "/orders" && method == "GET") {
            auto orders = database_->getOrders();
            std::string json = "[";
            for (size_t i = 0; i < orders.size(); ++i) {
                if (i > 0) json += ",";
                json += orderToJson(orders[i]);
            }
            json += "]";
            buildResponse(res, http::status::ok, jsonSuccess(json));
        } else if (path.find("/orders/") == 0 && method == "GET") {
            std::string id = path.substr(8);
            if (database_->orderExists(id)) {
                buildResponse(res, http::status::ok, jsonSuccess(orderToJson(database_->getOrder(id))));
            } else {
                buildResponse(res, http::status::not_found, jsonError("Order not found: " + id));
            }
        } else if (path == "/positions" && method == "GET") {
            auto positions = database_->getPositions();
            std::string json = "[";
            for (size_t i = 0; i < positions.size(); ++i) {
                if (i > 0) json += ",";
                json += positionToJson(positions[i]);
            }
            json += "]";
            buildResponse(res, http::status::ok, jsonSuccess(json));
        } else if (path == "/portfolio" && method == "GET") {
            auto portfolio = database_->getPortfolio("default");
            buildResponse(res, http::status::ok, jsonSuccess(portfolioToJson(portfolio)));
        } else if (path == "/risk" && method == "GET") {
            auto events = database_->getRiskEvents();
            std::string json = "[";
            for (size_t i = 0; i < events.size(); ++i) {
                if (i > 0) json += ",";
                json += R"({"order_id":")" + jsonEscape(std::get<0>(events[i]))
                    + R"(","event":")" + jsonEscape(std::get<1>(events[i]))
                    + R"(","reason":")" + jsonEscape(std::get<2>(events[i]))
                    + R"(","severity":")" + jsonEscape(std::get<3>(events[i])) + "\"}";
            }
            json += "]";
            buildResponse(res, http::status::ok, jsonSuccess(json));
        } else if (path == "/config" && method == "GET") {
            buildResponse(res, http::status::ok, jsonSuccess(R"({"port":4444,"version":"1.0","exchanges":["EXCHANGE1","EXCHANGE2","EXCHANGE3"]})"));
        } else if (path == "/exchange/connect" && method == "POST") {
            std::string exchange = "EXCHANGE1";
            size_t p = req.body().find("\"exchange\"");
            if (p != std::string::npos) {
                size_t start = req.body().find('"', p + 10);
                if (start != std::string::npos) {
                    size_t end = req.body().find('"', start + 1);
                    if (end != std::string::npos) {
                        exchange = req.body().substr(start + 1, end - start - 1);
                    }
                }
            }
            bool result = adapter_->connectExchange(exchange);
            buildResponse(res, result ? http::status::ok : http::status::bad_request,
                result ? jsonSuccess(R"({"message":"Exchange connected"})") : jsonError("Failed to connect exchange"));
        } else if (path == "/exchange/disconnect" && method == "POST") {
            std::string exchange = "EXCHANGE1";
            size_t p = req.body().find("\"exchange\"");
            if (p != std::string::npos) {
                size_t start = req.body().find('"', p + 10);
                if (start != std::string::npos) {
                    size_t end = req.body().find('"', start + 1);
                    if (end != std::string::npos) {
                        exchange = req.body().substr(start + 1, end - start - 1);
                    }
                }
            }
            bool result = adapter_->disconnectExchange(exchange);
            buildResponse(res, result ? http::status::ok : http::status::bad_request,
                result ? jsonSuccess(R"({"message":"Exchange disconnected"})") : jsonError("Failed to disconnect exchange"));
        } else if (path == "/config" && method == "POST") {
            database_->storeSystemEvent("Configuration updated");
            buildResponse(res, http::status::ok, jsonSuccess(R"({"message":"Configuration updated"})"));
        } else if (path == "/login" && method == "POST") {
            buildResponse(res, http::status::ok, jsonSuccess(R"({"message":"Login successful","token":"oms-token"})"));
        } else {
            buildResponse(res, http::status::not_found, jsonError("Endpoint not found: " + path));
        }
    } catch (const std::exception& e) {
        Logger::error("HTTP handler error: " + std::string(e.what()));
        buildResponse(res, http::status::internal_server_error, jsonError("Internal server error"));
    }

    sendHttpResponse(stream, res);
}

void OMS::handleWebSocketUpgrade(beast::tcp_stream stream, http::request<http::string_body>& req) {
    auto session = std::make_shared<WebSocketSession>(std::move(stream));

    beast::error_code ec;
    session->ws.accept(req, ec);
    if (ec) {
        Logger::error("WebSocket upgrade failed: " + ec.message());
        return;
    }

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.push_back(session);
    }

    Logger::info("WebSocket session established");
    database_->storeSystemEvent("WebSocket client connected");

    handleWebSocketSession(session);

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        session->closed = true;
        sessions_.erase(
            std::remove_if(sessions_.begin(), sessions_.end(),
                [](const auto& s) { return s->closed; }),
            sessions_.end());
    }

    Logger::info("WebSocket session closed");
}

void OMS::handleWebSocketSession(std::shared_ptr<WebSocketSession> session) {
    beast::flat_buffer buffer;
    while (!session->closed) {
        beast::error_code ec;
        session->ws.read(buffer, ec);
        if (ec == websocket::error::closed || ec == asio::error::eof) {
            break;
        }
        if (ec) {
            Logger::error("WebSocket read error: " + ec.message());
            break;
        }

        std::string msg = beast::buffers_to_string(buffer.data());
        buffer.consume(buffer.size());
        handleWebSocketMessage(session, msg);
    }
}

void OMS::handleWebSocketMessage(std::shared_ptr<WebSocketSession> session, const std::string& message) {
    size_t type_start = message.find("\"type\"");
    if (type_start == std::string::npos) {
        sendWebSocketMessage(session, "ERROR", jsonError("Missing message type"));
        return;
    }

    size_t value_start = message.find('"', type_start + 7);
    if (value_start == std::string::npos) return;
    size_t value_end = message.find('"', value_start + 1);
    if (value_end == std::string::npos) return;

    std::string type = message.substr(value_start + 1, value_end - value_start - 1);

    std::string payload;
    size_t data_start = message.find("\"data\"");
    if (data_start != std::string::npos) {
        size_t brace_start = message.find('{', data_start);
        if (brace_start != std::string::npos) {
            payload = message.substr(brace_start);
            size_t brace_end = payload.rfind('}');
            if (brace_end != std::string::npos) {
                payload = payload.substr(0, brace_end + 1);
            }
        }
    }

    if (type == "AUTH") {
        handleAuth(session, payload);
    } else if (type == "PLACE_ORDER") {
        handlePlaceOrder(session, payload);
    } else if (type == "CANCEL_ORDER") {
        handleCancelOrder(session, payload);
    } else if (type == "MODIFY_ORDER") {
        handleModifyOrder(session, payload);
    } else if (type == "PING") {
        handlePing(session);
    } else {
        sendWebSocketMessage(session, "ERROR", jsonError("Unknown message type: " + type));
    }
}

void OMS::handleAuth(std::shared_ptr<WebSocketSession> session, const std::string& payload) {
    session->authenticated = true;
    session->client_id = "default";
    Logger::info("[WebSocket] Client Authenticated: " + session->client_id);
    sendWebSocketMessage(session, "AUTH_OK", R"({"client_id":"default"})");
}

void OMS::handlePlaceOrder(std::shared_ptr<WebSocketSession> session, const std::string& payload) {
    if (!session->authenticated) {
        sendWebSocketMessage(session, "ORDER_REJECTED", jsonError("Not authenticated"));
        return;
    }

    OrderRequest request = parseOrderRequest(payload);

    if (request.client_id.empty()) {
        request.client_id = session->client_id;
    }

    Logger::info("[WebSocket] Order Received via WebSocket");

    ExecutionReport report;
    OrderResponse response = processOrder(request, &report);

    if (response.state == OrderState::REJECTED) {
        sendWebSocketMessage(session, "ORDER_REJECTED",
            R"({"order_id":")" + jsonEscape(response.id) + R"(","reason":")" + jsonEscape(response.reject_reason) + "\"}");
    } else {
        sendWebSocketMessage(session, "ORDER_ACCEPTED",
            R"({"order_id":")" + jsonEscape(response.id) + R"(","state":")" + stateToString(response.state) + "\"}");
    }

    if (!report.order_id.empty()) {
        publishExecutionReport(report);
    }
}

void OMS::handleCancelOrder(std::shared_ptr<WebSocketSession> session, const std::string& payload) {
    if (!session->authenticated) {
        sendWebSocketMessage(session, "ERROR", jsonError("Not authenticated"));
        return;
    }

    std::string order_id;
    size_t p = payload.find("\"order_id\"");
    if (p != std::string::npos) {
        size_t start = payload.find('"', p + 11);
        if (start != std::string::npos) {
            size_t end = payload.find('"', start + 1);
            if (end != std::string::npos) {
                order_id = payload.substr(start + 1, end - start - 1);
            }
        }
    }

    if (order_id.empty()) {
        sendWebSocketMessage(session, "ERROR", jsonError("Missing order_id"));
        return;
    }

    if (!database_->orderExists(order_id)) {
        sendWebSocketMessage(session, "CANCELLED",
            R"({"order_id":")" + jsonEscape(order_id) + R"(","state":"NOT_FOUND"})");
        return;
    }

    Order order = database_->getOrder(order_id);
    adapter_->cancelOrder(order.exchange, order_id);

    order.state = OrderState::CANCELLED;
    order.updated_at = std::chrono::system_clock::now();
    database_->updateOrder(order);
    database_->removeActiveOrder(order_id);
    database_->addCompletedOrder(order);
    database_->storeAudit({"Cancelled", order_id});

    Logger::info("[Order] Cancelled: " + order_id);

    ExecutionReport report;
    report.order_id = order_id;
    report.exec_id = order_id + "-cancel";
    report.side = order.side;
    report.symbol = order.symbol;
    report.state = OrderState::CANCELLED;
    report.timestamp = order.updated_at;

    sendWebSocketMessage(session, "CANCELLED",
        R"({"order_id":")" + jsonEscape(order_id) + R"(","state":"CANCELLED"})");

    publishExecutionReport(report);
}

void OMS::handleModifyOrder(std::shared_ptr<WebSocketSession> session, const std::string& payload) {
    if (!session->authenticated) {
        sendWebSocketMessage(session, "ERROR", jsonError("Not authenticated"));
        return;
    }

    std::string order_id;
    double price = 0.0;
    double quantity = 0.0;

    size_t p = payload.find("\"order_id\"");
    if (p != std::string::npos) {
        size_t start = payload.find('"', p + 11);
        if (start != std::string::npos) {
            size_t end = payload.find('"', start + 1);
            if (end != std::string::npos) {
                order_id = payload.substr(start + 1, end - start - 1);
            }
        }
    }

    p = payload.find("\"price\"");
    if (p != std::string::npos) {
        size_t start = payload.find(':', p + 7);
        if (start != std::string::npos) {
            price = std::stod(payload.substr(start + 1));
        }
    }

    p = payload.find("\"quantity\"");
    if (p != std::string::npos) {
        size_t start = payload.find(':', p + 10);
        if (start != std::string::npos) {
            quantity = std::stod(payload.substr(start + 1));
        }
    }

    if (order_id.empty()) {
        sendWebSocketMessage(session, "ERROR", jsonError("Missing order_id"));
        return;
    }

    Order order = database_->getOrder(order_id);
    adapter_->modifyOrder(order.exchange, order_id, price, quantity);

    order.state = OrderState::MODIFIED;
    if (price > 0) order.price = price;
    if (quantity > 0) order.quantity = quantity;
    order.updated_at = std::chrono::system_clock::now();
    database_->updateOrder(order);
    database_->storeAudit({"Modified", order_id});

    Logger::info("[Order] Modified: " + order_id);
    sendWebSocketMessage(session, "MODIFIED",
        R"({"order_id":")" + jsonEscape(order_id) + R"(","state":"MODIFIED"})");
}

void OMS::handlePing(std::shared_ptr<WebSocketSession> session) {
    sendWebSocketMessage(session, "PONG", R"({"timestamp":")" + std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()) + "\"}");
}

void OMS::sendWebSocketMessage(std::shared_ptr<WebSocketSession> session, const std::string& type, const std::string& data) {
    try {
        std::string msg = R"({"type":")" + type + R"(","data":)" + data + "}";
        Logger::info("[WebSocket] Sending JSON: " + msg);
        session->ws.write(asio::buffer(msg));
    } catch (const std::exception& e) {
        Logger::error("WebSocket send error: " + std::string(e.what()));
    }
}

void OMS::broadcastExecutionReport(const ExecutionReport& report) {
    std::string json = executionReportToJson(report);
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& session : sessions_) {
        if (session->authenticated && !session->closed) {
            sendWebSocketMessage(session, "EXECUTION_REPORT", json);
        }
    }
}

OrderResponse OMS::processOrder(const OrderRequest& request, ExecutionReport* out_report) {
    OrderId id = generateOrderId();

    Order order;
    order.id = id;
    order.client_id = request.client_id;
    order.client_order_id = id;
    order.symbol = request.symbol;
    order.side = request.side;
    order.type = request.type;
    order.quantity = request.quantity;
    order.price = request.price;
    order.stop_price = request.stop_price;
    order.remaining_quantity = request.quantity;
    order.exchange = request.exchange;
    order.state = OrderState::NEW;
    order.created_at = std::chrono::system_clock::now();
    order.updated_at = order.created_at;

    Logger::info("[Order] Received: " + id);

    order.state = OrderState::VALIDATING;
    database_->storeOrder(order);
    database_->storeAudit({"Order Received", id});

    Logger::info("[Order] Validating: " + id);

    Portfolio portfolio = database_->getPortfolio(request.client_id);
    RiskResult risk = risk_manager_->validateOrder(request, portfolio);

    if (risk.decision == RiskDecision::REJECTED) {
        order.state = OrderState::REJECTED;
        order.reject_reason = risk.reason;
        order.updated_at = std::chrono::system_clock::now();
        database_->updateOrder(order);
        database_->storeRiskEvent(id, "RISK_REJECTED", risk.reason, "WARNING");
        database_->storeAudit({"Risk Rejected", id + ": " + risk.reason});
        Logger::info("[Risk] Rejected: " + id + " - " + risk.reason);

        return {id, OrderState::REJECTED, "Order rejected by risk manager", risk.reason};
    }

    database_->storeRiskEvent(id, "RISK_APPROVED", "Risk approved", "INFO");
    order.state = OrderState::RISK_APPROVED;
    order.updated_at = std::chrono::system_clock::now();
    database_->updateOrder(order);
    database_->storeAudit({"Risk Approved", id});
    Logger::info("[Risk] Approved: " + id);

    order.state = OrderState::ROUTING;
    order.updated_at = std::chrono::system_clock::now();
    database_->updateOrder(order);
    Logger::info("[Order] Routed: " + id + " to " + order.exchange);

    order.state = OrderState::SENT;
    order.updated_at = std::chrono::system_clock::now();
    database_->updateOrder(order);
    database_->addActiveOrder(id);
    database_->storeAudit({"Sent To Exchange", id});
    Logger::info("[Order] Sent: " + id + " to " + order.exchange);

    ExchangeResponse exchange_resp = adapter_->routeOrder(order);

    if (!exchange_resp.success) {
        order.state = OrderState::REJECTED;
        order.reject_reason = exchange_resp.message;
        order.updated_at = std::chrono::system_clock::now();
        database_->updateOrder(order);
        database_->removeActiveOrder(id);
        database_->addCompletedOrder(order);
        database_->storeAudit({"Rejected", id + ": " + exchange_resp.message});
        Logger::info("[Exchange] Rejected: " + id + " - " + exchange_resp.message);

        ExecutionReport report;
        report.order_id = id;
        report.exec_id = id + "-exec";
        report.side = order.side;
        report.symbol = order.symbol;
        report.state = OrderState::REJECTED;
        report.timestamp = order.updated_at;
        if (out_report) *out_report = report;

        return {id, OrderState::REJECTED, "Exchange rejected: " + exchange_resp.message, exchange_resp.message};
    }

    order.state = exchange_resp.state;
    order.filled_quantity = exchange_resp.filled_quantity;
    order.remaining_quantity = order.quantity - order.filled_quantity;
    order.avg_fill_price = exchange_resp.avg_fill_price;
    order.exchange_order_id = exchange_resp.exchange_order_id;
    order.updated_at = std::chrono::system_clock::now();
    database_->updateOrder(order);
    database_->storeAudit({"Exchange ACK", id + " - " + stateToString(order.state)});
    Logger::info("[Exchange] ACK: " + id + " - " + stateToString(order.state));

    ExecutionReport report;
    report.order_id = id;
    report.exec_id = id + "-exec";
    report.side = order.side;
    report.symbol = order.symbol;
    report.quantity = order.quantity;
    report.price = order.avg_fill_price;
    report.leaves_qty = order.quantity - order.filled_quantity;
    report.cum_qty = order.filled_quantity;
    report.avg_price = order.avg_fill_price;
    report.state = order.state;
    report.timestamp = order.updated_at;
    database_->storeExecution(report);
    if (out_report) *out_report = report;

    if (order.state == OrderState::FILLED || order.state == OrderState::REJECTED || order.state == OrderState::CANCELLED) {
        database_->removeActiveOrder(id);
        database_->addCompletedOrder(order);
    }

    if (order.filled_quantity > 0) {
        Position pos;
        pos.symbol = order.symbol;
        pos.quantity = (order.side == OrderSide::BUY ? 1 : -1) * order.filled_quantity;
        pos.avg_price = order.avg_fill_price;

        Position existing = database_->getPosition(order.symbol);
        if (existing.quantity != 0 || existing.symbol.empty()) {
            double total_qty = existing.quantity + pos.quantity;
            pos.avg_price = total_qty != 0
                ? (existing.avg_price * std::abs(existing.quantity) + pos.avg_price * order.filled_quantity)
                  / std::abs(total_qty)
                : 0.0;
            pos.quantity = total_qty;
        }
        database_->updatePosition(pos);
    }

    Logger::info("[Order] Completed: " + id);

    return {id, order.state, "Order processed", ""};
}

OrderId OMS::generateOrderId() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    uint64_t counter = order_counter_++;
    return "ORD-" + std::to_string(ms) + "-" + std::to_string(counter);
}

void OMS::publishExecutionReport(const ExecutionReport& report) {
    std::string state;
    switch (report.state) {
        case OrderState::PARTIAL_FILL: state = "PARTIAL_FILL"; break;
        case OrderState::FILLED: state = "FILLED"; break;
        case OrderState::REJECTED: state = "REJECTED"; break;
        case OrderState::CANCELLED: state = "CANCELLED"; break;
        default: state = stateToString(report.state);
    }

    Logger::info("Execution Report: " + report.order_id + " - " + state);

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& session : sessions_) {
        if (session->authenticated && !session->closed) {
            sendWebSocketMessage(session, "EXECUTION_REPORT", executionReportToJson(report));
        }
    }
}

void OMS::buildResponse(http::response<http::string_body>& res, http::status status, const std::string& body) {
    res.result(status);
    res.body() = body;
    res.prepare_payload();
}

void OMS::sendHttpResponse(beast::tcp_stream& stream, http::response<http::string_body>& res) {
    http::write(stream, res);
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_send, ec);
}

std::string OMS::jsonEscape(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 8);
    for (char c : raw) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string OMS::jsonError(const std::string& message) {
    return R"({"error":")" + jsonEscape(message) + "\"}";
}

std::string OMS::jsonSuccess(const std::string& data) {
    return R"({"success":true,"data":)" + data + "}";
}

std::string OMS::orderToJson(const Order& order) {
    auto t = std::chrono::system_clock::to_time_t(order.created_at);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");

    return std::string("{") +
        R"("id":")" + jsonEscape(order.id) + "\"" +
        R"(,"client_id":")" + jsonEscape(order.client_id) + "\"" +
        R"(,"exchange_order_id":")" + jsonEscape(order.exchange_order_id) + "\"" +
        R"(,"symbol":")" + jsonEscape(order.symbol) + "\"" +
        R"(,"side":")" + sideToString(order.side) + "\"" +
        R"(,"type":")" + typeToString(order.type) + "\"" +
        R"(,"quantity":)" + std::to_string(order.quantity) +
        R"(,"price":)" + std::to_string(order.price) +
        R"(,"exchange":")" + jsonEscape(order.exchange) + "\"" +
        R"(,"state":")" + stateToString(order.state) + "\"" +
        R"(,"filled_quantity":)" + std::to_string(order.filled_quantity) +
        R"(,"remaining_quantity":)" + std::to_string(order.remaining_quantity) +
        R"(,"avg_fill_price":)" + std::to_string(order.avg_fill_price) +
        R"(,"created_at":")" + ss.str() + "\"" +
        R"(,"reject_reason":")" + jsonEscape(order.reject_reason) + "\"" +
        "}";
}

std::string OMS::positionToJson(const Position& pos) {
    return std::string("{") +
        R"("symbol":")" + jsonEscape(pos.symbol) + "\"" +
        R"(,"quantity":)" + std::to_string(pos.quantity) +
        R"(,"avg_price":)" + std::to_string(pos.avg_price) +
        R"(,"realized_pnl":)" + std::to_string(pos.realized_pnl) +
        R"(,"unrealized_pnl":)" + std::to_string(pos.unrealized_pnl) +
        "}";
}

std::string OMS::portfolioToJson(const Portfolio& portfolio) {
    std::string json = "{";
    json += R"("client_id":")" + jsonEscape(portfolio.client_id) + "\"";
    json += R"(,"total_equity":)" + std::to_string(portfolio.total_equity);
    json += R"(,"used_margin":)" + std::to_string(portfolio.used_margin);
    json += R"(,"free_margin":)" + std::to_string(portfolio.free_margin);
    json += R"(,"unrealized_pnl":)" + std::to_string(portfolio.unrealized_pnl);
    json += R"(,"realized_pnl":)" + std::to_string(portfolio.realized_pnl);
    json += R"(,"buying_power":)" + std::to_string(portfolio.buying_power);
    json += R"(,"leverage":)" + std::to_string(portfolio.leverage);
    json += R"(,"positions":[)";
    for (size_t i = 0; i < portfolio.positions.size(); ++i) {
        if (i > 0) json += ",";
        json += positionToJson(portfolio.positions[i]);
    }
    json += "]}";
    return json;
}

std::string OMS::executionReportToJson(const ExecutionReport& report) {
    return std::string("{") +
        R"("order_id":")" + jsonEscape(report.order_id) + "\"" +
        R"(,"exec_id":")" + jsonEscape(report.exec_id) + "\"" +
        R"(,"side":")" + sideToString(report.side) + "\"" +
        R"(,"symbol":")" + jsonEscape(report.symbol) + "\"" +
        R"(,"quantity":)" + std::to_string(report.quantity) +
        R"(,"price":)" + std::to_string(report.price) +
        R"(,"leaves_qty":)" + std::to_string(report.leaves_qty) +
        R"(,"cum_qty":)" + std::to_string(report.cum_qty) +
        R"(,"avg_price":)" + std::to_string(report.avg_price) +
        R"(,"state":")" + stateToString(report.state) + "\"" +
        "}";
}

OrderRequest OMS::parseOrderRequest(const std::string& json) {
    OrderRequest req;

    auto extractString = [&](const std::string& key) -> std::string {
        size_t p = json.find("\"" + key + "\"");
        if (p == std::string::npos) return "";
        size_t start = json.find('"', p + key.length() + 3);
        if (start == std::string::npos) return "";
        size_t end = json.find('"', start + 1);
        if (end == std::string::npos) return "";
        return json.substr(start + 1, end - start - 1);
    };

    auto extractDouble = [&](const std::string& key) -> double {
        size_t p = json.find("\"" + key + "\"");
        if (p == std::string::npos) return 0.0;
        size_t colon = json.find(':', p + key.length() + 2);
        if (colon == std::string::npos) return 0.0;
        size_t start = colon + 1;
        while (start < json.size() && json[start] == ' ') ++start;
        if (start >= json.size()) return 0.0;
        std::string rest = json.substr(start);
        size_t end = rest.find_first_of(",}");
        if (end == std::string::npos) return 0.0;
        try {
            return std::stod(rest.substr(0, end));
        } catch (...) {
            return 0.0;
        }
    };

    req.client_id = extractString("client_id");
    req.symbol = extractString("symbol");
    req.exchange = extractString("exchange");
    req.quantity = extractDouble("quantity");
    req.price = extractDouble("price");
    req.stop_price = extractDouble("stop_price");

    std::string side = extractString("side");
    req.side = (side == "SELL") ? OrderSide::SELL : OrderSide::BUY;

    std::string type = extractString("type");
    if (type == "MARKET") req.type = OrderType::MARKET;
    else if (type == "STOP") req.type = OrderType::STOP;
    else if (type == "STOP_LIMIT") req.type = OrderType::STOP_LIMIT;
    else req.type = OrderType::LIMIT;

    return req;
}

}
