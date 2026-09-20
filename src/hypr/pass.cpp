#include "pass.hpp"
#include "globals.hpp"
#include "../gl/renderer.hpp"
#include <hyprland/src/event/EventBus.hpp>

namespace hypr {

// Owned by plugin.cpp; declared here to keep the pass element free of plugin state.
extern FaceTexture g_incoming;
extern PHLMONITOR  g_monitor;

static CHyprSignalListener g_stageListener;
static gl::CubeRenderer    g_renderer;
static bool                g_rendererReady = false;
// A compile/link failure is deterministic for fixed shader source: retrying init() every
// frame would leak nothing (init() now cleans up after itself) but would notify forever.
// Try exactly once per plugin load.
static bool                g_rendererFailed = false;

std::vector<UP<IPassElement>> CubePassElement::draw() {
    if (!g_monitor)
        return {};

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

    // No capture yet (or it fell off the edge of the workspace list): clear to the
    // background colour instead of leaving the real desktop showing through, so a
    // swipe past the last workspace reads as "nothing here", not as a no-op.
    std::vector<gl::CubeRenderer::Quad> quads;
    if (g_incoming.valid()) {
        // Flat identity draw: unit quad scaled to fill NDC exactly.
        cube::Mat4 mvp = cube::identity();
        mvp.m[0] = 2.f;   // unit quad spans -0.5..0.5, so scale by 2 to fill -1..1
        mvp.m[5] = 2.f;
        quads.push_back({(unsigned int)g_incoming.tex->m_texID, mvp, 0.f});
    }

    const float bg[4] = {0.f, 0.f, 0.f, 1.f};
    g_renderer.draw(std::move(quads), bg, (int)g_monitor->m_pixelSize.x, (int)g_monitor->m_pixelSize.y);
    return {};
}

void startFrameLoop(PHLMONITOR mon) {
    g_monitor       = mon;
    g_stageListener = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) {
        if (stage != RENDER_LAST_MOMENT || !g_monitor)
            return;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CubePassElement>());
        g_pHyprRenderer->damageMonitor(g_monitor);
    });
}

void stopFrameLoop() {
    g_stageListener.reset();
    g_pHyprRenderer->m_renderPass.removeAllOfType("CubePassElement");
    if (g_rendererReady) {
        g_renderer.destroy();
        g_rendererReady = false;
    }
    g_monitor = nullptr;
}

}
