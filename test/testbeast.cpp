#include "../src/beast.h"
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/log.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>

using namespace fcitx;

int main() {
    char arg0[] = "testhallelujah";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=beast";
    char *argv[] = {arg0, arg1, arg2};
    Log::setLogRule("default=5");

    fcitx::BeastFactory beastFactory;
    fcitx::StaticAddonRegistry staticAddons = {
        std::make_pair<std::string, fcitx::AddonFactory *>("beast",
                                                           &beastFactory)};
    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(&staticAddons);
    EventDispatcher dispatcher;
    dispatcher.attach(&instance.eventLoop());

    instance.initialize();
    auto beast = dynamic_cast<Beast *>(instance.addonManager().addon("beast"));
    assert(beast);

    bool getterCalled = false;
    bool setterCalled = false;

    beast->setConfigGetter([&](const char *) {
        getterCalled = true;
        return "";
    });
    beast->setConfigSetter([&](const char *, const char *) {
        setterCalled = true;
        return "";
    });

    // Listen on default port.
    assert(std::system("lsof -i:32489") == 0);

    // Getter works.
    assert(!getterCalled);
    assert(std::system("curl http://localhost:32489/config/addon/beast") == 0);
    assert(getterCalled);

    // Setter works.
    assert(!setterCalled);
    assert(
        std::system(
            "curl -X POST -d '{}' http://localhost:32489/config/addon/beast") ==
        0);
    assert(setterCalled);

    // Port reset works.
    auto config = dynamic_cast<const BeastConfig *>(beast->getConfig());
    RawConfig raw;
    config->dumpDescription(raw);
    BeastConfig cfg;
    cfg.port.setValue(32490);
    cfg.save(raw);
    beast->setConfig(raw);

    assert(std::system("lsof -i:32489") != 0);
    assert(std::system("lsof -i:32490") == 0);

    return 0;
}
