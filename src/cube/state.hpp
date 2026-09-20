#pragma once

namespace cube {

struct Config {
    int   faces           = 4;
    int   durationMs      = 300;
    float fovDeg          = 45.f;
    float dragZoom        = 0.6f;
    float dragSensitivity = 1.f;
};

struct Clamped {
    Config config;
    bool   facesClamped    = false;
    bool   fovClamped      = false;
    bool   dragZoomClamped = false;
};

Clamped validate(Config raw);

int faceOfWorkspace(int workspaceId, int faces);   // -1 if workspaceId is not a face
int workspaceOfFace(int face);
int normalizeFace(int face, int faces);

enum class Phase { Idle, Rotating, Dragging, Settling };

struct Frame {
    float angle;
    float zoom;
};

float easeOutCubic(float t);

class CubeState {
  public:
    explicit CubeState(Config validated);

    bool  startRotate(int fromFace, int dir, double nowMs);
    Frame update(double nowMs);

    bool startDrag(int fromFace, double nowMs);
    void addDragDelta(float dxPixels);
    void release(double nowMs);
    void abort(double nowMs);

    Phase phase() const { return m_phase; }
    bool  active() const { return m_phase != Phase::Idle; }
    int   frontFace() const;
    int   takeCommit();   // -1 if nothing to commit

    float step() const;

  private:
    Config m_cfg;
    Phase  m_phase      = Phase::Idle;
    float  m_angle      = 0.f;
    float  m_zoom       = 1.f;
    float  m_fromAngle  = 0.f;
    float  m_toAngle    = 0.f;
    double m_startMs    = 0.0;
    double m_durationMs = 0.0;
    int    m_commit     = -1;

    float m_fromZoom       = 1.f;
    float m_toZoom         = 1.f;
    int   m_originFace     = 0;
    bool  m_suppressCommit = false;
};

}
