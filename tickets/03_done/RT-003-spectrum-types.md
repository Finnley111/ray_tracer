---
id: RT-003
title: Spectrum types: constant, blackbody, Gaussian, sampled
milestone: M1
week: 2
estimate: 3h
---
# RT-003 — Spectrum types: constant, blackbody, Gaussian, sampled

**Why:** Materials and lights are defined as functions of wavelength.

**Plan ref:** PLAN.md § Key decisions → Spectra

**Touches:** src/spectrum.h, tests/tests.cpp

## Tasks
- [x] Planck blackbody (normalised to peak 1)
- [x] Gaussian bump
- [x] piecewise-linear from (λ, value) pairs
- [x] CIE D65 illuminant (sampled table, 10 nm)

## Acceptance
- [x] D65 → chromaticity ≈ (0.3127, 0.3290)
- [x] 6500 K blackbody looks near-white, 2700 K warm orange in a swatch render
- [x] tests pass

## Notes
- `src/spectrum.h`: `Spectrum = std::function<float(float)>`, plus `constant`, `blackbody` (normalised to a Wien peak of 1), `gaussian`, `sampled` (linear interpolation, clamped at the ends) and `d65` (10 nm table, 1.0 at 560 nm).
- D65 integrates to xy = (0.3127, 0.3291) against the Wyman fit. The reference value is (0.3127, 0.3290).
- `ponytail:` marks the `std::function` choice. Revisit it if profiling in RT-013/014 shows it is slow.
