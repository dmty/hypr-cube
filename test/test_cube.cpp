#include "../src/cube/math.hpp"
#include "../src/cube/state.hpp"
#include <cassert>
#include <cstdio>

using namespace cube;

static constexpr float PI      = 3.14159265359f;
static constexpr float HALF_PI = PI / 2.f;

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

static void testDefaultConfigSurvivesValidation() {
    const Clamped c = validate(Config{});
    assert(c.config.faces == 4 && c.config.durationMs == 300);
    assert(!c.facesClamped && !c.fovClamped && !c.dragZoomClamped);
}

static void testFacesClampToLegalRange() {
    Config low{};  low.faces  = 1;
    Config high{}; high.faces = 99;
    const Clamped a = validate(low), b = validate(high);
    assert(a.config.faces == 3  && a.facesClamped);
    assert(b.config.faces == 16 && b.facesClamped);
}

static void testTwoFacesIsRejectedAsDegenerate() {
    Config c{}; c.faces = 2;
    const Clamped v = validate(c);
    assert(v.config.faces == 3 && v.facesClamped);
}

static void testFovClampsToLegalRange() {
    Config lo{}; lo.fovDeg = 1.f;
    Config hi{}; hi.fovDeg = 179.f;
    const Clamped a = validate(lo), b = validate(hi);
    assert(approxEq(a.config.fovDeg, 20.f)  && a.fovClamped);
    assert(approxEq(b.config.fovDeg, 120.f) && b.fovClamped);
}

static void testDragZoomClampsToUsableRange() {
    Config lo{}; lo.dragZoom = 0.f;
    Config hi{}; hi.dragZoom = 5.f;
    const Clamped a = validate(lo), b = validate(hi);
    assert(approxEq(a.config.dragZoom, 0.2f) && a.dragZoomClamped);
    assert(approxEq(b.config.dragZoom, 1.f)  && b.dragZoomClamped);
}

static void testDurationClampsToAtLeastOneFrame() {
    Config c{}; c.durationMs = 0;
    assert(validate(c).config.durationMs >= 16);
}

static void testWorkspaceToFaceMapping() {
    assert(faceOfWorkspace(1, 4) == 0);
    assert(faceOfWorkspace(4, 4) == 3);
    assert(faceOfWorkspace(5, 4) == -1);   // above N: not a face
    assert(faceOfWorkspace(0, 4) == -1);   // special workspaces are negative or zero
    assert(faceOfWorkspace(-99, 4) == -1);
    assert(workspaceOfFace(0) == 1);
    assert(workspaceOfFace(3) == 4);
}

static void testNormalizeFaceWrapsBothDirections() {
    assert(normalizeFace(0, 4) == 0);
    assert(normalizeFace(4, 4) == 0);
    assert(normalizeFace(-1, 4) == 3);
    assert(normalizeFace(-5, 4) == 3);
    assert(normalizeFace(9, 4) == 1);
}

static void testEasingEndpointsAreExact() {
    assert(easeOutCubic(0.f) == 0.f);
    assert(easeOutCubic(1.f) == 1.f);
}

static void testEasingIsMonotonicAndFrontLoaded() {
    float prev = -1.f;
    for (int i = 0; i <= 10; ++i) {
        const float t = static_cast<float>(i) / 10.f;
        const float e = easeOutCubic(t);
        assert(e > prev);
        prev = e;
    }
    assert(easeOutCubic(0.5f) > 0.5f);   // ease-out covers most ground early
}

static void testRotateStartsAtTheOriginFaceAngle() {
    CubeState s(validate(Config{}).config);
    assert(s.startRotate(0, 1, 1000.0));
    const Frame f = s.update(1000.0);
    assert(approxEq(f.angle, 0.f));
    assert(approxEq(f.zoom, 1.f));
}

static void testRotateEndsExactlyOnTheTargetAngle() {
    const float step = 2.f * PI / 4.f;
    CubeState s(validate(Config{}).config);
    s.startRotate(0, 1, 1000.0);
    const Frame f = s.update(1000.0 + 300.0);
    assert(f.angle == -step);            // exact, not approximate
    assert(s.phase() == Phase::Idle);
}

static void testRotatePastTheEndDoesNotOvershoot() {
    const float step = 2.f * PI / 4.f;
    CubeState s(validate(Config{}).config);
    s.startRotate(0, 1, 1000.0);
    const Frame f = s.update(1000.0 + 99999.0);
    assert(f.angle == -step);
}

static void testPrevFromFaceZeroTakesOneStepNotThree() {
    const float step = 2.f * PI / 4.f;
    CubeState s(validate(Config{}).config);
    s.startRotate(0, -1, 0.0);
    const Frame f = s.update(300.0);
    assert(f.angle == step);             // one step forward, not -3 steps
    assert(s.frontFace() == 3);
}

static void testRotationCommitsTheTargetFaceExactlyOnce() {
    CubeState s(validate(Config{}).config);
    s.startRotate(0, 1, 0.0);
    assert(s.takeCommit() == -1);        // nothing to commit mid-flight
    s.update(300.0);
    assert(s.takeCommit() == 1);
    assert(s.takeCommit() == -1);        // already taken
}

static void testRotateIsIgnoredWhileAlreadyAnimating() {
    CubeState s(validate(Config{}).config);
    assert(s.startRotate(0, 1, 0.0));
    assert(!s.startRotate(1, 1, 100.0));  // dropped, not queued
    s.update(300.0);
    assert(s.takeCommit() == 1);
}

static void testIdleStateIsInactiveAndSquare() {
    CubeState s(validate(Config{}).config);
    assert(!s.active());
    const Frame f = s.update(12345.0);
    assert(approxEq(f.angle, 0.f) && approxEq(f.zoom, 1.f));
}

static void testDragZoomsOutAndReachesTheConfiguredZoomExactly() {
    CubeState s(validate(Config{}).config);
    assert(s.startDrag(0, 0.0));
    assert(approxEq(s.update(0.0).zoom, 1.f));
    const Frame f = s.update(300.0);
    assert(f.zoom == 0.6f);
    assert(s.phase() == Phase::Dragging);   // stays dragging; zoom just finished easing
}

static void testDragDeltaTurnsTheCube() {
    CubeState s(validate(Config{}).config);
    s.startDrag(0, 0.0);
    s.update(300.0);
    s.addDragDelta(500.f);                  // one face worth at sensitivity 1.0
    assert(approxEq(s.update(400.0).angle, s.step()));
}

static void testDragDeltaIsIgnoredWhenNotDragging() {
    CubeState s(validate(Config{}).config);
    s.addDragDelta(500.f);
    assert(approxEq(s.update(0.0).angle, 0.f));
}

static void testReleaseSnapsToTheNearerFace() {
    CubeState s(validate(Config{}).config);
    s.startDrag(0, 0.0);
    s.update(300.0);
    s.addDragDelta(-300.f);                 // 0.6 of a step toward face 1
    s.release(300.0);
    const Frame f = s.update(600.0);
    assert(f.angle == -s.step());
    assert(f.zoom == 1.f);
    assert(s.phase() == Phase::Idle);
    assert(s.takeCommit() == 1);
}

static void testReleaseSnapsBackWhenUnderHalfway() {
    CubeState s(validate(Config{}).config);
    s.startDrag(0, 0.0);
    s.update(300.0);
    s.addDragDelta(-200.f);                 // 0.4 of a step: not far enough
    s.release(300.0);
    s.update(600.0);
    assert(s.takeCommit() == 0);
}

static void testExactHalfwaySnapIsDeterministic() {
    CubeState s(validate(Config{}).config);
    s.startDrag(0, 0.0);
    s.update(300.0);
    s.addDragDelta(-250.f);                 // exactly half a step
    s.release(300.0);
    s.update(600.0);
    // lround rounds half away from zero, so a half-step lands on the new face.
    assert(s.takeCommit() == 1);
}

static void testFreeSpinAcrossManyTurnsStillLandsOnACorrectFace() {
    CubeState s(validate(Config{}).config);
    s.startDrag(0, 0.0);
    s.update(300.0);
    s.addDragDelta(-500.f * 9.f);           // nine faces: two full turns plus one
    s.release(300.0);
    const Frame f = s.update(600.0);
    const int landed = s.takeCommit();
    assert(landed >= 0 && landed < 4);
    assert(landed == 1);
    // release() snaps to the raw lround target, not the nearestFace()-normalized one:
    // normalizing here would unwind the spin instead of landing where the drag pointed.
    assert(f.angle == -9.f * s.step());
}

static void testAbortReturnsToTheOriginFaceAndCommitsNothing() {
    CubeState s(validate(Config{}).config);
    s.startDrag(2, 0.0);
    s.update(300.0);
    s.addDragDelta(-700.f);                 // wandered well past face 3
    s.abort(300.0);
    const Frame f = s.update(600.0);
    assert(f.angle == -2.f * s.step());     // back where it started
    assert(f.zoom == 1.f);
    assert(s.phase() == Phase::Idle);
    assert(s.takeCommit() == -1);           // no workspace change
}

static void testMinimumDragZoomKeepsTheCubeInsideTheFarPlane() {
    // At the bottom of the legal drag_zoom range the eye moves back; the far plane
    // must track it or the whole prism clips away. Checked at every legal (faces, fov).
    for (int n = 3; n <= 16; ++n)
        for (float fovDeg = 20.f; fovDeg <= 120.f; fovDeg += 10.f) {
            const Geometry g = makeGeometry(n, 1920.f, 1080.f, fovDeg * PI / 180.f);
            for (int corner = 0; corner < 4; ++corner) {
                assert(projectFaceCorner(g, 0, 0.f, 0.2f, corner).z <= 1.f);
                assert(projectFaceCorner(g, n / 2, 0.f, 0.2f, corner).z <= 1.f);
            }
        }
}

static void testDragIsRefusedWhileRotating() {
    CubeState s(validate(Config{}).config);
    s.startRotate(0, 1, 0.0);
    assert(!s.startDrag(0, 100.0));
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
    testDefaultConfigSurvivesValidation();
    testFacesClampToLegalRange();
    testTwoFacesIsRejectedAsDegenerate();
    testFovClampsToLegalRange();
    testDragZoomClampsToUsableRange();
    testDurationClampsToAtLeastOneFrame();
    testWorkspaceToFaceMapping();
    testNormalizeFaceWrapsBothDirections();
    testEasingEndpointsAreExact();
    testEasingIsMonotonicAndFrontLoaded();
    testRotateStartsAtTheOriginFaceAngle();
    testRotateEndsExactlyOnTheTargetAngle();
    testRotatePastTheEndDoesNotOvershoot();
    testPrevFromFaceZeroTakesOneStepNotThree();
    testRotationCommitsTheTargetFaceExactlyOnce();
    testRotateIsIgnoredWhileAlreadyAnimating();
    testIdleStateIsInactiveAndSquare();
    testDragZoomsOutAndReachesTheConfiguredZoomExactly();
    testDragDeltaTurnsTheCube();
    testDragDeltaIsIgnoredWhenNotDragging();
    testReleaseSnapsToTheNearerFace();
    testReleaseSnapsBackWhenUnderHalfway();
    testExactHalfwaySnapIsDeterministic();
    testFreeSpinAcrossManyTurnsStillLandsOnACorrectFace();
    testAbortReturnsToTheOriginFaceAndCommitsNothing();
    testMinimumDragZoomKeepsTheCubeInsideTheFarPlane();
    testDragIsRefusedWhileRotating();
    std::printf("all tests passed\n");
    return 0;
}
