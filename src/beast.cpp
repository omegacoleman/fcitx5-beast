#include "beast.h"

#include <boost/asio/generic/stream_protocol.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <fcitx-config/iniparser.h>
#include <fcitx/inputcontextmanager.h>
#include <unistd.h>

#include "config/config-public.h"

#include "nlohmann/json.hpp"

#ifdef FCITX5_BEAST_HAS_UNIX_SOCKET
// For unlink(2)
#include <unistd.h>
#endif

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

nlohmann::json configToJson(const fcitx::Configuration &config);

namespace fcitx {

Beast::Beast(Instance *instance) : instance_(instance) {
    dispatcher_.attach(&instance->eventLoop());
    reloadConfig();
}

Beast::~Beast() { stopThread(); }

std::string Beast::routedGetConfig(const std::string& uri) {
    std::promise<std::string> prom;
    auto fut = prom.get_future();
    dispatcher_.schedule([this, &uri, &prom](){
        try {
            prom.set_value(getInstanceConfig(uri, this->instance_));
        } catch (...) {
            prom.set_exception(std::current_exception());
        }
    });
    return fut.get();
}

std::string Beast::routedSetConfig(const std::string& uri, const char* data, size_t sz) {
    std::promise<bool> prom;
    auto fut = prom.get_future();
    dispatcher_.schedule([this, &uri, &prom, data, sz](){
        try {
            prom.set_value(setInstanceConfig(uri, data, sz, this->instance_));
        } catch (...) {
            prom.set_exception(std::current_exception());
        }
    });
    if (!fut.get()) {
      return nlohmann::json{{"ERROR", "Failed to set config"}}.dump();
    } else {
      return nlohmann::json{{}}.dump();
    }
}

void Beast::setConfig(const RawConfig &config) {
    config_.load(config);
    safeSaveAsIni(config_, ConfPath);
    reloadConfig();
}

void Beast::reloadConfig() {
    dispatcher_.schedule([this](){
        if (this->serverThread_.joinable()) {
            this->stopThread();
        }
        readAsIni(config_, ConfPath);
        this->startThread();
    });
}

template <class Socket>
class http_connection
    : public std::enable_shared_from_this<http_connection<Socket>> {
public:
    http_connection(Socket socket, Beast *addon) : socket_(std::move(socket)), addon_(addon) {}

    // Initiate the asynchronous operations associated with the connection.
    void start() { read_request(); }

private:
    // The socket for the currently connected client.
    Socket socket_;

    // The buffer for performing reads.
    beast::flat_buffer buffer_{8192};

    // The request message.
    http::request<http::string_body> request_;

    // The response message.
    http::response<http::dynamic_body> response_;

    // The addon.
    Beast* addon_;

    // Asynchronously receive a complete request message.
    void read_request() {
        auto self = this->shared_from_this();

        http::async_read(
            socket_, buffer_, request_,
            [self](beast::error_code ec, std::size_t bytes_transferred) {
                boost::ignore_unused(bytes_transferred);
                if (!ec)
                    self->process_request();
            });
    }

    // Determine what needs to be done with the request message.
    void process_request() {
        response_.version(request_.version());
        response_.keep_alive(false);

        switch (request_.method()) {
        case http::verb::get:
        case http::verb::post:
            response_.result(http::status::ok);
            response_.set(http::field::server, "Beast");
            try {
                create_response();
            } catch (const std::exception &e) {
                response_.result(http::status::internal_server_error);
                response_.set(http::field::content_type, "text/plain");
                beast::ostream(response_.body())
                    << "An error occurred: " << e.what();
            }
            break;

        default:
            // We return responses indicating an error if
            // we do not recognize the request method.
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "text/plain");
            beast::ostream(response_.body())
                << "Invalid request-method '"
                << std::string(request_.method_string()) << "'";
            break;
        }

        write_response();
    }

    // Construct a response message based on the program state.
    void create_response() {
        if (stringutils::startsWith(request_.target(), "/config/")) {
            std::string uri = "fcitx:/";
            uri += request_.target();
            if (request_.method() == http::verb::get) {
                response_.result(http::status::ok);
                response_.set(http::field::content_type, "application/json");
                beast::ostream(response_.body()) << addon_->routedGetConfig(uri.c_str());
            } else if (request_.method() == http::verb::post) {
                response_.result(http::status::ok);
                response_.set(http::field::content_type, "application/json");
                beast::ostream(response_.body()) << addon_->routedSetConfig(uri.c_str(), request_.body().data(), request_.body().size());
            }
        } else {
            response_.result(http::status::not_found);
            response_.set(http::field::content_type, "text/plain");
            beast::ostream(response_.body()) << "File not found\r\n";
        }
    }

    // Asynchronously transmit the response message.
    void write_response() {
        auto self = this->shared_from_this();

        response_.content_length(response_.body().size());

        http::async_write(socket_, response_,
                          [self](beast::error_code ec, std::size_t) {
                              self->socket_.shutdown(Socket::shutdown_send, ec);
                          });
        FCITX_INFO() << response_.result_int() << " "
                     << request_.method_string() << " " << request_.target();
    }
};

// "Loop" forever accepting new connections.
template <class Acceptor, class Socket>
void http_server(Acceptor &acceptor, Socket &socket, Beast* addon) {
    acceptor.async_accept(socket, [&acceptor, &socket, addon](beast::error_code ec) {
        if (!ec)
            std::make_shared<http_connection<Socket>>(std::move(socket), addon)->start();
        http_server(acceptor, socket, addon);
    });
}

void Beast::startServer() {
    ioc = std::make_shared<asio::io_context>();

#ifdef FCITX5_BEAST_HAS_UNIX_SOCKET
    if (config_.communication.value() == BeastCommunication::UnixSocket) {
        auto path = config_.unix_socket.value().path.value();
        (void)::unlink(path.c_str());
        auto const ep = asio::local::stream_protocol::endpoint{path};
        asio::local::stream_protocol::acceptor acceptor{*ioc, ep};
        asio::local::stream_protocol::socket socket{*ioc};
        http_server(acceptor, socket, this);
        ioc->run();
    } else {
#endif
        auto const address = asio::ip::make_address("127.0.0.1");
        tcp::acceptor acceptor{
            *ioc, {address, (unsigned short)config_.tcp.value().port.value()}};
        tcp::socket socket{*ioc};
        http_server(acceptor, socket, this);
        ioc->run();
#ifdef FCITX5_BEAST_HAS_UNIX_SOCKET
    }
#endif
}

void Beast::startThread() {
    serverThread_ = std::thread([this] {
        try {
            startServer();
        } catch (const std::exception &e) {
            FCITX_ERROR() << "Error in Beast: " << e.what();
        }
    });
}

void Beast::stopThread() {
    ioc->stop();
    serverThread_.join();
}
} // namespace fcitx

#ifdef FCITX_BEAST_IS_SHARED
FCITX_ADDON_FACTORY(fcitx::BeastFactory)
#endif
