#define WLR_USE_UNSTABLE

#include "globals.hpp"
#include "capture.hpp"
#include "pass.hpp"

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>

#include <stdexcept>

namespace hypr {
FaceTexture g_incoming;
PHLMONITOR  g_monitor = nullptr;

// Shared by both invocation paths below: the Lua-config build of Hyprland has no route
// from hyprctl/hl.dispatch to an addDispatcherV2 dispatcher (confirmed by direct probing
// of the control socket), so addLuaFunction is the only way to actually fire this.
static void cubeWorkspace(const std::string& arg) {
    const auto mon = Desktop::focusState()->monitor();
    if (!mon)
        return;
    const int current = mon->activeWorkspaceID();
    const int target  = arg == "prev" ? current - 1 : current + 1;
    g_incoming        = captureWorkspace(mon, target);
    startFrameLoop(mon);
}

static void cubeStop() {
    stopFrameLoop();
    g_incoming = {};
}
}

// Reading the Lua call's own argument off the stack (lua_isstring/lua_tostring) crashes
// Hyprland's Lua runtime from a plugin-registered function on this build, so each
// direction gets its own zero-argument entry point instead of one taking a string.
// hyprctl (eval/repl) services each connection on its own std::thread, so these must not
// touch renderer state directly either; doLater hops back onto the main loop.
static int luaCubeNext(lua_State*) {
    g_pEventLoopManager->doLater([] { hypr::cubeWorkspace("next"); });
    return 0;
}

static int luaCubePrev(lua_State*) {
    g_pEventLoopManager->doLater([] { hypr::cubeWorkspace("prev"); });
    return 0;
}

static int luaCubeStop(lua_State*) {
    g_pEventLoopManager->doLater([] { hypr::cubeStop(); });
    return 0;
}

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

    HyprlandAPI::addDispatcherV2(PHANDLE, "cube:workspace", [](std::string arg) -> SDispatchResult {
        hypr::cubeWorkspace(arg);
        return SDispatchResult{};
    });

    HyprlandAPI::addDispatcherV2(PHANDLE, "cube:stop", [](std::string) -> SDispatchResult {
        hypr::cubeStop();
        return SDispatchResult{};
    });

    HyprlandAPI::addLuaFunction(PHANDLE, "cube", "next", luaCubeNext);
    HyprlandAPI::addLuaFunction(PHANDLE, "cube", "prev", luaCubePrev);
    HyprlandAPI::addLuaFunction(PHANDLE, "cube", "stop", luaCubeStop);

    HyprlandAPI::addNotification(PHANDLE, "[hypr-cube] loaded",
                                 CHyprColor{0.2, 1.0, 0.2, 1.0}, 3000);

    return {"hypr-cube", "Compiz-style cube workspace switching", "dmitry", "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    hypr::stopFrameLoop();
    hypr::g_incoming = {};
}
