// Small DSP helpers shared by the core audio code (click, voice, pitch tracker). No instrument DSP here.
#pragma once
#include <cmath>
#include <algorithm>

namespace wr::dsp {
constexpr float kPi = 3.14159265358979f;
constexpr float k2Pi = 6.28318530717959f;
inline float clampf (float x, float a, float b) { return std::min (b, std::max (a, x)); }
struct Biquad {   // RBJ biquad (coefficient args named c0.. to stay clear of <termios.h> B0/B1 macros)
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float process (float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void norm (float a0, float c0, float c1, float c2, float d1, float d2) { b0 = c0 / a0; b1 = c1 / a0; b2 = c2 / a0; a1 = d1 / a0; a2 = d2 / a0; }
    void setLP (float fs, float f, float q) { float w = k2Pi * clampf (f, 10, fs * 0.45f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q); norm (1 + al, (1 - c) / 2, 1 - c, (1 - c) / 2, -2 * c, 1 - al); }
    void setHP (float fs, float f, float q) { float w = k2Pi * clampf (f, 10, fs * 0.45f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q); norm (1 + al, (1 + c) / 2, -(1 + c), (1 + c) / 2, -2 * c, 1 - al); }
};
} // namespace wr::dsp
