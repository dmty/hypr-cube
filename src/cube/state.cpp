#include "state.hpp"

#include <cmath>

namespace cube {
namespace {

template <typename T>
bool clampTo(T& v, T lo, T hi) {
    if (v < lo) { v = lo; return true; }
    if (v > hi) { v = hi; return true; }
    return false;
}

constexpr float PI = 3.14159265358979323846f;

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

float easeOutCubic(float t) {
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    const float u = 1.f - t;
    return 1.f - u * u * u;
}

CubeState::CubeState(Config validated) : m_cfg(validated) {}

float CubeState::step() const {
    return 2.f * PI / static_cast<float>(m_cfg.faces);
}

bool CubeState::startRotate(int fromFace, int dir, double nowMs) {
    if (m_phase != Phase::Idle)
        return false;
    m_fromAngle  = -static_cast<float>(fromFace) * step();
    m_angle      = m_fromAngle;
    // Angle space, not face space: one step in the requested direction is always
    // the short way round, including prev-from-face-0.
    m_toAngle    = m_fromAngle - static_cast<float>(dir) * step();
    m_startMs    = nowMs;
    m_durationMs = static_cast<double>(m_cfg.durationMs);
    m_zoom       = 1.f;
    m_phase      = Phase::Rotating;
    return true;
}

Frame CubeState::update(double nowMs) {
    if (m_phase == Phase::Rotating) {
        const double t = (nowMs - m_startMs) / m_durationMs;
        if (t >= 1.0) {
            m_angle  = m_toAngle;            // assigned, never lerped, so it is exact
            m_phase  = Phase::Idle;
            m_commit = frontFace();
        } else {
            const float e = easeOutCubic(static_cast<float>(t));
            m_angle = m_fromAngle + (m_toAngle - m_fromAngle) * e;
        }
        m_zoom = 1.f;
    }
    return {m_angle, m_zoom};
}

int CubeState::frontFace() const {
    const int k = static_cast<int>(std::lround(-m_angle / step()));
    return normalizeFace(k, m_cfg.faces);
}

int CubeState::takeCommit() {
    const int c = m_commit;
    m_commit    = -1;
    return c;
}

}
