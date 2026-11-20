#pragma once
#include "spectrum.h"
#include "vec.h"
#include <cmath>
#include <numbers>
#include <random>

struct Rng {
    std::mt19937 g;
    explicit Rng(unsigned seed) : g(seed) {}
    float next() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(g); }
};

// Diffuse reflector that may also emit. Emission is two-sided.
// ponytail: one material kind; mirror/conductor/dielectric arrive in RT-010/011.
struct Material {
    Spectrum albedo = constant(0.0f);
    Spectrum emission = constant(0.0f);
    float emission_scale = 0.0f;  // 0 = not a light
};

// Cosine-weighted direction in the hemisphere around unit normal n (pdf = cos/pi).
// Orthonormal basis: Duff et al. 2017, "Building an Orthonormal Basis, Revisited".
inline Vec3 sample_cosine_hemisphere(Vec3 n, float u1, float u2) {
    float r = std::sqrt(u1), phi = 2 * std::numbers::pi_v<float> * u2;
    float sign = std::copysign(1.0f, n.z), a = -1.0f / (sign + n.z), b = n.x * n.y * a;
    Vec3 t{1 + sign * n.x * n.x * a, sign * b, -sign * n.x};
    Vec3 bt{b, sign + n.y * n.y * a, -n.y};
    return t * (r * std::cos(phi)) + bt * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1 - u1));
}
