# Plan — Spectral Ray Tracer

## Goal
A CPU path tracer that transports light per **wavelength** (380–780 nm) rather than per RGB channel, so effects RGB renderers fake or miss come out naturally: dispersion (prism rainbows), blackbody colour temperatures, metamerism, accurate light mixing.

Showcase target: a Cornell box lit by a ~3000 K blackbody plus a glass prism splitting a beam into a spectrum.

## Non-goals (for now)
GPU, real-time, a GUI, polarisation, fluorescence, volumetrics. Revisit after M5.

## Stack
- C++20, CMake, g++. **No external dependencies** until one earns its place (candidate: `stb_image_write` for PNG).
- Output: binary PPM (`P6`) — viewable everywhere, zero code.
- Tests: `tests/tests.cpp`, plain `assert`, built as `rt_tests`.

## Architecture

```
main.cpp ── scene setup ── render loop (per pixel, N samples)
   │
   ├─ sample hero wavelength λ (+3 stratified companions)   spectrum.h
   ├─ camera ray                                             camera.h
   ├─ trace(ray, λ) → radiance per λ                         integrator.h
   │     ├─ intersect scene (list → BVH)                     geometry.h, bvh.h
   │     └─ material.sample(λ) → bounce / emit               material.h
   └─ accumulate L(λ)·CMF(λ)/pdf(λ) → XYZ → linear sRGB → tonemap → 8-bit
                                                             color.h, image.h
```

### Key decisions
| Topic | Choice | Why |
|---|---|---|
| Wavelength sampling | **Hero wavelength** (Wilkie et al. 2014): 4 wavelengths per path, rotated evenly across the range | 4× less colour noise than 1 λ/path; collapses to 1 λ on dispersive bounces |
| Colour matching | CIE 1931 2° — **analytic multi-lobe Gaussian fit** (Wyman, Sloan, Shirley 2013) | no 471-row table to embed; error well below visible |
| XYZ → display | XYZ → linear sRGB (D65) matrix → exposure + Reinhard/ACES-fit → sRGB gamma | standard |
| Spectra | `Spectrum` = function of λ: constant, blackbody (Planck), Gaussian, piecewise-linear samples | materials defined natively in spectral terms |
| RGB albedos | Smits-style upsampling first; Jakob–Hanika 2019 later if needed | Smits is ~30 lines; JH needs a precomputed table |
| Dispersion | Cauchy / Sellmeier IOR per λ (BK7, flint) | gives the prism rainbow |
| Sampling | random (`std::mt19937` per thread) → later NEE + MIS | correctness first |
| Caustics | light tracing + camera paths, split by "is the first camera hit diffuse?" (`render_hybrid`); BDPT (`render_bdpt`) as an opt-in general renderer | path tracing alone can't find a light through a slit and prism. BDPT measured *no better* than the hybrid on the showcase: the remaining noise is SDS paths, which need photon mapping / VCM (RT-019 notes) |
| Parallelism | `std::thread` over tile rows | stdlib; no OpenMP/TBB needed |

## Milestones
Order only, no dates. `tickets/` holds the breakdown; status is whichever folder each ticket sits in.

- **M0 Foundation** — builds, writes an image, test harness runs.
- **M1 Spectral core** — wavelengths in, correct colours out (CMFs, XYZ→sRGB, blackbody, D65).
- **M2 Geometry & camera** — rays, spheres, planes/quads, triangles, pinhole camera.
- **M3 Path tracer** — Lambertian, emitters, Monte Carlo path tracing, Cornell box.
- **M4 Spectral materials** — mirror, dispersive dielectric, hero-wavelength collapse, prism scene.
- **M5 Performance** — BVH, multithreading, OBJ meshes.
- **M6 Quality** — NEE + MIS, RGB→spectrum upsampling, PNG output, simple scene file.
- **M7 BDPT** (stretch) — bidirectional path tracing with MIS, replacing the hybrid for caustic-heavy scenes.

**Next candidates:** SPPM or VCM for specular-diffuse-specular caustics (the remaining noise in the prism), smooth mesh normals (glass bunny), rough materials (GGX), Beer's-law absorption.
