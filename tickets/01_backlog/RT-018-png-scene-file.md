---
id: RT-018
title: PNG output + simple scene file
milestone: stretch
week: -
estimate: 3h
---
# RT-018 — PNG output + simple scene file

**Why:** Nicer output and no recompiling to change scenes.

**Plan ref:** PLAN.md § Stack

**Touches:** src/image.h, src/scene_file.h

## Tasks
- [ ] vendor stb_image_write.h
- [ ] line-based scene format

## Acceptance
- [ ] rt scene.txt out.png works
