// The cylindrical panorama projection of the original engine (pages of 2560 x 1024 are shown through it).
// exe:00445a70 (setup from the field of view), exe:00443f30 -> exe:0040cf00 + exe:0041a03c (render), exe:00403470 (view point -> page point).
// Analysis: analysis/notes/projection.md.  No SDL.
#pragma once
#include "engine/common.h"
#include <vector>

namespace biosys {

constexpr int kPanoW = 2560, kPanoH = 1024;   // the pages that are projected (header 0xa00 x 0x400)
constexpr int kViewW = 640, kViewH = 368;

// The camera of the last render() in the engine's own model (exe:0040cf00), for presentations that draw the same cylinder at
// a higher resolution (src/engine/panomesh.h). yaw: page x of the view centre in pixels, an integer in (-1280, 1280] (the original
// wraps it once); pitchDeg: after the clamp, exact (the original truncates it to 1/1024 degree); fovDeg: whole degrees; focal: the
// distance of the image plane in view pixels (max(320 / tan(fov / 2), 184 / tan(0.8985))).
struct PanoCamera {
    double yaw = 0, pitchDeg = 0, fovDeg = 75, focal = 0;
};

class Projection {
public:
    Projection() { setFov(75.0); }
    // exe:00445a70: the fields at main window +0x2e8..0x2f8 from the field of view (degrees; the constructor of the window passes 75.0)
    void setFov(double fov);
    double fov() const { return fov_; }
    int yawOffset() const { return offset_; }      // +0x2f0 = ftol(tan(fov/2) * 407.4)
    // the zoom loops set +0x2f0 back to the value of fov 75 after every setFov
    void setYawOffset(int v) { offset_ = v; }
    int fov1024() const { return fov1024_; }       // +0x2f4 = ftol(fov) << 10
    int pitchGain() const { return gain_; }        // +0x2f8

    // exe:00443f30 projected branch: scroll position (x, y) of the main window -> 640 x 368 indices in dst (row pitch 640).
    // surface: 2560 x 1024 indices. Remembers yaw / pitch the way the original keeps them in globals (0x499110 / 0x499114).
    void render(const u8* surface, int x, int y, u8* dst);
    // exe:00403470 for a point of the view (0..639, 0..367) using the remembered yaw / pitch and the current fov.
    void inverse(int sx, int sy, int& px, int& py) const;
    // Debug aid (hotspot outlines): the view point that shows page point (u, v) with the remembered state; false if it is behind the camera.
    // Not in the original (closed-form inverse of inverse(), for the unclamped plane).
    bool forward(double u, double v, int& sx, int& sy) const;
    // the remembered values (for tests): yaw wrapped into (-1280, 1280], pitch in 1/1024 degrees after the clamp
    int lastYaw() const { return yaw_; }
    int lastPitch() const { return pitch_; }
    const PanoCamera& lastCamera() const { return cam_; }   // of the last render() (not of track())
    // Remembers yaw / pitch for scroll (x, y) as a render would, without drawing (the port's render happens only per frame).
    void track(int x, int y);

private:
    void params(int x, int y, int& yaw, int& pitchIn) const;
    double fov_ = 75.0;
    int offset_ = 0, fov1024_ = 0, gain_ = 0;
    int yaw_ = 0, pitch_ = 0;
    PanoCamera cam_;
};

} // namespace biosys
