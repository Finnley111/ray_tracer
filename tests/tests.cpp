#include "color.h"
#include "image.h"
#include "spectrum.h"
#include "geometry.h"
#include "camera.h"
#include "integrator.h"
#include "metals.h"
#include <random>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>

static bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

int main() {
    // RT-002: y-bar peaks ~1 at 555 nm
    assert(near(cie_cmf(555.0f)[1], 1.0f, 0.02f));

    // RT-002: equal-energy illuminant -> chromaticity (1/3, 1/3), Y normalised to 1
    XYZ e = spectrum_to_xyz([](float) { return 1.0f; });
    float s = e[0] + e[1] + e[2];
    assert(near(e[1], 1.0f, 1e-4f));
    assert(near(e[0] / s, 1.0f / 3, 0.01f) && near(e[1] / s, 1.0f / 3, 0.01f));

    // RT-002: sRGB transfer function endpoints + clamping
    assert(srgb_encode(0) == 0 && near(srgb_encode(1), 1, 1e-6f) && srgb_encode(2) == srgb_encode(1));
    assert(near(srgb_encode(0.5f), 0.7354f, 1e-3f));

    // RT-001: PPM header + size
    Image img(3, 2);
    img.at(2, 1) = {1, 0, 0};
    const char* p = "rt_tests_tmp.ppm";
    assert(write_ppm(img, p));
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    assert(f.tellg() == std::streamoff(11 + 3 * 2 * 3));  // "P6\n3 2\n255\n" + pixels
    std::remove(p);

    // RT-003: spectrum types
    assert(near(blackbody(5000)(2.897771955e-3f / 5000 * 1e9f), 1.0f, 1e-4f));
    assert(blackbody(5000)(400) < 1 && blackbody(5000)(700) < 1);
    auto lin = sampled({{400, 0}, {500, 1}});
    assert(lin(300) == 0 && near(lin(450), 0.5f, 1e-6f) && lin(900) == 1);
    assert(near(gaussian(550, 10)(550), 1, 1e-6f));
    XYZ d = spectrum_to_xyz(d65());
    float ds = d[0] + d[1] + d[2];
    std::printf("D65 xy = (%.4f, %.4f)\n", d[0] / ds, d[1] / ds);
    assert(near(d[0] / ds, 0.3127f, 0.005f) && near(d[1] / ds, 0.3290f, 0.005f));

    // RT-004: wavelengths stay in range and are spread by range/4
    for (float u : {0.0f, 0.3f, 0.999f}) {
        auto w = SampledWavelengths::sample_uniform(u);
        for (float l : w.lambda) assert(l >= LAMBDA_MIN && l < LAMBDA_MAX);
    }
    // RT-004: MC estimate of D65 matches the deterministic integral to <1%
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> U(0, 1);
        Spectrum s = d65();
        const int N = 200000;
        double acc[3] = {0, 0, 0};
        for (int i = 0; i < N; ++i) {
            auto w = SampledWavelengths::sample_uniform(U(rng));
            XYZ c = to_xyz(w.eval(s), w);
            for (int k = 0; k < 3; ++k) acc[k] += c[k] / N;
        }
        for (int k = 0; k < 3; ++k) assert(std::fabs(acc[k] - d[k]) / d[k] < 0.01);
    }

    // RT-005: sphere hit from outside, miss, and from inside
    {
        Sphere sp{{0, 0, -5}, 1, 3};
        Hit h;
        assert(sp.intersect({{0, 0, 0}, {0, 0, -1}}, 1e-4f, 1e30f, h));
        assert(near(h.t, 4, 1e-5f) && h.front && near(h.n.z, 1, 1e-5f) && h.mat == 3);
        assert(!sp.intersect({{0, 0, 0}, {0, 1, 0}}, 1e-4f, 1e30f, h));
        assert(!sp.intersect({{0, 0, 0}, {0, 0, -1}}, 1e-4f, 3.0f, h));  // beyond tmax
        assert(sp.intersect({{0, 0, -5}, {1, 0, 0}}, 1e-4f, 1e30f, h));  // from inside
        assert(near(h.t, 1, 1e-5f) && !h.front && near(h.n.x, -1, 1e-5f));
    }
    // RT-005: quad hit inside bounds, miss outside, parallel ray misses
    {
        Quad q{{-1, -1, -2}, {2, 0, 0}, {0, 2, 0}, 0};  // u x v = +z, faces the camera
        Hit h;
        assert(q.intersect({{0.5f, 0.5f, 0}, {0, 0, -1}}, 1e-4f, 1e30f, h));
        assert(near(h.t, 2, 1e-5f) && h.front && near(h.n.z, 1, 1e-5f));
        assert(!q.intersect({{1.5f, 0, 0}, {0, 0, -1}}, 1e-4f, 1e30f, h));
        assert(!q.intersect({{0, 0, 0}, {1, 0, 0}}, 1e-4f, 1e30f, h));
    }
    // RT-005: scene returns the closest hit regardless of list order
    {
        Scene sc;
        sc.quads = {{{-1, -1, -2}, {2, 0, 0}, {0, 2, 0}, 1}};
        sc.spheres = {{{0, 0, -10}, 1, 2}};
        Hit h;
        assert(sc.intersect({{0, 0, 0}, {0, 0, -1}}, 1e-4f, 1e30f, h) && h.mat == 1);
    }
    // RT-006: centre ray points at look-at; top-left ray goes up and left
    {
        Camera cam({0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 90, 2.0f);
        Ray c = cam.ray(0.5f, 0.5f), tl = cam.ray(0, 0);
        assert(near(c.d.z, -1, 1e-5f));
        assert(tl.d.x < 0 && tl.d.y > 0 && near(tl.d.y / -tl.d.z, 1, 1e-5f));  // 90 deg vfov
    }

    // RT-007: cosine samples are unit length and in the hemisphere, mean cos = 2/3
    {
        Rng rng(3);
        Vec3 n = normalize(Vec3{0.3f, -0.8f, 0.5f});
        double mc = 0;
        for (int i = 0; i < 100000; ++i) {
            Vec3 d = sample_cosine_hemisphere(n, rng.next(), rng.next());
            assert(near(length(d), 1, 1e-4f) && dot(d, n) >= -1e-5f);
            mc += dot(d, n) / 100000;
        }
        assert(near(float(mc), 2.0f / 3, 0.01f));
    }
    // RT-007: furnace tests. Mean radiance per wavelength must match the analytic answer.
    auto furnace = [](const Scene& sc, const std::vector<Material>& mats, const Spectrum& bg, Ray r) {
        Rng rng(11);
        double sum = 0;
        const int N = 100000;
        for (int i = 0; i < N; ++i) {
            auto w = SampledWavelengths::sample_uniform(rng.next());
            SampledSpectrum L = trace(sc, mats, bg, r, w, rng);
            sum += (L[0] + L[1] + L[2] + L[3]) / (4.0 * N);
        }
        return float(sum);
    };
    {
        // White sphere (albedo 1) under uniform sky of 1 -> 1. Albedo 0.5 -> 0.5.
        Scene sc;
        sc.spheres = {{{0, 0, -3}, 1, 0}};
        Ray r{{0, 0, 0}, {0, 0, -1}};
        float a = furnace(sc, {{constant(1.0f)}}, constant(1.0f), r);
        float b = furnace(sc, {{constant(0.5f)}}, constant(1.0f), r);
        std::printf("furnace outside: %.4f (1), %.4f (0.5)\n", a, b);
        assert(near(a, 1, 0.01f) && near(b, 0.5f, 0.01f));
    }
    {
        // Inside a closed sphere: albedo 0.8, emission 0.2 -> L = e / (1 - a) = 1.
        // Exercises many bounces and Russian roulette.
        Scene sc;
        sc.spheres = {{{0, 0, 0}, 1, 0}};
        float c = furnace(sc, {{constant(0.8f), constant(1.0f), 0.2f}}, constant(0.0f), {{0, 0, 0}, {1, 0, 0}});
        std::printf("furnace inside: %.4f (1)\n", c);
        assert(near(c, 1, 0.02f));
    }

    // RT-014: image is bit-identical regardless of thread count
    {
        Scene sc;
        sc.spheres = {{{0, 0, -3}, 1, 0}};
        std::vector<Material> mats = {{constant(0.5f)}};
        Camera cam({0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 60, 1);
        Image a = render(sc, mats, d65(), cam, 16, 16, 8, 1, 5, 1);
        Image b = render(sc, mats, d65(), cam, 16, 16, 8, 1, 5, 4);
        assert(a.px == b.px);
    }

    // RT-010: Fresnel. Glass at normal incidence 4%; conductor matches the closed form; grazing -> 1.
    {
        assert(near(fresnel_conductor(1, 1.5f, 0), 0.04f, 1e-5f));
        float n = 0.43f, k = 2.455f;  // gold @ 549 nm
        assert(near(fresnel_conductor(1, n, k), ((n - 1) * (n - 1) + k * k) / ((n + 1) * (n + 1) + k * k), 1e-5f));
        assert(near(fresnel_conductor(0, n, k), 1, 1e-4f));
    }
    // RT-010: normal-incidence reflectance under D65 gives the right metal colours.
    {
        auto colour = [](Spectrum eta, Spectrum k) {
            Spectrum d = d65();
            return xyz_to_linear_srgb(spectrum_to_xyz([&](float l) { return d(l) * fresnel_conductor(1, eta(l), k(l)); }));
        };
        RGB au = colour(au_eta(), au_k()), cu = colour(cu_eta(), cu_k()), ag = colour(ag_eta(), ag_k());
        std::printf("Au %.3f %.3f %.3f | Cu %.3f %.3f %.3f | Ag %.3f %.3f %.3f\n",
                    au[0], au[1], au[2], cu[0], cu[1], cu[2], ag[0], ag[1], ag[2]);
        assert(au[0] > au[1] && au[1] > au[2] && au[0] > 1.5f * au[2]);       // gold: yellow
        assert(cu[0] > 1.3f * cu[1] && cu[1] > cu[2]);                          // copper: orange-red
        assert(near(ag[0] / ag[2], 1, 0.15f) && ag[1] > 0.85f);                // silver: bright, neutral
    }
    {
        // Mirror furnace: a 100% mirror sphere under a sky of 1 -> 1.
        Scene sc;
        sc.spheres = {{{0, 0, -3}, 1, 0}};
        float m = furnace(sc, {mirror(constant(1.0f))}, constant(1.0f), {{0, 0, 0}, normalize(Vec3{0.3f, 0.2f, -1})});
        assert(near(m, 1, 1e-4f));
    }

    std::printf("OK\n");
}
