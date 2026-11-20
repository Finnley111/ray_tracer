#pragma once
// Spectra as functions of wavelength (nm) + hero-wavelength sampling.
// See plan/PLAN.md § Spectra, § Wavelength sampling.
#include "color.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

// ponytail: std::function per eval; swap for a tagged struct if profiling (RT-013/014) shows it hot.
using Spectrum = std::function<float(float)>;

inline Spectrum constant(float v) {
    return [v](float) { return v; };
}

// Planck's law, normalised so the peak (Wien) equals 1.
inline Spectrum blackbody(float kelvin) {
    auto planck = [kelvin](double l_nm) {
        const double h = 6.62607015e-34, c = 2.99792458e8, k = 1.380649e-23;
        double l = l_nm * 1e-9;
        return 2 * h * c * c / (std::pow(l, 5) * (std::exp(h * c / (l * k * kelvin)) - 1));
    };
    double peak = planck(2.897771955e-3 / kelvin * 1e9);
    return [planck, peak](float l) { return float(planck(l) / peak); };
}

inline Spectrum gaussian(float mu, float sigma, float scale = 1.0f) {
    return [=](float l) { float t = (l - mu) / sigma; return scale * std::exp(-0.5f * t * t); };
}

// Piecewise-linear through (lambda, value) pairs sorted by lambda; clamped at the ends.
inline Spectrum sampled(std::vector<std::pair<float, float>> pts) {
    return [pts = std::move(pts)](float l) {
        if (l <= pts.front().first) return pts.front().second;
        if (l >= pts.back().first) return pts.back().second;
        auto it = std::upper_bound(pts.begin(), pts.end(), l,
                                   [](float v, const auto& p) { return v < p.first; });
        auto [l1, v1] = *it;
        auto [l0, v0] = *(it - 1);
        return v0 + (v1 - v0) * (l - l0) / (l1 - l0);
    };
}

// CIE standard illuminant D65, 380-780 nm at 10 nm, normalised to 1 at 560 nm.
inline Spectrum d65() {
    static const float t[] = {
        49.9755f, 54.6482f, 82.7549f, 91.486f,  93.4318f, 86.6823f, 104.865f, 117.008f, 117.812f,
        114.861f, 115.923f, 108.811f, 109.354f, 107.802f, 104.79f,  107.689f, 104.405f, 104.046f,
        100.0f,   96.3342f, 95.788f,  88.6856f, 90.0062f, 89.5991f, 87.6987f, 83.2886f, 83.6992f,
        80.0268f, 80.2146f, 82.2778f, 78.2842f, 69.7213f, 71.6091f, 74.349f,  61.604f,  69.8856f,
        75.087f,  63.5927f, 46.4182f, 66.8054f, 63.3828f};
    std::vector<std::pair<float, float>> pts;
    for (int i = 0; i < 41; ++i) pts.push_back({380.0f + 10.0f * i, t[i] / 100.0f});
    return sampled(std::move(pts));
}

// ---- Index of refraction (lambda in nm; coefficients in micrometres) ----

// Cauchy: n = A + B / lambda^2.
inline Spectrum cauchy(float A, float B) {
    return [=](float l) { float u = l * 1e-3f; return A + B / (u * u); };
}

// Sellmeier: n^2 = 1 + sum B_i lambda^2 / (lambda^2 - C_i), C in um^2.
inline Spectrum sellmeier(float B1, float B2, float B3, float C1, float C2, float C3) {
    return [=](float l) {
        float u2 = l * l * 1e-6f;
        return std::sqrt(1 + B1 * u2 / (u2 - C1) + B2 * u2 / (u2 - C2) + B3 * u2 / (u2 - C3));
    };
}

// Schott catalogue coefficients.
inline Spectrum glass_bk7() { return sellmeier(1.03961212f, 0.231792344f, 1.01046945f, 0.00600069867f, 0.0200179144f, 103.560653f); }
inline Spectrum glass_sf11() { return sellmeier(1.73759695f, 0.313747346f, 1.89878101f, 0.013188707f, 0.0623068142f, 155.23629f); }

// ---- Hero wavelength sampling (Wilkie et al. 2014) ----

constexpr int N_LAMBDA = 4;

// Spectral values at the N_LAMBDA wavelengths a path carries.
struct SampledSpectrum {
    std::array<float, N_LAMBDA> v{};
    float& operator[](int i) { return v[i]; }
    float operator[](int i) const { return v[i]; }
    SampledSpectrum& operator+=(const SampledSpectrum& o) { for (int i = 0; i < N_LAMBDA; ++i) v[i] += o.v[i]; return *this; }
    SampledSpectrum& operator*=(const SampledSpectrum& o) { for (int i = 0; i < N_LAMBDA; ++i) v[i] *= o.v[i]; return *this; }
    SampledSpectrum& operator*=(float s) { for (float& x : v) x *= s; return *this; }
    friend SampledSpectrum operator+(SampledSpectrum a, const SampledSpectrum& b) { return a += b; }
    friend SampledSpectrum operator*(SampledSpectrum a, const SampledSpectrum& b) { return a *= b; }
    friend SampledSpectrum operator*(SampledSpectrum a, float s) { return a *= s; }
};

struct SampledWavelengths {
    std::array<float, N_LAMBDA> lambda;
    std::array<float, N_LAMBDA> pdf;

    // u in [0,1): hero uniform over the range, companions rotated by range/N and wrapped.
    static SampledWavelengths sample_uniform(float u) {
        const float range = LAMBDA_MAX - LAMBDA_MIN;
        SampledWavelengths w;
        for (int i = 0; i < N_LAMBDA; ++i) {
            float l = LAMBDA_MIN + range * (u + float(i) / N_LAMBDA);
            if (l >= LAMBDA_MAX) l -= range;
            w.lambda[i] = l;
            w.pdf[i] = 1.0f / range;
        }
        return w;
    }

    SampledSpectrum eval(const Spectrum& s) const {
        SampledSpectrum r;
        for (int i = 0; i < N_LAMBDA; ++i) r[i] = s(lambda[i]);
        return r;
    }

    bool secondary_terminated() const { return pdf[1] == 0; }

    // After a wavelength-dependent direction choice (dispersion) only the hero can
    // follow the path. Zero the companions' pdf and fold their share into the hero so
    // to_xyz stays unbiased. Idempotent.
    void terminate_secondary() {
        if (secondary_terminated()) return;
        for (int i = 1; i < N_LAMBDA; ++i) pdf[i] = 0;
        pdf[0] /= N_LAMBDA;
    }
};

// One-sample MC estimate of XYZ for radiance L at wavelengths w.
// Normalised like spectrum_to_xyz: constant 1 -> Y = 1 in expectation.
inline XYZ to_xyz(const SampledSpectrum& L, const SampledWavelengths& w) {
    float x = 0, y = 0, z = 0;
    for (int i = 0; i < N_LAMBDA; ++i) {
        if (w.pdf[i] == 0) continue;
        XYZ c = cie_cmf(w.lambda[i]);
        float k = L[i] / w.pdf[i];
        x += k * c[0]; y += k * c[1]; z += k * c[2];
    }
    float n = N_LAMBDA * cie_y_integral();
    return {x / n, y / n, z / n};
}
