#include "oms.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = boost::asio::ip::tcp;

void oms::start() {
    try {
        asio::io_context io;

        tcp::acceptor acceptor(
            io,
            tcp::endpoint(tcp::v4(), 4444)
        );

        std::cout << "Server listening on port 4444\n";

        while (true) {
            tcp::socket socket(io);

            acceptor.accept(socket);

            std::cout << "New client connected: "
                      << socket.remote_endpoint()
                      << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}
