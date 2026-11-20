# Spectral Ray Tracer

A physically based, spectral (wavelength-domain) path tracer in C++20. What leaves this workspace: rendered images in `renders/` and the `rt` binary.

Built on ICM: folders carry sequencing, files carry state. If something needs explaining, it goes in that folder's `CONTEXT.md`.

## Where things live

| Folder | What it holds |
|---|---|
| `plan/` | factory: architecture + scope (`PLAN.md`), milestones and dates (`TIMELINE.md`) |
| `tickets/` | the work queue — `01_backlog/` → `02_active/` → `03_done/`; contract in `tickets/CONTEXT.md` |
| `src/` | the renderer source (one header per concept, `main.cpp` drives it) |
| `scenes/` | scene files (format documented at the top of `src/scene_file.h`) |
| `tests/` | `assert`-based checks, built as `rt_tests` |
| `renders/` | product: output images (gitignored) |
| `assets/` | downloaded meshes (gitignored) — see Build |

## Route by what just happened

| If | Go to | Then stop at |
|---|---|---|
| starting work | `tickets/CONTEXT.md`, then the lowest-numbered ticket in `02_active/` (else `01_backlog/`) | human reviews the diff + render |
| ticket approved | move it to `03_done/`, pull the next | — |
| asked for status | `ls tickets/*/` and compare against `plan/TIMELINE.md` | report |
| design question | `plan/PLAN.md` | — |

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/rt_tests && ./build/rt scenes/example.txt renders/example.png [spp]
./build/rt showcase renders/showcase.png      # built-in scenes: run ./build/rt for the list
```

Meshes for `rt bunny` (Stanford 3D Scanning Repository bunny, research use, not committed):

```
mkdir -p assets && curl -fL https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/stanford-bunny.obj -o assets/bunny.obj
```

## The one rule

A ticket moves to `03_done/` only after a person has looked at its acceptance check (test output or render).
