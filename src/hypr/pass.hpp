#pragma once
#include "capture.hpp"
#include "../cube/math.hpp"
#include "../cube/state.hpp"
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <optional>
#include <vector>

namespace hypr {

class CubePassElement : public IPassElement {
  public:
    std::vector<UP<IPassElement>> draw() override;
    bool                          needsLiveBlur() override { return false; }
    bool                          needsPrecomputeBlur() override { return false; }
    ePassElementType              type() override { return EK_CUSTOM; }
    const char*                   passName() override { return "CubePassElement"; }
    bool                          disableSimplification() override { return true; }
};

struct CubeSession {
    PHLMONITOR               monitor;
    cube::CubeState          state;
    cube::Geometry           geometry;
    std::vector<FaceTexture> faces;      // indexed by face, may hold invalid entries
    float                    background[4] = {0.f, 0.f, 0.f, 1.f};
};

extern std::optional<CubeSession> g_session;

// Populated in plugin.cpp; filled from config in a later task.
extern cube::Config g_cfg;
extern float        g_background[4];

// The clock cube::CubeState is driven by. Shared so every call site reads the same one.
double nowMs();

// Notifies on a failed Config::Actions call; shared by every workspace-switch site.
void reportIfFailed(const char* what, const Config::Actions::ActionResult& res);

void startSession(PHLMONITOR mon, const cube::Config& cfg);
void endSession();
// Records the face to commit (or -1 to just end without switching); the actual
// workspace switch and session teardown happen on the next event-loop turn.
void endSessionDeferred(int face);

}
