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
- [ ] Smits 1999 RGB→spectrum
- [ ] showcase scene: Cornell box, 3000 K light, glass prism, gold sphere

## Acceptance
- [ ] renders/showcase.ppm — final review
