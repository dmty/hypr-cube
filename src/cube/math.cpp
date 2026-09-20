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

}
