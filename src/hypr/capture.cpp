#include "capture.hpp"
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/helpers/time/Time.hpp>

namespace hypr {
namespace {

// IHyprRenderer::renderWorkspace is protected, and there is no public equivalent for
// rendering an arbitrary workspace into an off-screen framebuffer (beginFullFakeRender
// covers the begin/end half publicly; only this one call needs borrowing). Naming a
// protected member to form a pointer-to-member is not access-checked when that name
// appears as an explicit template argument, so this borrows it without touching any
// access specifier or header.
template <typename Tag, typename Tag::type Member> struct Steal {
    friend typename Tag::type stolen(Tag) {
        return Member;
    }
};

struct RenderWorkspaceTag {
    using type = void (Render::IHyprRenderer::*)(PHLMONITOR, PHLWORKSPACE, const Time::steady_tp&, const CBox&);
    friend type stolen(RenderWorkspaceTag);
};
template struct Steal<RenderWorkspaceTag, &Render::IHyprRenderer::renderWorkspace>;

}

FaceTexture captureWorkspace(PHLMONITOR mon, int workspaceId, bool sync) {
    FaceTexture out;
    out.workspaceId = workspaceId;

    if (!mon)
        return out;

    const auto ws = State::workspaceState()->query().id(workspaceId).run();
    if (!ws)
        return out; // does not exist yet: caller draws background
    if (ws->m_monitor != mon)
        return out; // lives on another monitor: same treatment

    const CBox monbox = {0.0, 0.0, mon->m_pixelSize.x, mon->m_pixelSize.y};

    out.fb = g_pHyprRenderer->createFB("hypr-cube");
    out.fb->alloc(monbox.w, monbox.h, mon->m_output->state->state().drmFormat);
    // Required, not cosmetic: without an image description, renderTextureInternal derefs
    // a null one and crashes the compositor the first time this texture is drawn.
    out.fb->setImageDescription(mon->workBufferImageDescription());

    const auto     savedWorkspace   = mon->m_activeWorkspace;
    const auto     savedSpecial     = mon->m_activeSpecialWorkspace;
    const bool     savedFeedback    = g_pHyprRenderer->m_bBlockSurfaceFeedback;
    const bool     savedVisible     = ws->m_visible;
    const bool     savedForceRend   = ws->m_forceRendering;
    const float    savedAlphaVal    = ws->m_alpha->value();
    const float    savedAlphaGoal   = ws->m_alpha->goal();
    const Vector2D savedOffsetVal   = ws->m_renderOffset->value();
    const Vector2D savedOffsetGoal  = ws->m_renderOffset->goal();
    CRegion        fakeDamage{0, 0, INT16_MAX, INT16_MAX};

    // shouldRenderWindow's visibility gate is keyed on CWorkspace::isVisible() (== m_visible),
    // not on which workspace mon->m_activeWorkspace points at. Reassigning m_activeWorkspace
    // below is not enough on its own: whatever workspace is actually on screen keeps
    // m_visible == true, still passes the gate, and its windows render into the capture right
    // alongside the target's — the real one wins since it's drawn after. Hide it (and any
    // active special workspace) for the duration, unless it's the same workspace we're
    // capturing.
    const bool sourceHadVisible  = savedWorkspace && savedWorkspace != ws && savedWorkspace->m_visible;
    const bool specialHadVisible = savedSpecial && savedSpecial != ws && savedSpecial->m_visible;
    if (sourceHadVisible)
        savedWorkspace->m_visible = false;
    if (specialHadVisible)
        savedSpecial->m_visible = false;

    g_pHyprRenderer->m_bBlockSurfaceFeedback = true;

    // Follows IHyprRenderer::makeSnapshotFB's own shape exactly (Renderer.cpp): the public
    // beginFullFakeRender/draw(clear)/startRenderPass sequence, not raw GL calls, is what
    // this renderer expects around a fake render into an arbitrary framebuffer.
    g_pHyprRenderer->beginFullFakeRender(mon, fakeDamage, out.fb);
    g_pHyprRenderer->m_bRenderingSnapshot = true;
    g_pHyprRenderer->draw(CClearPassElement::SClearData{CHyprColor(0, 0, 0, 1)});
    g_pHyprRenderer->startRenderPass();

    // shouldRenderWindow gates on the workspace being visible/forced/at full alpha; a
    // workspace that isn't the one currently shown otherwise sits at its faded-out
    // resting state. It also normally sits parked a monitor-width off to the side
    // (m_renderOffset) since it isn't the active workspace being slid to; without zeroing
    // that too, its windows render fully opaque but entirely off the edge of the capture.
    mon->m_activeWorkspace = ws;
    ws->m_visible          = true;
    ws->m_forceRendering   = true;
    ws->m_alpha->setValueAndWarp(1.F);
    ws->m_renderOffset->setValueAndWarp(Vector2D{0, 0});
    (g_pHyprRenderer.get()->*stolen(RenderWorkspaceTag{}))(mon, ws, Time::steadyNow(), monbox);
    g_pHyprRenderer->m_renderData.blockScreenShader = true;
    g_pHyprRenderer->endRender();

    // This VM's GPU path is guest GL -> virgl -> host ANGLE -> Metal; without a hard sync
    // here, the capture's draw commands are not guaranteed complete before the texture is
    // sampled from a later, separate render pass, and the result was empirically blank.
    // A multi-face caller can defer this (sync=false) and call finishCaptureSync() once
    // after the whole batch instead: glFinish() blocks on everything queued so far, so one
    // call after N captures still covers all N.
    if (sync)
        glFinish();

    g_pHyprRenderer->m_bRenderingSnapshot = false;
    mon->m_activeWorkspace                = savedWorkspace;
    ws->m_visible                         = savedVisible;
    ws->m_forceRendering                  = savedForceRend;
    ws->m_alpha->setValueAndWarp(savedAlphaVal);
    *ws->m_alpha                              = savedAlphaGoal;
    ws->m_renderOffset->setValueAndWarp(savedOffsetVal);
    *ws->m_renderOffset                       = savedOffsetGoal;
    g_pHyprRenderer->m_bBlockSurfaceFeedback = savedFeedback;
    if (sourceHadVisible)
        savedWorkspace->m_visible = true;
    if (specialHadVisible)
        savedSpecial->m_visible = true;

    out.tex = out.fb->getTexture();
    return out;
}

void finishCaptureSync() {
    glFinish();
}

}
