#pragma once
#include "camera.h"
#include "geometry.h"
#include "image.h"
#include "material.h"
#include "spectrum.h"
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

constexpr int MAX_DEPTH = 64;
constexpr int RR_START = 3;

// Unidirectional path tracer, BSDF sampling only (NEE + MIS in RT-016).
// Returns radiance at the path's 4 wavelengths.
inline SampledSpectrum trace(const Scene& scene, const std::vector<Material>& mats, const Spectrum& background,
                             Ray ray, const SampledWavelengths& w, Rng& rng) {
    SampledSpectrum L, beta;
    beta.v.fill(1.0f);
    for (int depth = 0; depth < MAX_DEPTH; ++depth) {
        Hit h;
        if (!scene.intersect(ray, 1e-4f, 1e30f, h)) {
            L += beta * w.eval(background);
            break;
        }
        const Material& m = mats[h.mat];
        if (m.emission_scale > 0) L += beta * w.eval(m.emission) * m.emission_scale;

        if (m.kind == Kind::Diffuse) {
            // Lambertian with cosine sampling: f*cos/pdf = albedo.
            beta *= w.eval(m.albedo);
            ray = {h.p, sample_cosine_hemisphere(h.n, rng.next(), rng.next())};
        } else {
            // Perfect specular reflection: weight is the reflectance.
            Vec3 d = normalize(ray.d);
            if (m.kind == Kind::Mirror) {
                beta *= w.eval(m.albedo);
            } else {
                float cos_i = -dot(d, h.n);
                for (int i = 0; i < N_LAMBDA; ++i)
                    beta[i] *= fresnel_conductor(cos_i, m.eta(w.lambda[i]), m.k(w.lambda[i]));
            }
            ray = {h.p, reflect(d, h.n)};
        }

        if (depth >= RR_START) {
            float q = std::max(0.05f, 1 - *std::max_element(beta.v.begin(), beta.v.end()));
            if (rng.next() < q) break;
            beta *= 1 / (1 - q);
        }
    }
    return L;
}

inline void render_row(const Scene& scene, const std::vector<Material>& mats, const Spectrum& background,
                       const Camera& cam, Image& img, int y, int spp, float exposure, Rng& rng) {
    for (int x = 0; x < img.w; ++x) {
        XYZ acc{0, 0, 0};
        for (int s = 0; s < spp; ++s) {
            auto w = SampledWavelengths::sample_uniform(rng.next());
            Ray r = cam.ray((x + rng.next()) / img.w, (y + rng.next()) / img.h);
            XYZ c = to_xyz(trace(scene, mats, background, r, w, rng), w);
            for (int i = 0; i < 3; ++i) acc[i] += c[i];
        }
        RGB c = xyz_to_linear_srgb(acc);
        float k = exposure / spp;
        img.at(x, y) = {c[0] * k, c[1] * k, c[2] * k};
    }
}

// Rows are handed out to threads through an atomic counter. Each row seeds its own
// Rng from (seed, y), so the image is identical whatever the thread count.
inline Image render(const Scene& scene, const std::vector<Material>& mats, const Spectrum& background,
                    const Camera& cam, int W, int H, int spp, float exposure, unsigned seed = 1,
                    unsigned threads = std::thread::hardware_concurrency()) {
    Image img(W, H);
    std::atomic<int> next_row{0};
    auto worker = [&] {
        for (int y; (y = next_row++) < H;) {
            Rng rng(seed * 0x9E3779B9u + unsigned(y));
            render_row(scene, mats, background, cam, img, y, spp, exposure, rng);
        }
    };
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < std::max(1u, threads); ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    return img;
}
