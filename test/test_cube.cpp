#include "../src/cube/math.hpp"
#include <cassert>
#include <cstdio>

using namespace cube;

static const float PI      = 3.14159265359f;
static const float HALF_PI = PI / 2.f;

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

static void testApothemForFourFacesIsHalfWidth() {
    const Geometry g = makeGeometry(4, 1920.f, 1080.f, 0.7853981634f); // 45 deg
    assert(approxEq(g.apothem, 960.f, 0.01f));
}

static void testApothemGrowsWithFaceCount() {
    const float fov = 0.7853981634f;
    const Geometry g4 = makeGeometry(4, 1920.f, 1080.f, fov);
    const Geometry g8 = makeGeometry(8, 1920.f, 1080.f, fov);
    assert(g8.apothem > g4.apothem);
}

static void testCameraSitsOutsideThePrism() {
    const Geometry g = makeGeometry(4, 1920.f, 1080.f, 0.7853981634f);
    assert(g.cameraDist > g.apothem);
}

// The invariant that matters most: at rest, face 0 exactly fills the viewport,
// so the handover to stock Hyprland rendering is invisible.
static void testRestingFaceFillsViewportExactly() {
    const Geometry g = makeGeometry(4, 1920.f, 1080.f, 0.7853981634f);
    const Vec4 bl = projectFaceCorner(g, 0, 0.f, 1.f, 0);
    const Vec4 br = projectFaceCorner(g, 0, 0.f, 1.f, 1);
    const Vec4 tr = projectFaceCorner(g, 0, 0.f, 1.f, 2);
    const Vec4 tl = projectFaceCorner(g, 0, 0.f, 1.f, 3);
    assert(approxEq(bl.x, -1.f) && approxEq(bl.y, -1.f));
    assert(approxEq(br.x,  1.f) && approxEq(br.y, -1.f));
    assert(approxEq(tr.x,  1.f) && approxEq(tr.y,  1.f));
    assert(approxEq(tl.x, -1.f) && approxEq(tl.y,  1.f));
}

static void testRestingInvariantHoldsAtEveryLegalFaceCountAndFov() {
    for (int n = 3; n <= 16; ++n)
        for (float fovDeg = 20.f; fovDeg <= 120.f; fovDeg += 10.f) {
            const Geometry g = makeGeometry(n, 1920.f, 1080.f, fovDeg * PI / 180.f);
            const Vec4 tr = projectFaceCorner(g, 0, 0.f, 1.f, 2);
            assert(approxEq(tr.x, 1.f) && approxEq(tr.y, 1.f));
        }
}

static void testZoomOutShrinksTheFace() {
    const Geometry g = makeGeometry(4, 1920.f, 1080.f, 0.7853981634f);
    const Vec4 full = projectFaceCorner(g, 0, 0.f, 1.f,  2);
    const Vec4 small = projectFaceCorner(g, 0, 0.f, 0.6f, 2);
    assert(small.x < full.x && small.y < full.y);
    assert(small.x > 0.f && small.y > 0.f);
}

static void testRotatingOneStepBringsTheNextFaceToRest() {
    const int n = 4;
    const float step = 2.f * PI / n;
    const Geometry g = makeGeometry(n, 1920.f, 1080.f, 0.7853981634f);
    // angle == -step is face 1's resting orientation.
    const Vec4 tr = projectFaceCorner(g, 1, -step, 1.f, 2);
    assert(approxEq(tr.x, 1.f) && approxEq(tr.y, 1.f));
}

int main() {
    testIdentityIsMultiplicativeUnit();
    testTranslateMovesAPoint();
    testRotateYQuarterTurnMapsXToNegativeZ();
    testMultiplyAppliesRightOperandFirst();
    testApothemForFourFacesIsHalfWidth();
    testApothemGrowsWithFaceCount();
    testCameraSitsOutsideThePrism();
    testRestingFaceFillsViewportExactly();
    testRestingInvariantHoldsAtEveryLegalFaceCountAndFov();
    testZoomOutShrinksTheFace();
    testRotatingOneStepBringsTheNextFaceToRest();
    std::printf("all tests passed\n");
    return 0;
}
