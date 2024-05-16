#ifndef _FCITX5_MODULES_BEAST_BEAST_H_
#define _FCITX5_MODULES_BEAST_BEAST_H_

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

using ConfigGetter = std::function<std::string(const char *)>;
using ConfigSetter = std::function<void(const char *, const char *)>;

namespace fcitx {

FCITX_CONFIGURATION(BeastConfig, Option<int, IntConstrain> port{
                                     this, "Port", _("Port"), DEFAULT_PORT,
                                     IntConstrain(1024, 65535)};);

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
