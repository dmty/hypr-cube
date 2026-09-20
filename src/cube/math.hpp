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

}
