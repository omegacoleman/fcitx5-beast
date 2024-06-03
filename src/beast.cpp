#include "beast.h"

#include <boost/asio/generic/stream_protocol.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <condition_variable>
#include <fcitx-config/iniparser.h>
#include <fcitx/event.h>
#include <fcitx/inputcontextmanager.h>
#include <queue>
#include <unistd.h>

#include "config/config-public.h"
#include "controller/router.h"
#include "subscribe/ev_map.h"
#include "subscribe/serializing.hpp"

#include "nlohmann/json.hpp"

#ifdef FCITX5_BEAST_HAS_UNIX_SOCKET
// For unlink(2)
#include <unistd.h>
#endif

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

namespace fcitx {

Beast::Beast(Instance *instance) : instance_(instance) {
    dispatcher_.attach(&instance->eventLoop());
    reloadConfig();
}

Beast::~Beast() { stopThread(); }

std::string Beast::routedGetConfig(const std::string &uri) {
    std::promise<std::string> prom;
    auto fut = prom.get_future();
    dispatcher_.schedule([this, &uri, &prom]() {
        try {
            prom.set_value(getInstanceConfig(uri, this->instance_));
        } catch (...) {
            prom.set_exception(std::current_exception());
        }
    });
    return fut.get();
}

std::string Beast::routedSetConfig(const std::string &uri, const char *data,
                                   size_t sz) {
    std::promise<bool> prom;
    auto fut = prom.get_future();
    dispatcher_.schedule([this, &uri, &prom, data, sz]() {
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

std::string Beast::routedControllerRequest(const std::string &path) {
    std::promise<std::string> prom;
    auto fut = prom.get_future();
    dispatcher_.schedule([this, &path, &prom]() {
        try {
            prom.set_value(
                handle_controller_request(path, this->instance_).dump());
        } catch (...) {
            prom.set_exception(std::current_exception());
        }
    });
    return fut.get();
}

void Beast::setConfig(const RawConfig &config) {
    config_.load(config);
    safeSaveAsIni(config_, ConfPath);
    reloadConfig();
}

void Beast::reloadConfig() {
    dispatcher_.schedule([this]() {
        if (this->serverThread_.joinable()) {
            this->stopThread();
        }
        readAsIni(config_, ConfPath);
        this->startThread();
    });
}

template <class Stream>
class ws_subscription
    : public std::enable_shared_from_this<ws_subscription<Stream>> {
public:
    ws_subscription(Stream stream, Beast *addon)
        : stream_(std::move(stream)), addon_(addon) {}

    void watch(const std::string &evname) {
        FCITX_INFO() << "subscribe: watching " << evname;
        auto ev = convert_ev_name(evname);
        if (static_cast<int>(ev) == 0) {
            FCITX_WARN() << "unknown event to subscribe: " << evname;
            return;
        }
        eventWatchers_.emplace_back(addon_->instance()->watchEvent(
            ev, EventWatcherPhase::PostInputMethod,
            [this, ev, evname](Event &event) {
                this->post(evname,
                           extract_params(this->addon_->instance(), ev, event));
            }));
    }

    void watch_all() {
        for (const auto &[k, v] : ev_map()) {
            watch(k);
        }
    }

    void start() { do_accept(); }

    template <class Request>
    void start(const Request &upgrade) {
        do_accept(upgrade);
    }

private:
    template <class Request>
    void do_accept(const Request &upgrade) {
        auto uptr = std::make_shared<const Request>(upgrade);
        stream_.async_accept(*uptr, [this, uptr, sg = this->shared_from_this()](
                                        boost::system::error_code ec) {
            (void)sg;
            (void)uptr;
            this->accept_done(ec);
        });
    }

    void do_accept() {
        stream_.async_accept([this, sg = this->shared_from_this()](
                                 boost::system::error_code ec) {
            (void)sg;
            this->accept_done(ec);
        });
    }

    void accept_done(boost::system::error_code ec) {
        if (ec) {
            FCITX_ERROR() << "ws send: " << ec.message();
            return;
        }
        do_recv();
    }

    static fcitx::EventType convert_ev_name(const std::string &name) {
        auto it = ev_map().find(name);
        if (it == ev_map().end())
            return static_cast<fcitx::EventType>(0);
        return it->second;
    }

    void post(const std::string &ev,
              const std::unordered_map<std::string, std::string> &params) {
        std::string msg = to_json_str(ev, params);
        std::unique_lock lg{mut_};
        msgs_.emplace_back(msg);

        if (!sending_) {
            asio::post(
                stream_.get_executor(),
                [this, sg = this->shared_from_this()]() { this->do_send(); });
        }
    }

    void do_send() {
        std::unique_lock lg{mut_};
        if (sending_)
            return;
        if (msgs_.size() && stream_.is_open()) {
            msg_ = msgs_.front();
            msgs_.pop_front();
            sending_ = true;
        } else {
            return;
        }
        stream_.async_write(asio::buffer(msg_),
                            [this, sg = this->shared_from_this()](
                                boost::system::error_code ec, size_t sz) {
                                this->send_done(ec, sz);
                            });
    }

    void send_done(boost::system::error_code ec, size_t) {
        msg_.clear();
        if (ec) {
            FCITX_ERROR() << "ws send: " << ec.message();
        }
        sending_ = false;
        do_send();
    }

    void do_recv() {
        stream_.async_read(buffer_,
                           [this, sg = this->shared_from_this()](
                               boost::system::error_code ec, size_t sz) {
                               this->recv_done(ec, sz);
                           });
    }

    void recv_done(boost::system::error_code ec, size_t sz) {
        if (ec == websocket::error::closed)
            return;
        if (ec) {
            FCITX_ERROR() << "ws recv: " << ec.message();
            return;
        }
        buffer_.consume(sz);
        do_recv();
    }

    std::mutex mut_;
    std::list<std::string> msgs_;
    bool sending_ = false;

    std::string msg_;
    beast::flat_buffer buffer_{8192};

    std::vector<std::unique_ptr<HandlerTableEntry<EventHandler>>>
        eventWatchers_;
    Stream stream_;
    Beast *addon_;
};

template <class Socket>
class http_connection
    : public std::enable_shared_from_this<http_connection<Socket>> {
public:
    http_connection(Socket socket, Beast *addon)
        : socket_(std::move(socket)), addon_(addon) {}

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
    Beast *addon_;

    void handle_subscribe(bool upgrade = true) && {
        auto ws = std::make_shared<ws_subscription<websocket::stream<Socket>>>(
            websocket::stream<Socket>{std::move(socket_)}, addon_);
        if (upgrade) {
            if (request_.target().starts_with("/subscribe/")) {
                std::string_view sv{request_.target()};
                sv.remove_prefix(11);
                auto it = sv.begin();
                auto beg = it;
                while (it != sv.end()) {
                    if (*it == '+') {
                        if (it != beg) {
                            std::string part{beg, it};
                            ws->watch(part);
                        }
                        it++;
                        beg = it;
                    } else {
                        it++;
                    }
                }
                if (beg != sv.end()) {
                    std::string part{beg, sv.end()};
                    ws->watch(part);
                }
            } else {
                ws->watch_all();
            }
            ws->start(request_);
        } else {
            ws->start();
        }
    }

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

        if (request_.target().starts_with("/subscribe") &&
            websocket::is_upgrade(request_)) {
            std::move(*this).handle_subscribe();
            return;
        }

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
                beast::ostream(response_.body())
                    << addon_->routedGetConfig(uri.c_str());
            } else if (request_.method() == http::verb::post) {
                response_.result(http::status::ok);
                response_.set(http::field::content_type, "application/json");
                beast::ostream(response_.body()) << addon_->routedSetConfig(
                    uri.c_str(), request_.body().data(),
                    request_.body().size());
            }
        } else if (request_.target().starts_with("/controller/")) {
            std::string s = request_.target().substr(12);
            response_.result(http::status::ok);
            response_.set(http::field::content_type, "application/json");
            beast::ostream(response_.body())
                << addon_->routedControllerRequest(s);
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
void http_server(Acceptor &acceptor, Socket &socket, Beast *addon) {
    acceptor.async_accept(socket, [&acceptor, &socket,
                                   addon](beast::error_code ec) {
        if (!ec)
            std::make_shared<http_connection<Socket>>(std::move(socket), addon)
                ->start();
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
    if (this->serverThread_.joinable()) {
        ioc->stop();
        serverThread_.join();
    }
}
} // namespace fcitx

#ifdef FCITX_BEAST_IS_SHARED
FCITX_ADDON_FACTORY(fcitx::BeastFactory)
#endif
