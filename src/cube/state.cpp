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

constexpr float DRAG_REFERENCE_PX = 500.f;   // px for one face at sensitivity 1.0

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

bool CubeState::startDrag(int fromFace, double nowMs) {
    if (m_phase != Phase::Idle)
        return false;
    m_originFace     = fromFace;
    m_angle          = -static_cast<float>(fromFace) * step();
    m_fromZoom       = 1.f;
    m_toZoom         = m_cfg.dragZoom;
    m_startMs        = nowMs;
    m_durationMs     = static_cast<double>(m_cfg.durationMs);
    m_suppressCommit = false;
    m_phase          = Phase::Dragging;
    return true;
}

void CubeState::addDragDelta(float dxPixels) {
    if (m_phase != Phase::Dragging)
        return;
    m_angle += dxPixels * m_cfg.dragSensitivity * (step() / DRAG_REFERENCE_PX);
}

void CubeState::release(double nowMs) {
    if (m_phase != Phase::Dragging)
        return;
    m_fromAngle  = m_angle;
    m_toAngle    = -static_cast<float>(std::lround(-m_angle / step())) * step();
    m_fromZoom   = m_zoom;
    m_toZoom     = 1.f;
    m_startMs    = nowMs;
    m_durationMs = static_cast<double>(m_cfg.durationMs);
    m_phase      = Phase::Settling;
}

void CubeState::abort(double nowMs) {
    if (m_phase != Phase::Dragging)
        return;
    release(nowMs);
    // Congruent target nearest the current angle, not the canonical -originFace*step:
    // after a multi-turn free spin the canonical target would unwind the whole spin
    // backwards inside one duration_ms. Which target is chosen never changes the
    // landing face, only how far the ease travels to get there.
    const float turn = 2.f * PI;
    m_toAngle = -static_cast<float>(m_originFace) * step()
              - static_cast<float>(std::lround((-m_angle - m_originFace * step()) / turn)) * turn;
    m_suppressCommit = true;
}

Frame CubeState::update(double nowMs) {
    const double t = m_durationMs > 0.0 ? (nowMs - m_startMs) / m_durationMs : 1.0;
    const float  e = easeOutCubic(static_cast<float>(t));

    switch (m_phase) {
        case Phase::Idle: break;

        case Phase::Rotating:
            if (t >= 1.0) {
                m_angle  = m_toAngle;         // assigned, never lerped, so it is exact
                m_zoom   = 1.f;
                m_phase  = Phase::Idle;
                m_commit = frontFace();
            } else {
                m_angle = m_fromAngle + (m_toAngle - m_fromAngle) * e;
                m_zoom  = 1.f;
            }
            break;

        case Phase::Dragging:
            // Angle is driven by addDragDelta; only zoom is time-based here.
            m_zoom = t >= 1.0 ? m_toZoom : m_fromZoom + (m_toZoom - m_fromZoom) * e;
            break;

        case Phase::Settling:
            if (t >= 1.0) {
                m_angle  = m_toAngle;
                m_zoom   = m_toZoom;
                m_phase  = Phase::Idle;
                m_commit = m_suppressCommit ? -1 : frontFace();
            } else {
                m_angle = m_fromAngle + (m_toAngle - m_fromAngle) * e;
                m_zoom  = m_fromZoom + (m_toZoom - m_fromZoom) * e;
            }
            break;
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
