#include "beast.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <fcitx-config/iniparser.h>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

namespace fcitx {

ConfigSetter configSetter_ = [](const char *, const char *) {};
ConfigGetter configGetter_ = [](const char *) { return ""; };

Beast::Beast(Instance *instance) : instance_(instance) { reloadConfig(); }

Beast::~Beast() { stopThread(); }

void Beast::setConfig(const RawConfig &config) {
    config_.load(config);
    safeSaveAsIni(config_, ConfPath);
    reloadConfig();
}

void Beast::reloadConfig() {
    if (serverThread_.joinable()) {
        stopThread();
    }
    readAsIni(config_, ConfPath);
    startThread();
}

class http_connection : public std::enable_shared_from_this<http_connection> {
public:
    http_connection(tcp::socket socket) : socket_(std::move(socket)) {}

    // Initiate the asynchronous operations associated with the connection.
    void start() { read_request(); }

private:
    // The socket for the currently connected client.
    tcp::socket socket_;

    // The buffer for performing reads.
    beast::flat_buffer buffer_{8192};

    // The request message.
    http::request<http::string_body> request_;

    // The response message.
    http::response<http::dynamic_body> response_;

    // Asynchronously receive a complete request message.
    void read_request() {
        auto self = shared_from_this();

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
                beast::ostream(response_.body()) << configGetter_(uri.c_str());
            } else if (request_.method() == http::verb::post) {
                response_.result(http::status::ok);
                response_.set(http::field::content_type, "text/plain");
                beast::ostream(response_.body()) << "";
                configSetter_(uri.c_str(), request_.body().data());
            }
        } else {
            response_.result(http::status::not_found);
            response_.set(http::field::content_type, "text/plain");
            beast::ostream(response_.body()) << "File not found\r\n";
        }
    }

    // Asynchronously transmit the response message.
    void write_response() {
        auto self = shared_from_this();

        response_.content_length(response_.body().size());

        http::async_write(
            socket_, response_, [self](beast::error_code ec, std::size_t) {
                self->socket_.shutdown(tcp::socket::shutdown_send, ec);
            });
        FCITX_INFO() << response_.result_int() << " "
                     << request_.method_string() << " " << request_.target();
    }
};

// "Loop" forever accepting new connections.
void http_server(tcp::acceptor &acceptor, tcp::socket &socket) {
    acceptor.async_accept(socket, [&](beast::error_code ec) {
        if (!ec)
            std::make_shared<http_connection>(std::move(socket))->start();
        http_server(acceptor, socket);
    });
}

void Beast::startServer() {
    auto const address = asio::ip::make_address("127.0.0.1");

    ioc = std::make_shared<asio::io_context>();

    // Create and bind the endpoint
    asio::ip::tcp::acceptor acceptor{
        *ioc, {address, (unsigned short)config_.port.value()}};
    asio::ip::tcp::socket socket{*ioc};
    http_server(acceptor, socket);
    ioc->run();
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
