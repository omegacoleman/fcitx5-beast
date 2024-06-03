#ifndef _FCITX5_MODULES_BEAST_BEAST_H_
#define _FCITX5_MODULES_BEAST_BEAST_H_

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#define FCITX5_BEAST_HAS_UNIX_SOCKET
#endif

#include <boost/asio.hpp>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/i18n.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
#include <thread>

namespace asio = boost::asio;

// fcitx in numpad
#define DEFAULT_PORT 32489
#define DEFAULT_UNIX_SOCKET_PATH "/tmp/fcitx5.sock"

namespace fcitx {

FCITX_CONFIGURATION(WebServerTcpConfig,
                    Option<int, IntConstrain> port{this, "Port", _("Port"),
                                                   DEFAULT_PORT,
                                                   IntConstrain(1024, 65535)};);

FCITX_CONFIGURATION(WebServerUnixSocketConfig,
                    Option<std::string> path{this, "Path", _("Path"),
                                             DEFAULT_UNIX_SOCKET_PATH};);

FCITX_CONFIG_ENUM(WebServerCommunication,
#ifdef FCITX5_BEAST_HAS_UNIX_SOCKET
                  UnixSocket,
#endif
                  Tcp);

FCITX_CONFIGURATION(WebServerConfig,
                    Option<WebServerCommunication> communication{
                        this, "Communication", _("Communication"),
#ifdef FCITX5_BEAST_HAS_UNIX_SOCKET
                        WebServerCommunication::UnixSocket
#else
                        WebServerCommunication::Tcp
#endif
                    };
                    Option<WebServerTcpConfig> tcp{this, "Tcp", _("Tcp"), {}};
                    Option<WebServerUnixSocketConfig> unix_socket{
                        this, "Unix Socket", _("Unix Socket"), {}};);

class WebServer : public AddonInstance {
public:
    WebServer(Instance *instance);
    ~WebServer();

    Instance *instance() { return instance_; }

    std::string routedGetConfig(const std::string &uri);
    std::string routedSetConfig(const std::string &uri, const char *data,
                                size_t sz);
    std::string routedControllerRequest(const std::string &path);

    const Configuration *getConfig() const override { return &config_; }
    void setConfig(const RawConfig &config) override;
    void reloadConfig() override;

private:
    static const inline std::string ConfPath = "conf/beast.conf";
    void startThread();
    void stopThread();
    void startServer();
    Instance *instance_;
    WebServerConfig config_;
    std::shared_ptr<asio::io_context> ioc;
    std::thread serverThread_;
    fcitx::EventDispatcher dispatcher_;
};

class WebServerFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new WebServer(manager->instance());
    }
};
} // namespace fcitx

#endif // _FCITX5_MODULES_BEAST_BEAST_H_
