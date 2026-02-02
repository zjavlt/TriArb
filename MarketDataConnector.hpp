#pragma once

#include "Common.hpp"
#include "SymbolMap.hpp"
#include "RingBuffer.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <memory>
#include <string>
#include <iostream>


namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

struct Trade {
    char symbol[16];    // s
    double price;               // p
    double quantity;            // q
    int64_t trade_time;         // t
    int64_t event_time;         // e
};


class MarketDataConnector : public std::enable_shared_from_this<MarketDataConnector>{
protected:
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws_;
    tcp::resolver resolver_;
    beast::flat_buffer buffer_;
    std::shared_ptr<RingBuffer<TickerUpdate>> queue_;
    const SymbolMap& symbol_map_;
    std::string host_;

public:
    MarketDataConnector(net::io_context& ioc, ssl::context& ctx, std::shared_ptr<RingBuffer<TickerUpdate>> queue, const SymbolMap& sm)
        : ws_(net::make_strand(ioc), ctx), resolver_(ioc), queue_(queue), symbol_map_(sm) {}

    virtual ~MarketDataConnector() = default;

    void run(const std::string& host, const std::string& port, const std::string& target) {
        host_ = host;
        resolver_.async_resolve(host, port,
            beast::bind_front_handler(&MarketDataConnector::on_resolve, shared_from_this(), target));
    }
protected:
    virtual void on_session_started() {}

    virtual void process_message(std::string_view data) = 0;

private:
    
    void on_resolve(std::string target, beast::error_code ec, tcp::resolver::results_type results) {
        if (ec) { std::cerr << "Resolve Failed: " << ec.message() << std::endl; return; }

        beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));
        beast::get_lowest_layer(ws_).async_connect(results,
            beast::bind_front_handler(&MarketDataConnector::on_connect, shared_from_this(), target));
    }

    void on_connect(std::string target, beast::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
        if(ec) { std::cerr << "Connect Failed: " << ec.message() << std::endl; return; }

        beast::get_lowest_layer(ws_).expires_never();

        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws_.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
            req.set(http::field::user_agent, "HFT_Bot_v1");
        }));

        if (! SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), host_.c_str())) {
            std::cerr << "Failed to set SNI Hostname" << std::endl;
            return;
        }

        ws_.next_layer().async_handshake(ssl::stream_base::client,
            beast::bind_front_handler(&MarketDataConnector::on_ssl_handshake, shared_from_this(), target));
    }

    void on_ssl_handshake(std::string target, beast::error_code ec) {
        if(ec) { std::cerr << "SSL Handshake Failed: " << ec.message() << std::endl; return; }

        ws_.async_handshake(host_, target,
            beast::bind_front_handler(&MarketDataConnector::on_handshake, shared_from_this()));
    }

    void on_handshake(beast::error_code ec) {
        if(ec) { std::cerr << "Handshake Failed: " << ec.message() << std::endl; return; }

        std::cout << "[Connected] " << host_ << std::endl;

        // Trigger the hook for children to subscribe if needed
        on_session_started();

        // Start reading loop
        do_read();
    }

    void do_read() {
        ws_.async_read(buffer_,
            beast::bind_front_handler(&MarketDataConnector::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        if(ec) { std::cerr << "Read Error: " << ec.message() << std::endl; return; }

        // Convert buffer to string_view and pass to child class for parsing
        auto data = beast::buffers_to_string(buffer_.data());
        process_message(data);

        buffer_.consume(buffer_.size());
        do_read(); // Loop
    }
};