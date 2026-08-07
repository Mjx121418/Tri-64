# Tri-64

Extracts geometry, objects, and collision from **Super Mario 64 ROMs** — including
binary ROM hacks — *without* relying on the decompiled source code, and renders the
result in Godot.

The extractor should faithfully reproduce the rendered level: textured triangles,
object placements, collision data, camera info, and level metadata. The renderer
does not need to consume every extracted datum.

## Architecture

    ROM
      ↓
    Level Script VM
      ↓
    Geo Layout Interpreter
      ↓
    Display List Decoder
      ↓
    Scene Representation
      ↓
    Triangle Export
      ↓
    Godot renderer

The stages are loosely coupled:

| Stage | Responsibility | Must not |
|---|---|---|
| Level Script | segment table, memory loading, object spawning, area management, geo entry points | render geometry, interpret display lists |
| Geo Layout | graph node hierarchy, transformations, render layers, object models | decode Fast3D |
| Fast3D Decoder | matrix stack, vertex cache, texture state, triangle emission | know about level scripts or graph nodes |
| Godot Bridge | transform exported data to Godot-native data | know about the ROM |
| Godot Renderer | render the level model, render options, camera control | know about the ROM |

## Repository layout

```
cpp/        Extractor + Godot extension (C++26, built with SCons)
  src/Memory/   Segment table, MIO0 decompression
  src/Scripts/  Level script VM, geo layout, collision, behavior scripts,
                macro/special preset tables, movtex (water/lava)
  src/Level/    Display list decoder/interpreter, textures, object models,
                level extraction pipeline
  src/ROM.h     ROM loading
  src/godot_bridge.cpp   GDExtension entry point (GodotBridge class)
  tests/        Test suite (scons test)
project/    Godot project (main.gd, main.tscn, GDExtension config)
docs/       Engine.md (implementation-vs-original-engine deviations),
            Quirks.md (pure engine / ROM-hack quirks),
            N64_RDP_State_Machine.md
```

## Building

Prerequisites:

- SCons (`pip install scons`)
- Godot 4.x
- `godot-cpp` checked out into `cpp/godot-cpp/` (a branch/tag matching your Godot
  version; this directory is gitignored — clone separately or add as a submodule)
- ROM dumps for testing: `baserom.us.z64` (vanilla) and/or a hack such as
  *Super Mario Treasure World*, placed in `cpp/`

Build the macOS extension and run the tests:

```
cd cpp
scons            # builds ../project/bin/libtri64.dylib
scons test       # builds the test binary
./tests/bin/test # runs the suite (expect 0 FAIL)
```

Cross-compile the Windows extension (requires `brew install mingw-w64`):

```
scons windows    # builds ../project/bin/tri64.windows.x86_64.dll
```

## Running

Open `project/` in Godot and run `main.tscn`. Load a ROM, pick a level and area,
then switch between the two render modes:

- **Geometry** — textured/lit level mesh plus object models.
- **Collision** — terrain and object collision triangles, colored by surface type
  (floors blue, walls green, ceilings red), with a widened edge wireframe.

## Compatibility

Supported inputs:

- Vanilla SM64 (US) and most binary ROM hacks
- Custom segment layouts (SM64-editor hacks; see `docs/Quirks.md` for its
  `level script command 0x17` redefinition and the "faked MIO0" segment 2)
- Fast3D microcode (the N64's `fast3d.s`, not F3DEX2); new microcodes can be added
  without modifying the core decoder

## Status

Implemented and verified against the decomp (see `docs/Engine.md` for every
deviation from the original engine):

- MIO0 decompression, segment table (bounds-checked)
- Level script interpreter (warps, painting/instant warps, whirlpools, music,
  dialog, transitions, Mario spawn, area→camera wiring)
- Geo layout processor (camera node, generated nodes, background, culling radius,
  view registry, switch-case/perspective/held-object funcs)
- Fast3D display list decoder (matrix stack, vertex cache, per-vertex lighting,
  combine-based texture classification, clamp/repeat, CI4/CI8/IA/I/RGBA32 textures,
  TLUTs, lights/fog)
- Collision decoder (surface flags, floor/wall/ceiling classification,
  lowerY/upperY, water boxes, rooms, object `LOAD_COLLISION_DATA`)
- Behavior script static analysis (field-write capture, frame-0 object animation)
- Object models (preset tables from the main segment, model mesh bake, frame-0
  animation)
- Movtex water/lava quad extraction
- Godot renderer with Geometry and Collision modes

## Design principles

- **Correctness > compatibility with ROM hacks > clean architecture**; avoid
  game-specific hacks whenever possible.
- Prefer RAII, `unique_ptr`, const correctness, and value semantics. No global
  state, no raw owning pointers, no large switch statements that mix
  responsibilities.
- Design for extensibility over quick fixes.

## Non-goals

The extractor does not emulate audio, gameplay, physics, or scripting behavior
beyond asset extraction. Runtime behavior that is C code (e.g. `cur_obj_scale`)
and C-table collisions (rotating/bowser platforms) are documented non-goals —
see `docs/Quirks.md`.

## Documentation

- `docs/Engine.md` — how our implementation deviates from the original SM64
  engine, organized by subsystem.
- `docs/Quirks.md` — pure engine and ROM-hack quirks.
- `docs/N64_RDP_State_Machine.md` — N64 RDP reference.

## Authorship

Most of the code in this repository was written by the AI model
**deepseek-v4-flash**.
