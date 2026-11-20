#pragma once
#include "spectrum.h"
#include "vec.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>

struct Rng {
    std::mt19937 g;
    explicit Rng(unsigned seed) : g(seed) {}
    float next() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(g); }
};

enum class Kind { Diffuse, Mirror, Conductor };

// Any material may also emit (two-sided). Fields used per kind:
//   Diffuse: albedo.  Mirror: albedo = reflectance.  Conductor: eta, k (smooth, Fresnel).
struct Material {
    Spectrum albedo = constant(0.0f);
    Spectrum emission = constant(0.0f);
    float emission_scale = 0.0f;  // 0 = not a light
    Kind kind = Kind::Diffuse;
    Spectrum eta = constant(1.0f), k = constant(0.0f);
};

inline Material mirror(Spectrum r) { return {r, constant(0), 0, Kind::Mirror}; }
inline Material conductor(Spectrum eta, Spectrum k) { return {constant(0), constant(0), 0, Kind::Conductor, eta, k}; }

// Unpolarised Fresnel reflectance of a conductor with complex IOR eta + ik (relative to the
// outside medium), cos_i = cosine of the incident angle. k = 0 gives the dielectric case.
inline float fresnel_conductor(float cos_i, float eta, float k) {
    using C = std::complex<float>;
    cos_i = std::clamp(cos_i, 0.0f, 1.0f);
    C e(eta, k);
    C sin2_t = (1 - cos_i * cos_i) / (e * e);
    C cos_t = std::sqrt(C(1) - sin2_t);
    C r_par = (e * cos_i - cos_t) / (e * cos_i + cos_t);
    C r_perp = (cos_i - e * cos_t) / (cos_i + e * cos_t);
    return (std::norm(r_par) + std::norm(r_perp)) / 2;
}

inline Vec3 reflect(Vec3 d, Vec3 n) { return d - n * (2 * dot(d, n)); }

// Cosine-weighted direction in the hemisphere around unit normal n (pdf = cos/pi).
// Orthonormal basis: Duff et al. 2017, "Building an Orthonormal Basis, Revisited".
inline Vec3 sample_cosine_hemisphere(Vec3 n, float u1, float u2) {
    float r = std::sqrt(u1), phi = 2 * std::numbers::pi_v<float> * u2;
    float sign = std::copysign(1.0f, n.z), a = -1.0f / (sign + n.z), b = n.x * n.y * a;
    Vec3 t{1 + sign * n.x * n.x * a, sign * b, -sign * n.x};
    Vec3 bt{b, sign + n.y * n.y * a, -n.y};
    return t * (r * std::cos(phi)) + bt * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1 - u1));
}
