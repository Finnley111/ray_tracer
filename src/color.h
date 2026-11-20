#pragma once
// CIE 1931 2° colour matching + XYZ -> sRGB. See plan/PLAN.md § Colour matching.
#include <array>
#include <cmath>

using RGB = std::array<float, 3>;
using XYZ = std::array<float, 3>;

constexpr float LAMBDA_MIN = 380.0f, LAMBDA_MAX = 780.0f;

// Piecewise Gaussian: different widths left/right of the mean.
inline float pgauss(float x, float mu, float s1, float s2) {
    float t = (x - mu) / (x < mu ? s1 : s2);
    return std::exp(-0.5f * t * t);
}

// Wyman, Sloan, Shirley 2013, "Simple Analytic Approximations to the CIE XYZ
// Color Matching Functions" — multi-lobe fit. lambda in nm.
inline XYZ cie_cmf(float l) {
    return {
        1.056f * pgauss(l, 599.8f, 37.9f, 31.0f) + 0.362f * pgauss(l, 442.0f, 16.0f, 26.7f)
            - 0.065f * pgauss(l, 501.1f, 20.4f, 26.2f),
        0.821f * pgauss(l, 568.8f, 46.9f, 40.5f) + 0.286f * pgauss(l, 530.9f, 16.3f, 31.1f),
        1.217f * pgauss(l, 437.0f, 11.8f, 36.0f) + 0.681f * pgauss(l, 459.0f, 26.0f, 13.8f),
    };
}

// Integral of y-bar over the visible range, used to normalise so that a
// spectrum of constant 1 maps to Y = 1. (~106.86 for the fit.)
inline float cie_y_integral() {
    static const float v = [] {
        double s = 0;
        for (float l = LAMBDA_MIN; l <= LAMBDA_MAX; l += 1.0f) s += cie_cmf(l)[1];
        return float(s);
    }();
    return v;
}

// Deterministic integration of a spectrum f(lambda) to XYZ, 1 nm steps,
// normalised so constant 1 -> Y = 1. Reference/tests; the renderer uses MC (RT-004).
template <class F>
XYZ spectrum_to_xyz(F&& f) {
    double x = 0, y = 0, z = 0;
    for (float l = LAMBDA_MIN; l <= LAMBDA_MAX; l += 1.0f) {
        XYZ c = cie_cmf(l);
        double v = f(l);
        x += v * c[0]; y += v * c[1]; z += v * c[2];
    }
    double n = cie_y_integral();
    return {float(x / n), float(y / n), float(z / n)};
}

inline RGB xyz_to_linear_srgb(const XYZ& c) {
    return {
        3.2404542f * c[0] - 1.5371385f * c[1] - 0.4985314f * c[2],
        -0.9692660f * c[0] + 1.8760108f * c[1] + 0.0415560f * c[2],
        0.0556434f * c[0] - 0.2040259f * c[1] + 1.0572252f * c[2],
    };
}

// Linear -> sRGB-encoded, input clamped to [0,1].
inline float srgb_encode(float v) {
    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}
