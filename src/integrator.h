#pragma once
#include "camera.h"
#include "geometry.h"
#include "image.h"
#include "material.h"
#include "spectrum.h"
#include <algorithm>
#include <atomic>
#include <numbers>
#include <thread>
#include <vector>

constexpr int MAX_DEPTH = 64;
constexpr int RR_START = 3;

// Sample the next direction at a hit and update the path throughput. Shared by the
// camera path tracer and the light tracer. May terminate secondary wavelengths (dispersion).
inline void scatter(const Material& m, const Hit& h, Ray& ray, SampledWavelengths& w, SampledSpectrum& beta, Rng& rng) {
    if (m.kind == Kind::Diffuse) {
        // Lambertian with cosine sampling: f*cos/pdf = albedo.
        beta *= w.eval(m.albedo);
        ray = {h.p, sample_cosine_hemisphere(h.n, rng.next(), rng.next())};
        return;
    }
    Vec3 d = normalize(ray.d);
    if (m.kind == Kind::Dielectric) {
        // IOR varies across the 4 wavelengths -> only the hero can follow one direction.
        SampledSpectrum ior = w.eval(m.eta);
        if (!w.secondary_terminated() && (ior[0] != ior[1] || ior[0] != ior[2] || ior[0] != ior[3])) {
            w.terminate_secondary();
            for (int i = 1; i < N_LAMBDA; ++i) beta[i] = 0;
        }
        // Same direction for all surviving wavelengths, so use the hero's IOR.
        float eta = h.front ? ior[0] : 1 / ior[0], cos_i = -dot(d, h.n);
        float F = fresnel_conductor(cos_i, eta, 0);  // 1 under total internal reflection
        // Pick reflection with probability F: weight F/F = 1 either way.
        ray = {h.p, rng.next() < F ? reflect(d, h.n) : refract(d, h.n, eta)};
        return;
    }
    // Perfect specular reflection: weight is the reflectance.
    if (m.kind == Kind::Mirror) {
        beta *= w.eval(m.albedo);
    } else {
        float cos_i = -dot(d, h.n);
        for (int i = 0; i < N_LAMBDA; ++i) beta[i] *= fresnel_conductor(cos_i, m.eta(w.lambda[i]), m.k(w.lambda[i]));
    }
    ray = {h.p, reflect(d, h.n)};
}

// Russian roulette. Returns false if the path dies.
inline bool roulette(int depth, SampledSpectrum& beta, Rng& rng) {
    if (depth < RR_START) return true;
    float q = std::max(0.05f, 1 - *std::max_element(beta.v.begin(), beta.v.end()));
    if (rng.next() < q) return false;
    beta *= 1 / (1 - q);
    return true;
}

// Unidirectional path tracer, BSDF sampling only (NEE + MIS in RT-016).
// Returns radiance at the path's 4 wavelengths. May terminate the secondary
// wavelengths in w (dispersion), so pass the same w on to to_xyz.
// skip_diffuse_first: stop after the first hit if it is diffuse (the light tracer
// covers those paths in render_hybrid); emission seen directly is still counted.
inline SampledSpectrum trace(const Scene& scene, const std::vector<Material>& mats, const Spectrum& background,
                             Ray ray, SampledWavelengths& w, Rng& rng, bool skip_diffuse_first = false) {
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
        if (skip_diffuse_first && depth == 0 && m.kind == Kind::Diffuse) break;
        scatter(m, h, ray, w, beta, rng);
        if (!roulette(depth, beta, rng)) break;
    }
    return L;
}

inline void render_row(const Scene& scene, const std::vector<Material>& mats, const Spectrum& background,
                       const Camera& cam, Image& img, int y, int spp, float exposure, Rng& rng,
                       bool skip_diffuse_first = false) {
    for (int x = 0; x < img.w; ++x) {
        XYZ acc{0, 0, 0};
        for (int s = 0; s < spp; ++s) {
            auto w = SampledWavelengths::sample_uniform(rng.next());  // trace() may modify
            Ray r = cam.ray((x + rng.next()) / img.w, (y + rng.next()) / img.h);
            XYZ c = to_xyz(trace(scene, mats, background, r, w, rng, skip_diffuse_first), w);
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
                    unsigned threads = std::thread::hardware_concurrency(), bool skip_diffuse_first = false) {
    Image img(W, H);
    std::atomic<int> next_row{0};
    auto worker = [&] {
        for (int y; (y = next_row++) < H;) {
            Rng rng(seed * 0x9E3779B9u + unsigned(y));
            render_row(scene, mats, background, cam, img, y, spp, exposure, rng, skip_diffuse_first);
        }
    };
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < std::max(1u, threads); ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    return img;
}

// ---- Light tracing (particle tracing), for caustics such as the prism (RT-012) ----

// Trace one path from a random emissive quad. At every diffuse vertex, connect to the
// pinhole camera and splat into film (XYZ, W*H). n_paths = total paths over all threads.
// Lights are two-sided: side chosen with p = 1/2, direction cosine-weighted.
inline void light_path(const Scene& scene, const std::vector<Material>& mats, const std::vector<int>& lights,
                       const Camera& cam, int W, int H, double n_paths, std::vector<XYZ>& film, Rng& rng) {
    const Quad& L = scene.quads[lights[std::min(int(rng.next() * lights.size()), int(lights.size()) - 1)]];
    const Material& lm = mats[L.mat];
    Vec3 ln = cross(L.u, L.v);
    float area = length(ln);
    ln = ln / area;
    if (rng.next() < 0.5f) ln = -ln;
    auto w = SampledWavelengths::sample_uniform(rng.next());
    // beta = Le * cos / (p_light * p_area * p_dir), p_dir = 0.5 * cos / pi
    SampledSpectrum beta = w.eval(lm.emission) * (lm.emission_scale * area * lights.size() * 2 * std::numbers::pi_v<float>);
    Ray ray{L.q + L.u * rng.next() + L.v * rng.next(), sample_cosine_hemisphere(ln, rng.next(), rng.next())};
    const float splat = float(W) * H / (n_paths * cam.film_area);

    for (int depth = 0; depth < MAX_DEPTH; ++depth) {
        Hit h;
        if (!scene.intersect(ray, 1e-4f, 1e30f, h)) break;
        const Material& m = mats[h.mat];
        if (m.kind == Kind::Diffuse) {
            Vec3 to = cam.origin - h.p;
            float d = length(to), cos_x = dot(to, h.n) / d, u, v, cos_c;
            Hit occ;
            if (cos_x > 0 && cam.project(h.p, u, v, cos_c) && !scene.intersect({h.p, to / d}, 1e-4f, d * (1 - 1e-4f), occ)) {
                // f * cos_x * We * cos_c / d^2, with pinhole We = 1 / (film_area * cos_c^4)
                float k = splat * cos_x / (std::numbers::pi_v<float> * d * d * cos_c * cos_c * cos_c);
                XYZ c = to_xyz(beta * w.eval(m.albedo) * k, w);
                XYZ& px = film[size_t(std::min(int(v * H), H - 1)) * W + std::min(int(u * W), W - 1)];
                for (int i = 0; i < 3; ++i) px[i] += c[i];
            }
        }
        scatter(m, h, ray, w, beta, rng);
        if (!roulette(depth, beta, rng)) break;
    }
}

// Camera paths for everything not starting at a diffuse surface (lights seen directly,
// anything seen via glass/mirrors) + light paths for everything that does. The split is
// disjoint, so nothing is counted twice. Area-light quads only; no environment light.
inline Image render_hybrid(const Scene& scene, const std::vector<Material>& mats, const Camera& cam, int W, int H,
                           int spp, long n_light_paths, float exposure, unsigned seed = 1,
                           unsigned threads = std::thread::hardware_concurrency()) {
    Image img = render(scene, mats, constant(0.0f), cam, W, H, spp, exposure, seed, threads, true);
    std::vector<int> lights;
    for (int i = 0; i < int(scene.quads.size()); ++i)
        if (mats[scene.quads[i].mat].emission_scale > 0) lights.push_back(i);
    if (lights.empty()) return img;

    const long CHUNK = 1 << 16;
    std::atomic<long> next{0};
    threads = std::max(1u, threads);
    std::vector<std::vector<XYZ>> films(threads, std::vector<XYZ>(size_t(W) * H, XYZ{0, 0, 0}));
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            for (long c; (c = next++) * CHUNK < n_light_paths;) {
                Rng rng(seed * 0x85EBCA6Bu + unsigned(c));
                for (long i = c * CHUNK; i < std::min(n_light_paths, (c + 1) * CHUNK); ++i)
                    light_path(scene, mats, lights, cam, W, H, double(n_light_paths), films[t], rng);
            }
        });
    for (auto& t : pool) t.join();
    for (size_t p = 0; p < img.px.size(); ++p) {
        XYZ sum{0, 0, 0};
        for (auto& f : films) for (int i = 0; i < 3; ++i) sum[i] += f[p][i];
        RGB c = xyz_to_linear_srgb(sum);
        for (int i = 0; i < 3; ++i) img.px[p][i] += c[i] * exposure;
    }
    return img;
}
