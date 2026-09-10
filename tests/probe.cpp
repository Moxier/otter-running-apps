// Read-only ABI probe against the session's real NIRI_SOCKET. Never sends a focus action.
#include "otter_plugin_abi.h"
#include <chrono>
#include <dlfcn.h>
#include <iostream>
#include <thread>
unsigned count = 0, active = 0;
int item(OtterPluginFrameCtx *, const char *, const OtterPluginRect *, const char *, uint32_t,
         const char *, uint32_t state, uint64_t) {
    ++count;
    active += state == OTTER_PLUGIN_BAR_ITEM_ACTIVE;
    return 0;
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        std::cerr << dlerror() << '\n';
        return 1;
    }
    auto init = reinterpret_cast<decltype(&otter_plugin_init)>(dlsym(lib, "otter_plugin_init"));
    auto attach =
        reinterpret_cast<decltype(&otter_plugin_bar_attach)>(dlsym(lib, "otter_plugin_bar_attach"));
    auto deinit =
        reinterpret_cast<decltype(&otter_plugin_deinit)>(dlsym(lib, "otter_plugin_deinit"));
    OtterPluginHost host{};
    host.abi_version = 1;
    host.sd_bar_item = item;
    if (!init || !attach || !deinit || init(&host))
        return 1;
    auto *instance = attach(&host, "readonly-probe");
    if (!instance)
        return 1;
    OtterPluginSlotConstraints constraints{{0, 0, 288, 32}, {0, 0, 288, 32}};
    for (int i = 0; i < 20; ++i) {
        count = active = 0;
        if (instance->vtable->contribute(instance->user_data, nullptr, &constraints))
            return 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "Rendered cells: " << count << "; active cells: " << active << '\n';
    instance->vtable->destroy(instance->user_data);
    deinit();
    dlclose(lib);
}
