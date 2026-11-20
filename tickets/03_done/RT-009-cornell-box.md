---
id: RT-009
title: Spectral Cornell box
milestone: M3
week: 5-6
estimate: 3h
---
# RT-009 — Spectral Cornell box

**Why:** Standard reference scene; validates everything so far.

**Plan ref:** PLAN.md § Milestones → M3

**Touches:** src/main.cpp (scene)

## Tasks
- [x] Cornell box from quads, red/green walls as Gaussian-ish reflectance spectra
- [x] area light D65
- [x] 1024 spp render

## Acceptance
- [x] renders/cornell.ppm: colour bleeding visible on ceiling/boxes, no NaNs/fireflies grid

## Notes
- `rt cornell <out> [spp]`: classic 555-unit box. The red wall is a sigmoid that switches on above 600 nm, green is a Gaussian at 530 nm, white is a constant 0.73, and the light is D65 × 15.
- `add_box()` in `geometry.h` builds a box from 6 quads, and `rot_box` in `main.cpp` rotates it about y.
- 400×400 at 1024 spp takes 49 s on 8 threads. Colour bleeding is visible and there are no fireflies. Some noise remains, which NEE (RT-016) will clean up.
- These are not the measured Cornell spectra. Swap in the measured data if you want a reference comparison.
