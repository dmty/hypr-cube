#pragma once
#define WLR_USE_UNSTABLE
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>

namespace hypr {

struct FaceTexture {
    SP<Render::IFramebuffer> fb;
    SP<Render::ITexture>     tex;
    int                      workspaceId = -1;
    bool                     valid() const { return tex != nullptr; }
};

// sync=false skips the trailing glFinish(): a caller capturing several faces in a row can
// pass false for all but call finishCaptureSync() once after the loop instead, collapsing
// N pipeline stalls into one.
FaceTexture captureWorkspace(PHLMONITOR mon, int workspaceId, bool sync = true);
void        finishCaptureSync();

}
