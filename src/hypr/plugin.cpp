#define WLR_USE_UNSTABLE

#include "globals.hpp"

#include <stdexcept>

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    // Both hashes are const char*, so they must be compared as strings; comparing them
    // directly compares pointers and never matches, which refuses every load.
    const std::string self = __hyprland_api_get_client_hash();
    if (__hyprland_api_get_hash() != self) {
        HyprlandAPI::addNotification(PHANDLE,
            "[hypr-cube] built against a different Hyprland commit; refusing to load",
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hypr-cube] version mismatch");
    }

    HyprlandAPI::addNotification(PHANDLE, "[hypr-cube] loaded",
                                 CHyprColor{0.2, 1.0, 0.2, 1.0}, 3000);

    return {"hypr-cube", "Compiz-style cube workspace switching", "dmitry", "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
}
