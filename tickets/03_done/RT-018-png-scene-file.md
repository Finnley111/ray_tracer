---
id: RT-018
title: PNG output + simple scene file
milestone: stretch
estimate: 3h
---
# RT-018 — PNG output + simple scene file

**Why:** Nicer output and no recompiling to change scenes.

**Plan ref:** PLAN.md § Stack

**Touches:** src/image.h, src/scene_file.h

## Tasks
- [x] vendor stb_image_write.h
- [x] line-based scene format

## Acceptance
- [x] rt scene.txt out.png works

## Notes
- **PNG: stb was not vendored.** `write_png()` in `image.h` (~50 lines) writes stored (uncompressed) deflate blocks with CRC32 and Adler-32, so it needs no zlib and no dependency. Files are ~3× larger than compressed PNGs (a 400×400 image is 480 KB); this is marked `ponytail:`. `write_image()` chooses PNG or PPM from the file extension.
  - Checked by test (signature, layout, IEND CRC) and independently with Python's zlib (every chunk CRC verified, IDAT decompresses to the right size).
- **Scene files:** `src/scene_file.h`, with the grammar documented at the top.
  - Statements: `image`, `spp`, `exposure`, `camera`, `background`, `renderer pt|hybrid N`, `material` (diffuse/mirror/conductor/dielectric/light, spectra as `rgb`/`const`/`blackbody`/`d65`/`gaussian`), `sphere`, `quad`, `tri`, `box`, `obj`.
  - Every error reports `file:line`: unknown statement or material, bad number, trailing junk, duplicate name, failed OBJ load. Tests cover parsing plus seven error cases.
- `rt <file-with-a-dot> <out> [spp]` loads a scene file; the optional number overrides spp (pt) or light paths per pixel (hybrid). `rt` with no arguments lists the built-in modes.
- `scenes/example.txt`: Cornell box with RGB walls, gold sphere and SF11 sphere, hybrid, 64 s. `scenes/bunny.txt`: glass and copper bunnies under a 4500 K light (needs the asset).
- Not supported: `usemtl` and textures in OBJ files, include files, and per-object transforms beyond `box` rotation and `obj` fitting.
