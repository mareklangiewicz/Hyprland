// Binds every wl_output as soon as it is advertised and releases it on global_remove, like a toolkit does.
// Prints "started" once connected. On a fatal protocol error it reports it and exits, closing stdout.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <print>

#include <wayland-client.h>

static std::map<uint32_t, wl_output*> outputs;

static void                           onGlobal(void*, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    if (std::strcmp(interface, wl_output_interface.name) != 0)
        return;

    outputs[name] = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 3U)));
}

static void onGlobalRemove(void*, wl_registry*, uint32_t name) {
    const auto IT = outputs.find(name);
    if (IT == outputs.end())
        return;

    if (wl_output_get_version(IT->second) >= WL_OUTPUT_RELEASE_SINCE_VERSION)
        wl_output_release(IT->second);
    else
        wl_output_destroy(IT->second);

    outputs.erase(IT);
}

static const wl_registry_listener REGISTRY_LISTENER = {
    .global        = onGlobal,
    .global_remove = onGlobalRemove,
};

int main() {
    wl_display* display = wl_display_connect(nullptr);
    if (!display) {
        std::println("failed to connect");
        return 2;
    }

    wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &REGISTRY_LISTENER, nullptr);
    wl_display_roundtrip(display);

    std::println("started");
    std::fflush(stdout);

    while (wl_display_dispatch(display) != -1) {
        // the test kills us, there is nothing to do but serve events
    }

    std::println("disconnected: {}", std::strerror(wl_display_get_error(display)));
    return 1;
}
