#include "math.hpp"
#include <cmath>

namespace cube {

Mat4 identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
    return r;
}

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float sum = 0.f;
            for (int k = 0; k < 4; ++k)
                sum += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = sum;
        }
    return r;
}

Mat4 rotateY(float rad) {
    Mat4 r  = identity();
    const float c = std::cos(rad), s = std::sin(rad);
    r.m[0]  = c;
    r.m[2]  = -s;
    r.m[8]  = s;
    r.m[10] = c;
    return r;
}

Mat4 translate(float x, float y, float z) {
    Mat4 r  = identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

Vec4 transform(const Mat4& a, const Vec4& v) {
    return {a.m[0] * v.x + a.m[4] * v.y + a.m[8]  * v.z + a.m[12] * v.w,
            a.m[1] * v.x + a.m[5] * v.y + a.m[9]  * v.z + a.m[13] * v.w,
            a.m[2] * v.x + a.m[6] * v.y + a.m[10] * v.z + a.m[14] * v.w,
            a.m[3] * v.x + a.m[7] * v.y + a.m[11] * v.z + a.m[15] * v.w};
}

bool approxEq(float a, float b, float eps) {
    const float d = a - b;
    return d < eps && d > -eps;
}

namespace {
constexpr float PI = 3.14159265358979323846f;
}

Mat4 perspective(float fovYRad, float aspect, float nearZ, float farZ) {
    Mat4 r{};
    const float f = 1.f / std::tan(fovYRad / 2.f);
    r.m[0]  = f / aspect;
    r.m[5]  = f;
    r.m[10] = (farZ + nearZ) / (nearZ - farZ);
    r.m[11] = -1.f;
    r.m[14] = (2.f * farZ * nearZ) / (nearZ - farZ);
    return r;
}

Geometry makeGeometry(int faces, float width, float height, float fovYRad) {
    Geometry g{};
    g.faces      = faces;
    g.width      = width;
    g.height     = height;
    g.fovYRad    = fovYRad;
    g.apothem    = (width / 2.f) / std::tan(PI / static_cast<float>(faces));
    g.cameraDist = g.apothem + (height / 2.f) / std::tan(fovYRad / 2.f);
    return g;
}

Mat4 faceMvp(const Geometry& g, int face, float angle, float zoom) {
    const float step  = 2.f * PI / static_cast<float>(g.faces);
    const Mat4  model = multiply(rotateY(angle + static_cast<float>(face) * step),
                                 translate(0.f, 0.f, g.apothem));
    const Mat4  view  = translate(0.f, 0.f, -g.cameraDist / zoom);
    // near must stay well inside cameraDist - apothem; far must clear the back faces.
    const Mat4  proj  = perspective(g.fovYRad, g.width / g.height,
                                    g.cameraDist * 0.01f, g.cameraDist * 4.f);
    return multiply(proj, multiply(view, model));
}

Vec4 projectFaceCorner(const Geometry& g, int face, float angle, float zoom, int corner) {
    const float hw = g.width / 2.f, hh = g.height / 2.f;
    static const float sx[4] = {-1.f, 1.f, 1.f, -1.f};
    static const float sy[4] = {-1.f, -1.f, 1.f, 1.f};
    const Vec4 clip = transform(faceMvp(g, face, angle, zoom),
                                {sx[corner] * hw, sy[corner] * hh, 0.f, 1.f});
    return {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w, clip.w};
}

}
