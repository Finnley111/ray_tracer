---
id: RT-004
title: Hero wavelength sampling + spectral swatch render
milestone: M1
estimate: 3h
---
# RT-004 — Hero wavelength sampling + spectral swatch render

**Why:** Monte Carlo over λ is the core of spectral rendering.

**Plan ref:** PLAN.md § Key decisions → Wavelength sampling

**Touches:** src/spectrum.h, src/main.cpp

## Tasks
- [x] SampledWavelengths: 4 λ, hero uniform + 3 rotated by range/4
- [x] SampledSpectrum (4 floats) with arithmetic
- [x] to_xyz estimator: Σ L(λ)·CMF(λ)/pdf / 4
- [x] swatch render: one strip per blackbody temperature 1500–10000 K, converged by MC

## Acceptance
- [x] MC estimate of D65 matches RT-003 integral to <1%
- [x] renders/swatch.ppm shows a smooth red→white→blue strip

## Notes
- `SampledWavelengths::sample_uniform(u)` picks a hero wavelength plus 3 companions spaced range/4 apart, wrapping at the ends. `SampledSpectrum` holds the 4 values. `to_xyz` is the Monte Carlo estimator and skips any wavelength whose pdf is 0, which RT-011 relies on.
- The MC estimate of D65 over 200k samples matches the exact integral to within 1% (tested).
- `rt renders/swatch.ppm`: blackbody 1000→12000 K, with the MC result on top and the exact integral below. They match. The ends of each strip are normalised so only hue is compared.
- Dropped the secondary wavelengths early. RT-011 adds `terminate_secondary()` when dispersion needs it.
