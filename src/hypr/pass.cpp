#include "pass.hpp"
#include "globals.hpp"
#include "../gl/renderer.hpp"
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <chrono>
#include <cmath>

namespace hypr {

std::optional<CubeSession> g_session;

static CHyprSignalListener g_stageListener;
static gl::CubeRenderer    g_renderer;
static bool                g_rendererReady = false;
// A compile/link failure is deterministic for fixed shader source: retrying init() every
// frame would leak nothing (init() now cleans up after itself) but would notify forever.
// Try exactly once per plugin load.
static bool                g_rendererFailed = false;

// Set from draw() when the animation finishes. The commit itself happens in
// flushPendingCommit() on the next render.stage callback, never from inside draw();
// teardown happens one callback later still, via g_pendingTeardown below.
static bool g_pendingEnd  = false;
static int  g_pendingFace = -1;
// Set once the commit lands. By RENDER_LAST_MOMENT the frame's window content is already
// rendered against whatever workspace was active a moment ago, so ending the session on the
// same frame the workspace changes would show that stale content for one frame. Keep the
// cube (pixel-identical to the stock resting frame, so drawing it once more is invisible)
// for that one frame and tear down on the next stage callback instead.
static bool g_pendingTeardown = false;

double nowMs() {
    return std::chrono::duration<double, std::milli>(Time::steadyNow().time_since_epoch()).count();
}

void reportIfFailed(const char* what, const Config::Actions::ActionResult& res) {
    if (res)
        return;
    HyprlandAPI::addNotification(PHANDLE, std::string("[hypr-cube] ") + what + ": " + res.error().message,
                                 CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
}

static void flushPendingCommit() {
    if (!g_pendingEnd)
        return;
    const int face = g_pendingFace;
    g_pendingEnd    = false;
    g_pendingFace   = -1;

    if (face >= 0) {
        // The session's own monitor is authoritative, not whatever is focused right now: the
        // keybind path installs no input grab, so focus-follows-mouse (or a window mapping
        // elsewhere) can move focus to a different monitor during the 300ms rotation.
        // Config::Actions::changeWorkspace() resolves relative to the focused monitor
        // internally, so committing while focus has drifted would yank the target workspace
        // onto the wrong monitor (spec S9). If focus no longer matches, skip the switch
        // rather than risk that, but still report it and still tear the session down below.
        const auto mon = g_session->monitor;
        if (Desktop::focusState()->monitor() != mon) {
            reportIfFailed("workspace switch failed",
                           Config::Actions::actionError(
                               "focus moved off the cube's monitor mid-rotation; workspace switch skipped",
                               Config::Actions::eActionErrorLevel::WARNING, Config::Actions::eActionErrorCode::INVALID_STATE));
        } else {
            const auto oldWs = mon->m_activeWorkspace;

            const auto res = Config::Actions::changeWorkspace(std::to_string(cube::workspaceOfFace(face)));
            reportIfFailed("workspace switch failed", res);

            // changeWorkspace() just started Hyprland's own slide/fade on both workspaces
            // (CMonitor::changeWorkspace -> Animation::Workspace::startAnimation); the cube already
            // played that transition, so finish it on the spot rather than let it play again.
            if (res) {
                if (oldWs) {
                    oldWs->m_renderOffset->warp();
                    oldWs->m_alpha->warp();
                }
                if (const auto newWs = mon->m_activeWorkspace) {
                    newWs->m_renderOffset->warp();
                    newWs->m_alpha->warp();
                }
            }
        }
    }
    // Stay alive through this frame; the following stage callback tears down for real.
    g_pendingTeardown = true;
}

std::vector<UP<IPassElement>> CubePassElement::draw() {
    if (!g_session)
        return {};
    auto& s = *g_session;

    if (!g_rendererReady) {
        if (g_rendererFailed) {
            // A GL init failure is permanent for the plugin's lifetime (see g_rendererFailed's
            // comment), so this session can never render; end it now instead of leaving
            // s.state stuck "active" forever with no draw() ever reaching the check below.
            // face -1: the renderer never produced anything to commit to.
            endSessionDeferred(-1);
            return {};
        }
        g_rendererReady = g_renderer.init();
        if (!g_rendererReady) {
            g_rendererFailed = true;
            HyprlandAPI::addNotification(PHANDLE,
                std::string("[hypr-cube] GL init failed: ") + g_renderer.lastError(),
                CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
            return {};
        }
    }

    const cube::Frame f = s.state.update(nowMs());

    // faceMvp works in pixel space; the VBO is a unit quad, so scale it up. Depends only
    // on the session's geometry, so it is built once rather than per face per frame.
    // m[0]/m[5] are the (0,0)/(1,1) entries of math.hpp's column-major m[col*4+row].
    cube::Mat4 scale = cube::identity();
    scale.m[0] = s.geometry.width;
    scale.m[5] = s.geometry.height;

    std::vector<gl::CubeRenderer::Quad> quads;
    quads.reserve(s.faces.size());
    for (int k = 0; k < (int)s.faces.size(); ++k) {
        if (!s.faces[k].valid())
            continue;

        const cube::Mat4 faceM = cube::faceMvp(s.geometry, k, f.angle, f.zoom);
        const cube::Mat4 mvp   = cube::multiply(faceM, scale);

        // Depth for painter's sort: the face centre's pre-divide clip w (== view-space
        // distance from the camera for a perspective projection).
        const cube::Vec4 centre = cube::transform(faceM, {0.f, 0.f, 0.f, 1.f});
        quads.push_back({(unsigned int)s.faces[k].tex->m_texID, mvp, centre.w});
    }

    g_renderer.draw(std::move(quads), s.background,
                    (int)s.monitor->m_pixelSize.x, (int)s.monitor->m_pixelSize.y);
    g_pHyprRenderer->damageMonitor(s.monitor);

    if (!s.state.active())
        endSessionDeferred(s.state.takeCommit());

    return {};
}

void startSession(PHLMONITOR mon, const cube::Config& cfg, int fromFace, bool captureAll) {
    const float fovYRad = cfg.fovDeg * (float)M_PI / 180.f;
    const cube::Geometry geometry =
        cube::makeGeometry(cfg.faces, (float)mon->m_pixelSize.x, (float)mon->m_pixelSize.y, fovYRad);

    std::vector<FaceTexture> faces(cfg.faces);
    if (captureAll) {
        // One glFinish() after the batch instead of one per face (drag start otherwise
        // stalls the pipeline 4-16 times in a row).
        for (int i = 0; i < cfg.faces; ++i)
            faces[i] = captureWorkspace(mon, cube::workspaceOfFace(i), /*sync=*/false);
        finishCaptureSync();
    } else {
        faces[fromFace] = captureWorkspace(mon, cube::workspaceOfFace(fromFace));
    }

    g_session.emplace(CubeSession{mon, cube::CubeState{cfg}, geometry, std::move(faces), {}});
    for (int i = 0; i < 4; ++i)
        g_session->background[i] = g_background[i];

    g_stageListener = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) {
        if (stage != RENDER_LAST_MOMENT)
            return;

        // Every monitor fires this stage callback on its own render cycle, but g_pendingEnd/
        // g_pendingFace/g_pendingTeardown below are session-global state: a foreign monitor's
        // frame must never read or mutate them, or it can steal the session's one-frame commit
        // and teardown sequencing (racing the session's own monitor for who sees pendingTeardown
        // first). So the identity check comes before any of that, not after.
        // Render::SRenderData::pMonitor names the monitor currently being rendered; it is a weak
        // PHLMONITORREF against g_session->monitor's shared PHLMONITOR. CWeakPointer defines
        // operator== against a CSharedPointer but operator!= only against nullptr_t, so negate
        // the equality explicitly rather than reaching for !=.
        if (!g_session || !(g_pHyprRenderer->m_renderData.pMonitor == g_session->monitor))
            return;

        if (g_pendingTeardown) {
            endSession();
            return;
        }

        // Only ever mutates g_pendingEnd/g_pendingFace/g_pendingTeardown (it reads g_session's
        // monitor but never reassigns g_session itself), so the check above still holds
        // afterward; no need to repeat it.
        flushPendingCommit();

        g_pHyprRenderer->m_renderPass.add(makeUnique<CubePassElement>());
    });
}

void endSession() {
    g_stageListener.reset();
    g_pHyprRenderer->m_renderPass.removeAllOfType("CubePassElement");
    if (g_rendererReady) {
        g_renderer.destroy();
        g_rendererReady = false;
    }
    g_session.reset();
    // Direct callers (cubeStop, PLUGIN_EXIT) bypass flushPendingCommit's own reset of these;
    // without clearing them here, a commit left pending from the session just torn down would
    // fire on the very first frame of the next session and kill it immediately. Same reasoning
    // for g_pendingTeardown: a direct call here (e.g. unload landing between the commit frame
    // and its teardown frame) must not leave it set for the next session to trip over.
    g_pendingEnd      = false;
    g_pendingFace     = -1;
    g_pendingTeardown = false;
}

void endSessionDeferred(int face) {
    g_pendingEnd  = true;
    g_pendingFace = face;
}

}
