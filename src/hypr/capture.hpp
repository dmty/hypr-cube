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

FaceTexture captureWorkspace(PHLMONITOR mon, int workspaceId);

}
