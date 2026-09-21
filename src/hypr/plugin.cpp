#define WLR_USE_UNSTABLE

#include "globals.hpp"
#include "capture.hpp"
#include "pass.hpp"
#include "input.hpp"

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/time/Time.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>

namespace hypr {

cube::Config g_cfg;
float        g_background[4] = {0.f, 0.f, 0.f, 1.f};

namespace {
using namespace Config::Values;

// One spelling per config key, shared by registration and read-back. A key registered
// under one string and read under another silently reads its compiled default forever,
// which is invisible until someone notices the setting does nothing.
constexpr const char* N_FACES      = "plugin:hypr-cube:faces";
constexpr const char* N_DURATION   = "plugin:hypr-cube:duration_ms";
constexpr const char* N_FOV        = "plugin:hypr-cube:fov";
constexpr const char* N_DRAG_ZOOM  = "plugin:hypr-cube:drag_zoom";
constexpr const char* N_DRAG_SENS  = "plugin:hypr-cube:drag_sensitivity";
constexpr const char* N_BACKGROUND = "plugin:hypr-cube:background";

constexpr Config::INTEGER D_FACES = 4, D_DURATION = 300, D_BACKGROUND = 0xFF000000;
constexpr Config::FLOAT   D_FOV = 45.f, D_DRAG_ZOOM = 0.6f, D_DRAG_SENS = 1.f;

void registerOne(SP<IValue> value) {
    const auto res = Config::mgr()->registerPluginValue(PHANDLE, value);
    if (!res)
        HyprlandAPI::addNotification(PHANDLE,
            "[hypr-cube] failed to register " + std::string(value->name()) + ": " + res.error(),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
}

// registerPluginValue (Lua config manager) converts the IValue we hand it into an
// independent Config::Lua::ILuaConfigValue that holds the live, hot-reloadable data;
// our own IValue is discarded once the call returns, and its own value() always reads
// back its compiled default (verified on-device: a value read via the handle stayed at
// its default while `hyprctl repl "print(hl.get_config(...))"` showed the live edited
// number). So values are read back the same generic way Hyprland itself reads plugin
// values, via getConfigValue()'s type-erased pointer, not via the registration handle.
template <typename T>
T readValue(const char* name, T fallback) {
    const auto reply = Config::mgr()->getConfigValue(name);
    if (!reply.dataptr)
        return fallback;
    return *reinterpret_cast<const T*>(*reply.dataptr);
}

}

void registerConfig() {
    registerOne(makeShared<CIntValue>(N_FACES, "faces", D_FACES));
    registerOne(makeShared<CIntValue>(N_DURATION, "duration_ms", D_DURATION));
    registerOne(makeShared<CFloatValue>(N_FOV, "fov", D_FOV));
    registerOne(makeShared<CFloatValue>(N_DRAG_ZOOM, "drag_zoom", D_DRAG_ZOOM));
    registerOne(makeShared<CFloatValue>(N_DRAG_SENS, "drag_sensitivity", D_DRAG_SENS));
    registerOne(makeShared<CColorValue>(N_BACKGROUND, "background", D_BACKGROUND));
}

// Reads the registered values, validates/clamps them (cube::validate), and reports any
// clamp with a visible notification. g_cfg is only ever consulted at the start of a
// session (startSession copies it by value into CubeState, and the session's own
// geometry/background are computed/snapshotted once at that point too), so replacing it
// here while a session is mid-animation cannot corrupt an in-flight frame.
void loadConfig() {
    cube::Config raw;
    raw.faces           = (int)readValue<Config::INTEGER>(N_FACES, D_FACES);
    raw.durationMs      = (int)readValue<Config::INTEGER>(N_DURATION, D_DURATION);
    raw.fovDeg          = readValue<Config::FLOAT>(N_FOV, D_FOV);
    raw.dragZoom        = readValue<Config::FLOAT>(N_DRAG_ZOOM, D_DRAG_ZOOM);
    raw.dragSensitivity = readValue<Config::FLOAT>(N_DRAG_SENS, D_DRAG_SENS);

    const cube::Clamped v = cube::validate(raw);
    g_cfg                 = v.config;

    if (v.facesClamped)
        HyprlandAPI::addNotification(PHANDLE,
            "[hypr-cube] faces clamped to " + std::to_string(g_cfg.faces) + " (legal range 3-16)",
            CHyprColor{1.0, 0.8, 0.2, 1.0}, 5000);
    if (v.fovClamped)
        HyprlandAPI::addNotification(PHANDLE,
            "[hypr-cube] fov clamped to " + std::to_string((int)g_cfg.fovDeg) + " (legal range 20-120)",
            CHyprColor{1.0, 0.8, 0.2, 1.0}, 5000);
    if (v.dragZoomClamped)
        HyprlandAPI::addNotification(PHANDLE,
            "[hypr-cube] drag_zoom clamped (legal range 0.2-1.0)",
            CHyprColor{1.0, 0.8, 0.2, 1.0}, 5000);

    // CColorValue stores Hyprland's AR32 int (see CHyprColor::getAsHex()): bits 24-31
    // alpha, 16-23 red, 8-15 green, 0-7 blue.
    const uint32_t bg = (uint32_t)readValue<Config::INTEGER>(N_BACKGROUND, D_BACKGROUND);
    g_background[0]   = ((bg >> 16) & 0xFF) / 255.f;
    g_background[1]   = ((bg >>  8) & 0xFF) / 255.f;
    g_background[2]   = ( bg        & 0xFF) / 255.f;
    g_background[3]   = ((bg >> 24) & 0xFF) / 255.f;
}

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
    // A live drag's listeners must go with the session: without this, cube:stop mid-drag
    // leaves them registered with g_session already null, and (pre-fix-round-3) they swallowed
    // every subsequent keyboard/mouse event forever, with no Escape to recover since Escape's
    // own handling also sat behind a null-session check. See input.cpp for the other half.
    endDragGrab();
    endSession();
}

static void cubeDrag() {
    const auto mon = Desktop::focusState()->monitor();
    if (!mon || g_session)
        return; // already animating: ignored, same as cubeWorkspace.

    const int face = cube::faceOfWorkspace(mon->activeWorkspaceID(), g_cfg.faces);
    if (face < 0)
        return; // not on a cube face: nothing to spin

    startSession(mon, g_cfg, face, /*captureAll=*/true);
    g_session->state.startDrag(face, nowMs());
    beginDragGrab();
}
}

// Reading the Lua call's own argument off the stack (lua_isstring/lua_tostring) crashes
// Hyprland's Lua runtime from a plugin-registered function on this build, so each
// direction gets its own zero-argument entry point instead of one taking a string.
// hyprctl (eval/repl) services each connection on its own std::thread, so these must not
// touch renderer state directly either; doLater hops back onto the main loop.
//
// A plain doLater() cannot be cancelled: if the plugin is unloaded between the hop being
// queued and the main loop running it, the callback fires into memory that dlclose() has
// already freed. doLaterLock()'s handle undoes the queueing on destruction, so holding one
// per in-flight hop and dropping them all in PLUGIN_EXIT guarantees nothing queued here
// outlives the plugin.
//
// A single reused handle would cancel an earlier still-pending hop if two Lua entry points
// fired in the same event-loop turn, silently dropping whichever call was queued first. A
// vector holds one lock per hop instead. The locks don't report when their callback has
// already run, so entries are pruned by a size cap rather than by checking completion: by
// the time this many have queued without the loop turning over, the oldest are certainly done.
static std::vector<UP<SEventLoopDoLaterLock>> g_pendingLua;

// Lives for the plugin's lifetime; reset in PLUGIN_EXIT so it cannot fire loadConfig()
// into unloaded code.
static CHyprSignalListener g_configListener;

// `removed` (disconnected/disabled) and `destroyMon` (hard removed) are separate signals;
// a session holding that monitor wants to end either way, so both are guarded the same.
static CHyprSignalListener g_monitorRemovedListener;
static CHyprSignalListener g_monitorDestroyedListener;

static void endSessionIfOwnsMonitor(PHLMONITOR mon) {
    if (hypr::g_session && hypr::g_session->monitor == mon) {
        hypr::endDragGrab();
        hypr::endSession();
    }
}

static void queueLuaHop(std::function<void()> fn) {
    if (g_pendingLua.size() >= 8)
        g_pendingLua.erase(g_pendingLua.begin());
    g_pendingLua.push_back(g_pEventLoopManager->doLaterLock(std::move(fn)));
}

static int luaCubeNext(lua_State*) {
    queueLuaHop([] { hypr::cubeWorkspace("next"); });
    return 0;
}

static int luaCubePrev(lua_State*) {
    queueLuaHop([] { hypr::cubeWorkspace("prev"); });
    return 0;
}

static int luaCubeStop(lua_State*) {
    queueLuaHop([] { hypr::cubeStop(); });
    return 0;
}

static int luaCubeDrag(lua_State*) {
    queueLuaHop([] { hypr::cubeDrag(); });
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

    hypr::registerConfig();

    HyprlandAPI::addDispatcherV2(PHANDLE, "cube:workspace", [](std::string arg) -> SDispatchResult {
        hypr::cubeWorkspace(arg);
        return SDispatchResult{};
    });

    HyprlandAPI::addDispatcherV2(PHANDLE, "cube:stop", [](std::string) -> SDispatchResult {
        hypr::cubeStop();
        return SDispatchResult{};
    });

    // Fired by a real keybind (hl.bind with drag = true), which already runs on the main
    // loop, so this runs cubeDrag() directly rather than hopping through doLater.
    HyprlandAPI::addDispatcherV2(PHANDLE, "cube:drag", [](std::string) -> SDispatchResult {
        hypr::cubeDrag();
        return SDispatchResult{};
    });

    HyprlandAPI::addLuaFunction(PHANDLE, "cube", "next", luaCubeNext);
    HyprlandAPI::addLuaFunction(PHANDLE, "cube", "prev", luaCubePrev);
    HyprlandAPI::addLuaFunction(PHANDLE, "cube", "stop", luaCubeStop);
    HyprlandAPI::addLuaFunction(PHANDLE, "cube", "drag", luaCubeDrag);

    g_configListener = Event::bus()->m_events.config.reloaded.listen([] { hypr::loadConfig(); });
    hypr::loadConfig();

    g_monitorRemovedListener   = Event::bus()->m_events.monitor.removed.listen(endSessionIfOwnsMonitor);
    g_monitorDestroyedListener = Event::bus()->m_events.monitor.destroyMon.listen(endSessionIfOwnsMonitor);

    HyprlandAPI::addNotification(PHANDLE, "[hypr-cube] loaded",
                                 CHyprColor{0.2, 1.0, 0.2, 1.0}, 3000);

    return {"hypr-cube", "Compiz-style cube workspace switching", "dmitry", "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // Drop any doLater hop queued by a Lua entry point before it can fire into freed code,
    // and unregister the drag grab's live listeners so an unload mid-drag can't leave
    // Hyprland holding callbacks into memory this .so is about to lose.
    g_pendingLua.clear();
    g_configListener.reset();
    g_monitorRemovedListener.reset();
    g_monitorDestroyedListener.reset();
    hypr::endDragGrab();
    hypr::endSession();
}
