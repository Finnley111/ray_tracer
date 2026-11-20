#include "camera.h"
#include "color.h"
#include "geometry.h"
#include "image.h"
#include "integrator.h"
#include "material.h"
#include "metals.h"
#include "spectrum.h"
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <string>

static Rng rng(1);

// Blackbody swatch, 1000 K (left) -> 12000 K (right). Top: MC over hero
// wavelengths; bottom: deterministic integral. Columns normalised to max channel 1.
static Image swatch() {
    const int SPP = 64;
    Image img(800, 200);
    for (int x = 0; x < img.w; ++x) {
        Spectrum bb = blackbody(1000.0f + 11000.0f * x / (img.w - 1));
        RGB ref = xyz_to_linear_srgb(spectrum_to_xyz(bb));
        float scale = 1.0f / std::max({ref[0], ref[1], ref[2]});
        for (int y = 0; y < img.h; ++y) {
            RGB c = ref;
            if (y < img.h / 2) {
                XYZ acc{0, 0, 0};
                for (int s = 0; s < SPP; ++s) {
                    auto w = SampledWavelengths::sample_uniform(rng.next());
                    XYZ e = to_xyz(w.eval(bb), w);
                    for (int i = 0; i < 3; ++i) acc[i] += e[i] / SPP;
                }
                c = xyz_to_linear_srgb(acc);
            }
            img.at(x, y) = {c[0] * scale, c[1] * scale, c[2] * scale};
        }
    }
    return img;
}

// Geometry debug view: colour = (normal + 1) / 2, background black.
static Image normals() {
    const int W = 640, H = 360, SPP = 4;
    Scene scene;
    scene.spheres = {{{0, 1, 0}, 1, 0}, {{-2.2f, 0.6f, 0.5f}, 0.6f, 0}, {{2.0f, 0.8f, -0.8f}, 0.8f, 0}};
    scene.quads = {{{-6, 0, -6}, {0, 0, 12}, {12, 0, 0}, 0}};  // floor, u x v = +y
    Camera cam({0, 2, 6}, {0, 0.8f, 0}, {0, 1, 0}, 40, float(W) / H);
    Image img(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            Vec3 acc;
            for (int s = 0; s < SPP; ++s) {
                Hit h;
                Ray r = cam.ray((x + rng.next()) / W, (y + rng.next()) / H);
                if (scene.intersect(r, 1e-4f, 1e30f, h)) acc += (h.n + Vec3{1, 1, 1}) * (0.5f / SPP);
            }
            img.at(x, y) = {acc.x, acc.y, acc.z};
        }
    return img;
}

// RT-008: diffuse scene lit only by a 3000 K blackbody panel overhead.
// Neutral (constant-albedo) floor should come out warm; the coloured
// spheres use Gaussian reflectance spectra, not RGB.
static Image lights() {
    const int W = 480, H = 270, SPP = 128;
    std::vector<Material> mats = {
        {constant(0.8f)},                                           // 0 white
        {[](float l) { return 0.05f + 0.8f * gaussian(620, 40)(l); }},  // 1 red
        {[](float l) { return 0.05f + 0.7f * gaussian(540, 30)(l); }},  // 2 green
        {[](float l) { return 0.05f + 0.7f * gaussian(460, 30)(l); }},  // 3 blue
        {constant(0.0f), blackbody(3000), 25.0f},                   // 4 light
    };
    Scene scene;
    scene.spheres = {{{0, 1, 0}, 1, 1}, {{-2.2f, 0.6f, 0.5f}, 0.6f, 2}, {{2.0f, 0.8f, -0.8f}, 0.8f, 3}};
    scene.quads = {{{-6, 0, -6}, {0, 0, 12}, {12, 0, 0}, 0},   // floor
                   {{-1, 4, -1}, {2, 0, 0}, {0, 0, 2}, 4}};    // light
    Camera cam({0, 2, 6}, {0, 0.8f, 0}, {0, 1, 0}, 40, float(W) / H);
    return render(scene, mats, constant(0.0f), cam, W, H, SPP, 1.0f);
}

// RT-009: Cornell box (classic 555-unit dimensions), D65 area light.
// Wall reflectances are smooth spectral curves, not RGB triples.
static Image cornell(int spp) {
    const int W = 400, H = 400;
    auto red = [](float l) { return 0.05f + 0.6f / (1 + std::exp(-(l - 600) / 12)); };
    std::vector<Material> mats = {
        {constant(0.73f)},                                              // 0 white
        {red},                                                          // 1 red
        {[](float l) { return 0.05f + 0.45f * gaussian(530, 35)(l); }},  // 2 green
        {constant(0.0f), d65(), 15.0f},                                 // 3 light
    };
    Scene sc;
    sc.quads = {
        {{555, 0, 0}, {0, 555, 0}, {0, 0, 555}, 1},        // left (camera's left is +x)
        {{0, 0, 0}, {0, 555, 0}, {0, 0, 555}, 2},          // right
        {{0, 0, 0}, {555, 0, 0}, {0, 0, 555}, 0},          // floor
        {{0, 555, 0}, {555, 0, 0}, {0, 0, 555}, 0},        // ceiling
        {{0, 0, 555}, {555, 0, 0}, {0, 555, 0}, 0},        // back
        {{343, 554, 332}, {-130, 0, 0}, {0, 0, -105}, 3},  // light
    };
    auto rot_box = [&](Vec3 p, Vec3 size, float deg) {
        float r = deg * std::numbers::pi_v<float> / 180, c = std::cos(r), s = std::sin(r);
        add_box(sc.quads, p, Vec3{c, 0, -s} * size.x, {0, size.y, 0}, Vec3{s, 0, c} * size.z, 0);
    };
    rot_box({265, 0, 295}, {165, 330, 165}, 15);   // tall
    rot_box({130, 0, 65}, {165, 165, 165}, -18);   // short
    Camera cam({278, 278, -800}, {278, 278, 0}, {0, 1, 0}, 40, 1.0f);
    return render(sc, mats, constant(0.0f), cam, W, H, spp, 1.0f);
}

// RT-010: gold, copper, silver (measured n,k) and a 95% mirror under a D65 panel
// and a dim D65 sky. Metal colours come only from the spectral Fresnel term.
static Image metals(int spp) {
    const int W = 640, H = 300;
    std::vector<Material> mats = {
        {constant(0.73f)},                   // 0 floor
        conductor(au_eta(), au_k()),         // 1 gold
        conductor(cu_eta(), cu_k()),         // 2 copper
        conductor(ag_eta(), ag_k()),         // 3 silver
        mirror(constant(0.95f)),             // 4 mirror
        {constant(0.0f), d65(), 6.0f},       // 5 light
    };
    Scene sc;
    sc.spheres = {{{-3.3f, 1, 0}, 1, 1}, {{-1.1f, 1, 0}, 1, 2}, {{1.1f, 1, 0}, 1, 3}, {{3.3f, 1, 0}, 1, 4}};
    sc.quads = {{{-20, 0, -20}, {0, 0, 40}, {40, 0, 0}, 0},
                {{-3, 6, -1}, {6, 0, 0}, {0, 0, 2}, 5}};
    Spectrum s = d65();
    Spectrum sky = [s](float l) { return 0.4f * s(l); };
    Camera cam({0, 2.2f, 8}, {0, 1, 0}, {0, 1, 0}, 35, float(W) / H);
    return render(sc, mats, sky, cam, W, H, spp, 1.0f);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: rt <swatch|normals|lights|cornell|metals> <out.ppm> [spp]\n");
        return 2;
    }
    std::string mode = argv[1];
    Image img = mode == "swatch" ? swatch() : mode == "normals" ? normals()
                : mode == "lights"  ? lights()
                : mode == "cornell" ? cornell(argc > 3 ? std::atoi(argv[3]) : 1024)
                : mode == "metals"  ? metals(argc > 3 ? std::atoi(argv[3]) : 256)
                                    : Image(0, 0);
    if (img.w == 0) {
        std::fprintf(stderr, "unknown mode %s\n", argv[1]);
        return 2;
    }
    if (!write_ppm(img, argv[2])) {
        std::fprintf(stderr, "could not write %s\n", argv[2]);
        return 1;
    }
    std::printf("wrote %s\n", argv[2]);
}
