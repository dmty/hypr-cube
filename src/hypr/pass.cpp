#include "pass.hpp"
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/event/EventBus.hpp>

namespace hypr {
namespace {

// renderTextureInternal is private, and CTexPassElement (the public pass-element route to
// it) is built around window surfaces and does not display a bare, surface-less capture
// correctly. Borrowing the member directly, the way it is meant to be called, sidesteps
// that: see the comment on the identical trick in capture.cpp for why this is safe.
template <typename Tag, typename Tag::type Member> struct Steal {
    friend typename Tag::type stolen(Tag) {
        return Member;
    }
};

struct RenderTextureInternalTag {
    using type = void (Render::GL::CHyprOpenGLImpl::*)(SP<Render::ITexture>, const CBox&, const Render::GL::CHyprOpenGLImpl::STextureRenderData&);
    friend type stolen(RenderTextureInternalTag);
};
template struct Steal<RenderTextureInternalTag, &Render::GL::CHyprOpenGLImpl::renderTextureInternal>;

}

// Owned by plugin.cpp; declared here to keep the pass element free of plugin state.
extern FaceTexture g_incoming;
extern PHLMONITOR  g_monitor;

static CHyprSignalListener g_stageListener;

std::vector<UP<IPassElement>> CubePassElement::draw() {
    if (!g_monitor)
        return {};

    const CBox box = {0.0, 0.0, g_monitor->m_pixelSize.x, g_monitor->m_pixelSize.y};

    // captureWorkspace returns no texture for a workspace that doesn't exist or lives on
    // another monitor; draw its background (black) instead of leaving the real desktop
    // showing through, so a swipe past the last workspace reads as "nothing here", not
    // as the gesture having no effect.
    if (!g_incoming.valid()) {
        std::vector<UP<IPassElement>> out;
        out.emplace_back(makeUnique<CRectPassElement>(CRectPassElement::SRectData{.box = box, .color = CHyprColor{0.f, 0.f, 0.f, 1.f}}));
        return out;
    }

    const CRegion& damage = g_pHyprRenderer->renderData().damage;

    Render::GL::CHyprOpenGLImpl::STextureRenderData data;
    data.damage = &damage;
    data.a      = 1.f;
    (Render::GL::g_pHyprOpenGL.get()->*stolen(RenderTextureInternalTag{}))(g_incoming.tex, box, data);
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
    g_monitor = nullptr;
}

}
