#include "state.hpp"

namespace cube {
namespace {

template <typename T>
bool clampTo(T& v, T lo, T hi) {
    if (v < lo) { v = lo; return true; }
    if (v > hi) { v = hi; return true; }
    return false;
}

}

Clamped validate(Config raw) {
    Clamped out;
    // faces < 3 is degenerate: 2 is a flat flip, and (w/2)/tan(pi/N) blows up as N -> 1.
    out.facesClamped    = clampTo(raw.faces, 3, 16);
    out.fovClamped      = clampTo(raw.fovDeg, 20.f, 120.f);
    out.dragZoomClamped = clampTo(raw.dragZoom, 0.2f, 1.f);
    clampTo(raw.durationMs, 16, 5000);
    if (raw.dragSensitivity <= 0.f)
        raw.dragSensitivity = 1.f;
    out.config = raw;
    return out;
}

int faceOfWorkspace(int workspaceId, int faces) {
    if (workspaceId < 1 || workspaceId > faces)
        return -1;
    return workspaceId - 1;
}

int workspaceOfFace(int face) {
    return face + 1;
}

int normalizeFace(int face, int faces) {
    const int r = face % faces;
    return r < 0 ? r + faces : r;
}

}
