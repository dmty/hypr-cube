#include "../src/cube/math.hpp"
#include <cassert>
#include <cstdio>

using namespace cube;

static const float HALF_PI = 1.57079632679f;

static void expectMat(const Mat4& got, const Mat4& want, const char* what) {
    for (int i = 0; i < 16; ++i) {
        if (!approxEq(got.m[i], want.m[i])) {
            std::printf("FAIL %s: element %d got %f want %f\n", what, i, got.m[i], want.m[i]);
            assert(false);
        }
    }
}

static void testIdentityIsMultiplicativeUnit() {
    const Mat4 t = translate(3.f, 4.f, 5.f);
    expectMat(multiply(t, identity()), t, "t * I");
    expectMat(multiply(identity(), t), t, "I * t");
}

static void testTranslateMovesAPoint() {
    const Vec4 p = transform(translate(1.f, 2.f, 3.f), {0.f, 0.f, 0.f, 1.f});
    assert(approxEq(p.x, 1.f) && approxEq(p.y, 2.f) && approxEq(p.z, 3.f) && approxEq(p.w, 1.f));
}

static void testRotateYQuarterTurnMapsXToNegativeZ() {
    const Vec4 p = transform(rotateY(HALF_PI), {1.f, 0.f, 0.f, 1.f});
    assert(approxEq(p.x, 0.f));
    assert(approxEq(p.z, -1.f));
}

static void testMultiplyAppliesRightOperandFirst() {
    // multiply(a, b) means a*b, so b is applied to the vector first.
    const Mat4 m = multiply(rotateY(HALF_PI), translate(1.f, 0.f, 0.f));
    const Vec4 p = transform(m, {0.f, 0.f, 0.f, 1.f});
    assert(approxEq(p.x, 0.f));
    assert(approxEq(p.z, -1.f));
}

int main() {
    testIdentityIsMultiplicativeUnit();
    testTranslateMovesAPoint();
    testRotateYQuarterTurnMapsXToNegativeZ();
    testMultiplyAppliesRightOperandFirst();
    std::printf("all tests passed\n");
    return 0;
}
