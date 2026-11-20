---
id: RT-017
title: RGB→spectrum upsampling + showcase render
milestone: M6
week: 10
estimate: 3h
---
# RT-017 — RGB→spectrum upsampling + showcase render

**Why:** Let materials be authored in RGB; produce the target image.

**Plan ref:** PLAN.md § Key decisions → RGB albedos

**Touches:** src/spectrum.h, src/main.cpp

## Tasks
- [x] Smits 1999 RGB→spectrum
- [x] showcase scene: Cornell box, 3000 K light, glass prism, gold sphere

## Acceptance
- [x] renders/showcase.ppm — final review

## Notes
- `spectrum.h`: `rgb_spectrum(r, g, b)` implements Smits 1999: seven basis spectra at 10 points from 380 to 720 nm, decomposed into white plus one secondary plus one primary, with linear interpolation.
  - Round trip (reflectance under D65 back to linear sRGB): white and grey come back exact, and the worst channel error over 7 test colours is 0.067 (pure yellow's blue channel). That's the expected accuracy for Smits.
  - Jakob–Hanika would do better if needed.
- `main.cpp`: `add_cornell_room`, `add_rot_box` and `cornell_camera` are shared by `cornell` and `showcase`.
- `rt showcase` (hybrid: 512 camera spp plus 1024 light paths/pixel, 500×500, 4 min 28 s): a Cornell room with RGB walls via Smits, a 3000 K blackbody light, an SF11 prism and a gold sphere with measured n,k.
- `rt showcase-pt` renders the same scene with the path tracer only. At equal time it's much noisier because of caustic fireflies from the gold sphere and the prism, which is why the showcase uses the hybrid.
- What remains in the final image:
  - Chromatic speckle when looking through the prism, because a camera path through dispersive glass carries one wavelength.
  - A few speckles where the gold sphere reflects caustics on the floor.
  - Both are specular-diffuse-specular paths that neither the camera nor the light subpaths sample well. **BDPT** is the proper fix; it's on the stretch list.
- The gold sphere's centre looks black because it reflects the open front of the box. That's correct for this classic setup.
- No tone mapper was needed. Exposure plus clamping looks fine because only the light panel clips.
