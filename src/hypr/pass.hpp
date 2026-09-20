#pragma once
#include "capture.hpp"
#include <hyprland/src/render/pass/PassElement.hpp>

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

void startFrameLoop(PHLMONITOR mon);
void stopFrameLoop();

}
