---
id: RT-002
title: CIE colour matching + XYZ to sRGB
milestone: M1
estimate: 3h
---
# RT-002 — CIE colour matching + XYZ to sRGB

**Why:** Spectral radiance only becomes a pixel via the CMFs.

**Plan ref:** PLAN.md § Key decisions → Colour matching

**Touches:** src/color.h, tests/tests.cpp

## Tasks
- [x] Wyman 2013 multi-lobe fit for x̄ ȳ z̄
- [x] XYZ → linear sRGB (D65) matrix
- [x] sRGB transfer function (linear → encoded)
- [x] spectrum_to_xyz(f) by numeric integration 380–780 nm

## Acceptance
- [x] ȳ(555 nm) ≈ 1.0 (±0.02)
- [x] equal-energy spectrum → x,y chromaticity ≈ (0.333, 0.333)
- [x] tests pass

## Notes
- `src/color.h`: Wyman 2013 fit, `spectrum_to_xyz` (1 nm Riemann sum, normalised so a constant spectrum of 1 gives Y=1), XYZ→linear sRGB, `srgb_encode`.
- ȳ(555) ≈ 0.998. Equal-energy chromaticity is within 0.01 of (1/3, 1/3).
- The spectrum strip has flat bands where pure wavelengths fall outside the sRGB gamut and get clipped. That is expected; tone mapping and gamut handling come with RT-004.
