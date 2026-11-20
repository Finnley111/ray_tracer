---
id: RT-019
title: Bidirectional path tracing (BDPT) with MIS
milestone: M7
week: stretch (Feb 2027)
estimate: 12-16h
---
# RT-019 — Bidirectional path tracing (BDPT) with MIS

**Why:** Two kinds of noise are left in the showcase: chromatic speckle when looking *through* the prism, and specks where the gold sphere reflects caustics on the floor. Both are specular → diffuse → specular paths. The camera tracer (`trace`) and the light tracer (`light_path`) each sample them badly, and `render_hybrid` can only pick one of the two per path. BDPT (Veach 1997, ch. 10) builds a camera subpath and a light subpath, connects every pair of vertices, and weights all the strategies with MIS. It replaces `render_hybrid`.

**Plan ref:** PLAN.md § Key decisions → Sampling, Caustics

**Touches:** src/integrator.h (new `bdpt.h` only if it grows past ~250 lines), src/camera.h (pdf of a camera direction), src/geometry.h (light pdfs, reusing `sample_light`), src/main.cpp, src/scene_file.h (`renderer bdpt`), tests/tests.cpp

## Design notes
- **Vertex record:** position, normal, material, `beta`, `pdf_fwd`, `pdf_rev` (area measure), a `delta` flag (mirror, conductor or dielectric), and the vertex type (camera, light, surface).
- **Wavelengths:** sample one `SampledWavelengths` per pixel sample and share it between both subpaths. If either subpath crosses dispersive glass, the joined path keeps only the hero wavelength. Terminate secondaries on a *copy* for each connection, so one connection doesn't strip wavelengths from another. This must match how `to_xyz` handles the pdf.
- **Strategies (s light vertices, t camera vertices):**
  - s = 0: the camera path hits an emitter.
  - s = 1: NEE. The existing `sample_light` logic gets a full pdf_rev.
  - t = 1: splat to the film. The existing `light_path` camera connection becomes the general case.
  - s, t ≥ 2: connect through a shadow ray with a geometry term.
  - Connections are skipped when either endpoint is `delta`.
- **MIS:** start with the balance heuristic computed from the pdf_fwd/pdf_rev ratio chain (pbrt-v4 `MISWeight` pattern), with delta vertices treated as pdf 1 and excluded from the sum. Switch to the power heuristic only if it measurably helps.
- **Lights:** one-sided by default and `two_sided` aware, matching RT-016. Area lights only (quads and spheres); the environment/background stays s = 0 only, so document that.
- **Film:** splats (t = 1) go to per-thread XYZ buffers, as in `render_hybrid`. Everything else accumulates per pixel. Combine with the same `W*H / n_paths` normalisation, adjusted for camera paths per pixel.
- **Keep** `trace()` and `render()` as the fast default for scenes without caustics. BDPT is opt-in: `renderer bdpt` in scene files, `showcase-bdpt` mode.

## Tasks
- [x] Camera: `pdf_we(ray)`, i.e. the solid-angle pdf of a camera ray (pinhole: 1 / (film_area · cos³θ))
- [x] Light: area pdf and emission pdf for a point and direction on a quad or sphere light
- [x] `random_walk()` shared by both subpaths, recording vertices with pdf_fwd and filling the previous vertex's pdf_rev
- [x] `connect(s, t)` for every strategy, including the t = 1 splat and s = 1 light resampling
- [x] MIS weight via pdf ratios; leave a `ponytail:` note on any approximation
- [x] `render_bdpt()` with threads, splat buffers and a deterministic seed per row/chunk
- [x] Scene-file `renderer bdpt`; `rt showcase-bdpt`
- [x] Tests (below)

## Acceptance
- [x] **Unbiased:** on the RT-016 test scene (quad and sphere light, occluder) and the furnace-inside-a-sphere scene, BDPT's mean matches `render()` within 2%. On the prism scene it matches `render_hybrid` within 3% on the screen region.
- [x] **MIS sanity:** for one fixed path of length 3 in a diffuse-only scene, the weights over all (s, t) strategies sum to 1 (±1e-4).
- [ ] ❌ **Better where it matters:** on `showcase` at equal wall-clock time, BDPT has lower two-seed RMS noise (as in `neebench`, display-clamped) than `render_hybrid` over (a) the prism's screen-space bounding box and (b) the gold sphere's. Report both numbers. It must not be worse overall.
- [x] `renders/bench_bdpt.png` (BDPT) vs `renders/bench_hybrid.png` at equal time reviewed side by side with `showcase.png`.
- [x] `rt_tests` passes; no warnings.

## References
- E. Veach, *Robust Monte Carlo Methods for Light Transport Simulation*, PhD thesis 1997, ch. 10.
- Pharr, Jakob, Humphreys, *Physically Based Rendering* 4th ed., §13.3 (BDPT; MIS weight via pdf ratios).

## Notes
- `src/bdpt.h` (~330 lines): `Vertex` and `Bdpt` with `walk`, `connect` and `mis_weight` (balance heuristic via pdf ratios, as in pbrt-v4), plus `render_bdpt()`. `Camera::pdf_dir` and `Camera::importance` were added. `render_desc()` in `scene_file.h` dispatches pt/hybrid/bdpt; the scene-file syntax is `renderer bdpt`, and there's an `rt showcase-bdpt` mode.
- **Deviations from the design notes:**
  - Wavelength termination is shared per sample, as in pbrt-v4, not per connection. It's simpler and still unbiased.
  - s = 1 reuses the light subpath's first vertex instead of resampling a light.
  - Russian roulette runs in both walks and is left out of the MIS pdfs. That's still unbiased, because the weights still sum to 1 and every (s, t) can produce every path.
  - The (1, 1) strategy is skipped. It duplicates (0, 2) for a light seen directly; this was a real bug, found because the image came out 1.66× too bright.
- **Correctness:**
  - MIS weights for a fixed 4-vertex path: 0.0067 + 0.2501 + 0.4710 + 0.2721 = 1.000000.
  - BDPT vs `render()` mean, all within 0.7%: the RT-016 lights scene, the same scene with dispersive glass and a mirror, and the inside-emitting-sphere scene.
  - Debugging confirmed each strategy family on its own reproduces the right per-length contributions.
  - Prism caustic: BDPT vs hybrid screen mean within 0.26%.
- **`rt bdptbench` at equal time on the showcase** (hybrid with 64 camera + 128 light paths per pixel, 30.7 s, vs BDPT at 55 spp, 34.3 s; two-seed RMS, display-clamped):
  - Prism region: 0.0870 → 0.0855 (1.02×).
  - Gold region: 0.0300 → 0.0302 (0.99×).
  - Whole image: 0.0277 → 0.0312 (**0.89×, worse**).
  - **The acceptance criterion is not met.**
- **Why: the ticket's premise was wrong.** Diagnosis with the hybrid on showcase variants:

  | variant | prism region | gold region |
  |---|---|---|
  | SF11 prism | 0.0836 | 0.0310 |
  | non-dispersive prism (n = 1.79) | 0.0717 | 0.0266 |
  | no prism | 0.0094 | 0.0186 |

  - Dispersion accounts for only ~14% of the prism noise. Most of it is **specular-diffuse-specular (SDS) paths**: the floor seen *through* the prism, lit by light that also came *through* the prism, and the gold sphere reflecting caustics. No BDPT connection can sample those, since every connection has to pass through a delta vertex. The fix is **photon mapping / VCM** (vertex connection and merging), which adds photon "merging" on top of exactly this BDPT.
  - The remaining dispersion noise is one-wavelength-per-path colour noise; no light-transport algorithm changes that.
- **Status:** BDPT is correct and opt-in. It's the most general renderer here: it handles caustics *and* a background, which `render_hybrid` can't, but in this scene it doesn't beat the hybrid's camera/light split. The `showcase` default stays hybrid.
