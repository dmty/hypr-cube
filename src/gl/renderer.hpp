#pragma once
#include "../cube/math.hpp"
#include <vector>

namespace gl {

class CubeRenderer {
  public:
    struct Quad {
        unsigned int texture;
        cube::Mat4   mvp;
        float        depth;      // larger is further from the camera
    };

    bool init();
    void destroy();
    // Takes quads by value: draw sorts them back to front in place, and the caller
    // builds a fresh vector each frame, so moving in avoids a per-frame copy.
    void draw(std::vector<Quad> quads, const float bg[4], int viewportW, int viewportH);

    const char* lastError() const { return m_error; }

  private:
    unsigned int m_program = 0;
    unsigned int m_vbo     = 0;
    int          m_uMvp    = -1;
    int          m_uTex    = -1;
    int          m_aPos    = -1;
    int          m_aUv     = -1;
    const char*  m_error   = "";
};

}
