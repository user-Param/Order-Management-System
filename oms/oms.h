#pragma once

#include "../include/models.h"
#include "../include/logger.h"
#include "../riskmanager/riskmanager.h"
#include "../oadapter/oadapter.h"
#include "../database/database.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <thread>
#include <atomic>
#include <queue>
#include <sstream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
using tcp = boost::asio::ip::tcp;

namespace oms {

class OMS {
public:
    OMS();
    ~OMS();

    void start();
    void stop();
    bool isRunning() const;

private:
    struct WebSocketSession {
        websocket::stream<beast::tcp_stream> ws;
        ClientId client_id;
        bool authenticated = false;
        bool closed = false;

        WebSocketSession(beast::tcp_stream&& stream)
            : ws(std::move(stream)) {}
    };

    void handleConnection(beast::tcp_stream stream);
    void handleHttpRequest(beast::tcp_stream& stream, http::request<http::string_body>& req);
    void handleWebSocketUpgrade(beast::tcp_stream stream, http::request<http::string_body>& req);
    void handleWebSocketSession(std::shared_ptr<WebSocketSession> session);

    void handleWebSocketMessage(std::shared_ptr<WebSocketSession> session, const std::string& message);
    void handleAuth(std::shared_ptr<WebSocketSession> session, const std::string& payload);
    void handlePlaceOrder(std::shared_ptr<WebSocketSession> session, const std::string& payload);
    void handleCancelOrder(std::shared_ptr<WebSocketSession> session, const std::string& payload);
    void handleModifyOrder(std::shared_ptr<WebSocketSession> session, const std::string& payload);
    void handlePing(std::shared_ptr<WebSocketSession> session);

    void sendWebSocketMessage(std::shared_ptr<WebSocketSession> session, const std::string& type, const std::string& data);
    void broadcastExecutionReport(const ExecutionReport& report);

    OrderResponse processOrder(const OrderRequest& request, ExecutionReport* out_report = nullptr);
    OrderId generateOrderId();
    void publishExecutionReport(const ExecutionReport& report);

    void buildResponse(http::response<http::string_body>& res, http::status status, const std::string& body);
    void sendHttpResponse(beast::tcp_stream& stream, http::response<http::string_body>& res);

    std::string jsonEscape(const std::string& raw);
    std::string jsonError(const std::string& message);
    std::string jsonSuccess(const std::string& data);
    std::string orderToJson(const Order& order);
    std::string positionToJson(const Position& pos);
    std::string portfolioToJson(const Portfolio& portfolio);
    std::string executionReportToJson(const ExecutionReport& report);
    OrderRequest parseOrderRequest(const std::string& json);

    std::unique_ptr<Database> database_;
    std::unique_ptr<RiskManager> risk_manager_;
    std::unique_ptr<OrderAdapter> adapter_;

    std::atomic<uint64_t> order_counter_{0};
    mutable std::mutex oms_mutex_;

    std::vector<std::shared_ptr<WebSocketSession>> sessions_;
    std::mutex sessions_mutex_;

    std::atomic<bool> running_{false};
    unsigned short port_ = 4444;
};

}
