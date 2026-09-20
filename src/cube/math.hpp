#pragma once

namespace cube {

// Column-major: m[col * 4 + row]. Matches GL's default uniform layout.
struct Mat4 {
    float m[16];
};

struct Vec4 {
    float x, y, z, w;
};

Mat4 identity();
Mat4 multiply(const Mat4& a, const Mat4& b);   // returns a * b
Mat4 rotateY(float rad);
Mat4 translate(float x, float y, float z);
Vec4 transform(const Mat4& a, const Vec4& v);

bool approxEq(float a, float b, float eps = 1e-4f);

// A regular N-gon prism of workspace faces, viewed face-on from outside.
struct Geometry {
    int   faces;
    float width;
    float height;
    float fovYRad;
    float apothem;      // axis to face-centre distance
    float cameraDist;   // origin to camera distance along +Z
};

Mat4     perspective(float fovYRad, float aspect, float nearZ, float farZ);
Geometry makeGeometry(int faces, float width, float height, float fovYRad);
Mat4     faceMvp(const Geometry& g, int face, float angle, float zoom);
// corner: 0 bottom-left, 1 bottom-right, 2 top-right, 3 top-left. NDC, divide already applied.
Vec4     projectFaceCorner(const Geometry& g, int face, float angle, float zoom, int corner);

}
