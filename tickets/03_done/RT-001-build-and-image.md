---
id: RT-001
title: CMake build, PPM writer, test harness
milestone: M0
estimate: 2h
---
# RT-001 — CMake build, PPM writer, test harness

**Why:** Everything else needs a build and a way to see pixels.

**Plan ref:** PLAN.md § Stack

**Touches:** CMakeLists.txt, src/main.cpp, src/image.h, tests/tests.cpp, .gitignore

## Tasks
- [x] CMakeLists.txt with targets rt and rt_tests (C++20, -O2, warnings on)
- [x] image.h: float RGB buffer + write_ppm (P6, clamped, sRGB-encoded)
- [x] main.cpp renders a gradient to argv[1]
- [x] tests.cpp with assert-based checks
- [x] .gitignore for build/ and renders/
- [x] git init

## Acceptance
- [x] cmake build succeeds with zero warnings
- [x] ./build/rt renders/gradient.ppm produces a viewable gradient
- [x] ./build/rt_tests prints OK

## Notes
- Builds with zero warnings (`-Wall -Wextra -Wpedantic`). `rt_tests` keeps asserts live in Release via `-UNDEBUG`.
- `rt` currently draws a spectrum strip + grey ramp (`renders/spectrum.ppm`). It gets replaced in RT-004.
- git repo initialised. Nothing committed yet.
