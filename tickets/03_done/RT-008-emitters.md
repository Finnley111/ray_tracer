---
id: RT-008
title: Area lights with blackbody/D65 emission
milestone: M3
estimate: 2h
---
# RT-008 — Area lights with blackbody/D65 emission

**Why:** Spectral lights are the point.

**Plan ref:** PLAN.md § Key decisions → Spectra

**Touches:** src/material.h, src/integrator.h

## Tasks
- [x] emissive material with Spectrum × scale
- [x] emission added on hit
- [x] exposure control in main

## Acceptance
- [x] scene with a 3000 K quad light renders visibly warm

## Notes
- Emission is `Spectrum × emission_scale`, applied on every hit. Lights are **two-sided** for now, which is simpler and harmless for ceiling panels. Add one-sided lights if a scene needs them.
- `rt lights renders/lights.ppm`: 3000 K panel overhead, 480×270 at 128 spp, 10.5 s single-threaded. The neutral floor comes out warm, and the spheres use Gaussian reflectance spectra.
- Exposure is a plain multiplier with clamping. Reinhard/ACES tone mapping is deferred until a scene clips badly (likely the showcase in RT-017).
- The image is noisy because there is no light sampling yet (BSDF sampling only). RT-016 adds next-event estimation (NEE).
