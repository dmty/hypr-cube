#include "renderer.hpp"
#include <GLES3/gl32.h>
#include <algorithm>

namespace gl {
namespace {

// This VM's EGL context negotiates down to GLES 3.0 (3.2 context creation fails), so the
// shaders must target 300 es even though the GLES3/gl32.h headers are present.
const char* VERT = R"(#version 300 es
uniform mat4 mvp;
in vec3 pos;
in vec2 uv;
out vec2 vUv;
void main() {
    vUv = uv;
    gl_Position = mvp * vec4(pos, 1.0);
}
)";

const char* FRAG = R"(#version 300 es
precision highp float;
uniform sampler2D tex;
in vec2 vUv;
out vec4 outColor;
void main() { outColor = texture(tex, vUv); }
)";

// A unit quad in local face space: x and y in [-0.5, 0.5], z = 0.
// The MVP scales it to the face's real size, so geometry never changes.
// v=0 at the bottom vertex: the capture's FBO-backed texture reads bottom-up relative to
// screen space, so this is inverted from the brief's listing to come out right side up.
const float QUAD[] = {
    //  x      y     z     u     v
    -0.5f, -0.5f, 0.f,  0.f, 0.f,
     0.5f, -0.5f, 0.f,  1.f, 0.f,
     0.5f,  0.5f, 0.f,  1.f, 1.f,
    -0.5f, -0.5f, 0.f,  0.f, 0.f,
     0.5f,  0.5f, 0.f,  1.f, 1.f,
    -0.5f,  0.5f, 0.f,  0.f, 1.f,
};

unsigned int compile(unsigned int type, const char* src) {
    const unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(s); return 0; }
    return s;
}

}

bool CubeRenderer::init() {
    const unsigned int vs = compile(GL_VERTEX_SHADER, VERT);
    const unsigned int fs = compile(GL_FRAGMENT_SHADER, FRAG);
    if (!vs || !fs) {
        // Whichever one compiled is still a live GL object; only the failed one deleted
        // itself inside compile(). Without this, a fixed, always-failing shader source
        // leaks one shader object per init() attempt.
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        m_error = "shader compile failed";
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    int ok = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(m_program);
        m_program = 0;
        m_error   = "program link failed";
        return false;
    }

    m_uMvp = glGetUniformLocation(m_program, "mvp");
    m_uTex = glGetUniformLocation(m_program, "tex");
    m_aPos = glGetAttribLocation(m_program, "pos");
    m_aUv  = glGetAttribLocation(m_program, "uv");

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void CubeRenderer::destroy() {
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
    m_uMvp = m_uTex = m_aPos = m_aUv = -1;
}

void CubeRenderer::draw(std::vector<Quad> quads, const float bg[4],
                        int viewportW, int viewportH) {
    // Hyprland's CHyprOpenGLImpl shadows GL state. Save everything touched.
    int  prevProgram = 0, prevVbo = 0, prevViewport[4] = {0, 0, 0, 0};
    bool prevBlend = glIsEnabled(GL_BLEND);
    bool prevDepth = glIsEnabled(GL_DEPTH_TEST);
    bool prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevVbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    float prevClearColor[4] = {0, 0, 0, 0};
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClearColor);
    int prevActiveTexture = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    // The texture binding we're about to disturb belongs to unit 0 specifically, since
    // that's the unit draw() activates below — not whatever unit was active on entry.
    glActiveTexture(GL_TEXTURE0);
    int prevTexBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexBinding);
    // A link that optimized an attribute away leaves its location at -1; querying or
    // enabling/disabling that is GL_INVALID_VALUE every frame and corrupts the restore below.
    int prevPosEnabled = 0, prevUvEnabled = 0;
    if (m_aPos >= 0)
        glGetVertexAttribiv(m_aPos, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &prevPosEnabled);
    if (m_aUv >= 0)
        glGetVertexAttribiv(m_aUv, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &prevUvEnabled);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);        // convex prism: painter's algorithm suffices
    glDisable(GL_BLEND);
    glViewport(0, 0, viewportW, viewportH);

    glClearColor(bg[0], bg[1], bg[2], bg[3]);
    glClear(GL_COLOR_BUFFER_BIT);

    std::sort(quads.begin(), quads.end(),
              [](const Quad& a, const Quad& b) { return a.depth > b.depth; });

    glUseProgram(m_program);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    if (m_aPos >= 0) {
        glEnableVertexAttribArray(m_aPos);
        glVertexAttribPointer(m_aPos, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    }
    if (m_aUv >= 0) {
        glEnableVertexAttribArray(m_aUv);
        glVertexAttribPointer(m_aUv, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                              (void*)(3 * sizeof(float)));
    }

    glUniform1i(m_uTex, 0); // texture unit 0, activated above while saving its prior binding

    for (const Quad& q : quads) {
        if (!q.texture) continue;
        glBindTexture(GL_TEXTURE_2D, q.texture);
        glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, q.mvp.m);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    if (m_aPos >= 0) { if (prevPosEnabled) glEnableVertexAttribArray(m_aPos); else glDisableVertexAttribArray(m_aPos); }
    if (m_aUv >= 0)  { if (prevUvEnabled)  glEnableVertexAttribArray(m_aUv);  else glDisableVertexAttribArray(m_aUv); }
    glBindTexture(GL_TEXTURE_2D, prevTexBinding);
    glActiveTexture(prevActiveTexture);

    glUseProgram(prevProgram);
    glBindBuffer(GL_ARRAY_BUFFER, prevVbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glClearColor(prevClearColor[0], prevClearColor[1], prevClearColor[2], prevClearColor[3]);
    if (prevBlend)   glEnable(GL_BLEND);
    if (prevDepth)   glEnable(GL_DEPTH_TEST);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
}

}
