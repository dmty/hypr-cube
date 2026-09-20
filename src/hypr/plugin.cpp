#define WLR_USE_UNSTABLE

#include "globals.hpp"
#include "capture.hpp"
#include "pass.hpp"

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/helpers/time/Time.hpp>

#include <chrono>
#include <stdexcept>

namespace hypr {

// Populated in a later task; defaults describe a 4-face cube until then.
cube::Config g_cfg;
float        g_background[4] = {0.f, 0.f, 0.f, 1.f};

// The Lua-config build of Hyprland has no route from hyprctl/hl.dispatch to an
// addDispatcherV2 dispatcher (confirmed by direct probing of the control socket), so
// addLuaFunction is the only way to fire this from hyprctl eval; a keybind reaches the
// dispatcher directly instead. Both paths funnel through this one function.
//
// hyprctl's own "dispatch" text command is evaluated as Lua on this build, so
// Config::Actions::changeWorkspace (the same call the built-in Lua workspace dispatcher
// uses) replaces every HyprlandAPI::invokeHyprctlCommand("dispatch", ...) call below.
static void cubeWorkspace(const std::string& arg) {
    const auto mon = Desktop::focusState()->monitor();
    if (!mon)
        return;

    const int dir  = (arg == "prev") ? -1 : +1;
    const int face = cube::faceOfWorkspace(mon->activeWorkspaceID(), g_cfg.faces);
    if (g_session)
        return; // already animating: ignored, the in-flight rotation completes untouched.

    if (face < 0) {
        // Not on a cube face: plain relative switch.
        reportIfFailed("workspace switch failed",
                       Config::Actions::changeWorkspace(dir > 0 ? "e+1" : "e-1"));
        return;
    }

    startSession(mon, g_cfg, face, /*captureAll=*/false);
    const int dest = cube::normalizeFace(face + dir, g_cfg.faces);
    g_session->faces[dest] = captureWorkspace(mon, cube::workspaceOfFace(dest));

    g_session->state.startRotate(face, dir, nowMs());
}

static void cubeStop() {
    endSession();
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
    hypr::endSession();
}
