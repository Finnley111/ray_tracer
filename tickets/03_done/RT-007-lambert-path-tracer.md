---
id: RT-007
title: Lambertian material + recursive path tracer
milestone: M3
estimate: 4h
---
# RT-007 — Lambertian material + recursive path tracer

**Why:** The actual light transport.

**Plan ref:** PLAN.md § Architecture → integrator

**Touches:** src/material.h, src/integrator.h, src/main.cpp

## Tasks
- [x] Material: diffuse with Spectrum albedo
- [x] cosine-weighted hemisphere sampling
- [x] trace(): bounce up to max depth, Russian roulette after 3
- [x] per-path SampledSpectrum throughput

## Acceptance
- [x] white furnace test: albedo 1 sphere in uniform env radiance 1 → pixel = 1 (±1%)

## Notes
- `src/material.h`: `Rng`, `Material` (diffuse albedo + optional emission) and cosine hemisphere sampling (Duff 2017 orthonormal basis).
- `src/integrator.h`: `trace()` is an iterative path tracer with up to 64 bounces and Russian roulette from bounce 3. `render()` produces an image from a scene and camera.
- Furnace tests (100k paths each):
  - Albedo 1 sphere under a sky of 1: 1.0000.
  - Albedo 0.5 sphere: 0.5000.
  - Inside a closed sphere with albedo 0.8 and emission 0.2: 0.9957 (expected 1). This one exercises multi-bounce paths and Russian roulette.
- `ponytail:` marks two shortcuts: rendering is single-threaded until RT-014, and there is only one material kind until RT-010/011.
