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

int faceOfWorkspace(int workspaceId, int faces);
int workspaceOfFace(int face);
int normalizeFace(int face, int faces);

}
