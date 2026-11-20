# Spectral Ray Tracer

A physically based, spectral (wavelength-domain) path tracer in C++20. What leaves this workspace: rendered images in `renders/` and the `rt` binary.

Built on ICM: folders carry sequencing, files carry state. If something needs explaining, it goes in that folder's `CONTEXT.md`.

## Where things live

| Folder | What it holds |
|---|---|
| `plan/` | factory: architecture + scope (`PLAN.md`), milestones and dates (`TIMELINE.md`) |
| `tickets/` | the work queue — `01_backlog/` → `02_active/` → `03_done/`; contract in `tickets/CONTEXT.md` |
| `src/` | the renderer source (one header per concept, `main.cpp` drives it) |
| `tests/` | `assert`-based checks, built as `rt_tests` |
| `renders/` | product: output images (gitignored) |

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
./build/rt_tests && ./build/rt normals renders/normals.ppm   # modes: swatch, normals
```

## The one rule

A ticket moves to `03_done/` only after a person has looked at its acceptance check (test output or render).
