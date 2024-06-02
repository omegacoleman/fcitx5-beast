#ifndef _FCITX5_MODULES_BEAST_BEAST_H_
#define _FCITX5_MODULES_BEAST_BEAST_H_

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#define FCITX5_BEAST_HAS_UNIX_SOCKET
#endif

#include <boost/asio.hpp>
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

using ConfigGetter = std::function<std::string(const char *)>;
using ConfigSetter = std::function<void(const char *, const char *)>;

namespace fcitx {

FCITX_CONFIGURATION(BeastTcpConfig, Option<int, IntConstrain> port{
                                        this, "Port", _("Port"), DEFAULT_PORT,
                                        IntConstrain(1024, 65535)};);

FCITX_CONFIGURATION(BeastUnixSocketConfig,
                    Option<std::string> path{this, "Path", _("Path"),
                                             DEFAULT_UNIX_SOCKET_PATH};);

enum class BeastCommunication { UnixSocket, TCP };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(BeastCommunication,
#ifdef FCITX5_BEAST_HAS_UNIX_SOCKET
                                 N_("Unix Socket"),
#endif
                                 N_("TCP"));

FCITX_CONFIGURATION(BeastConfig,
                    Option<BeastCommunication> communication{
                        this, "Communication", _("Communication"),
#ifdef FCITX5_BEAST_HAS_UNIX_SOCKET
                        BeastCommunication::UnixSocket
#else
                        BeastCommunication::TCP
#endif
                    };
#ifdef FCITX5_BEAST_HAS_UNIX_SOCKET
                    Option<BeastUnixSocketConfig> unix_socket{this,
                                                              "Unix Socket",
                                                              _("Unix Socket"),
                                                              {}};
#endif
                    Option<BeastTcpConfig> tcp{this, "TCP", _("TCP"), {}};);

extern ConfigGetter configGetter_;
extern ConfigSetter configSetter_;

class Beast : public AddonInstance {
public:
    Beast(Instance *instance);
    ~Beast();

    Instance *instance() { return instance_; }

    const Configuration *getConfig() const override { return &config_; }
    void setConfig(const RawConfig &config) override;

    void reloadConfig() override;
    void setConfigGetter(ConfigGetter getter) { configGetter_ = getter; }
    void setConfigSetter(ConfigSetter setter) { configSetter_ = setter; }

private:
    static const inline std::string ConfPath = "conf/beast.conf";
    void startThread();
    void stopThread();
    void startServer();
    Instance *instance_;
    BeastConfig config_;
    std::shared_ptr<asio::io_context> ioc;
    std::thread serverThread_;
};

class BeastFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new Beast(manager->instance());
    }
};
} // namespace fcitx

#endif // _FCITX5_MODULES_BEAST_BEAST_H_
