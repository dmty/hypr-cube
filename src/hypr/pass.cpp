#include "pass.hpp"
#include "globals.hpp"
#include "../gl/renderer.hpp"
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
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

// Set from draw() when the animation finishes; the actual commit/teardown is applied
// from flushPendingCommit() on the next render.stage callback, never from inside draw().
static bool g_pendingEnd  = false;
static int  g_pendingFace = -1;

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

    if (face >= 0)
        reportIfFailed("workspace switch failed",
                       Config::Actions::changeWorkspace(std::to_string(cube::workspaceOfFace(face))));
    endSession();
}

std::vector<UP<IPassElement>> CubePassElement::draw() {
    if (!g_session)
        return {};
    auto& s = *g_session;

    if (!g_rendererReady) {
        if (g_rendererFailed)
            return {};
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
        for (int i = 0; i < cfg.faces; ++i)
            faces[i] = captureWorkspace(mon, cube::workspaceOfFace(i));
    } else {
        faces[fromFace] = captureWorkspace(mon, cube::workspaceOfFace(fromFace));
    }

    g_session.emplace(CubeSession{mon, cube::CubeState{cfg}, geometry, std::move(faces), {}});
    for (int i = 0; i < 4; ++i)
        g_session->background[i] = g_background[i];

    g_stageListener = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) {
        if (stage != RENDER_LAST_MOMENT)
            return;
        flushPendingCommit();
        if (!g_session)
            return;
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
    // fire on the very first frame of the next session and kill it immediately.
    g_pendingEnd  = false;
    g_pendingFace = -1;
}

void endSessionDeferred(int face) {
    g_pendingEnd  = true;
    g_pendingFace = face;
}

}
