# Super Mario 64 Engine: our implementation vs the original

This file records how our C++ extraction pipeline differs from the original SM64
engine. It is *not* an emulator: we run a static, one-shot asset extraction (geometry,
textures, objects, collision, camera, lights, movtex, ...) and stop where the game
would begin its per-frame loop. The decomp is the reference (a local checkout of the SM64 decomp); decomp file
paths below are relative to it.

Layout: one section per subsystem. Each entry states the **original** behavior (with a
decomp pointer), then **ours**, then **why**. Pure engine/hack quirks (facts about the
ROM/engine that hold regardless of our implementation) live in `docs/Quirks.md`;
this file focuses on *deviations*.

> See also `docs/Quirks.md` for engine/hack quirks (segment-2 layouts, null DL
> addresses, SM64-editor command 0x17, ...) and `WORKLOG.md` for the work log.

---

## 1. Scope and approach

- **Original**: the game boots, loads segments, builds the scene graph, then runs the
  level/object loops every frame (level_update, cur_obj_update, the RSP microcode) and
  renders via the RSP/RDP.
- **Ours**: a single pipeline `ROM → SegmentTable → LevelScriptVM → GeoLayoutProcessor →
  DLInterpreter → Mesh/Textures → Godot`, plus static decoders for collision, behaviors,
  movtex, and the level's metadata. Everything runtime (physics, AI, camera movement,
  particle/audio state, per-frame animation) is out of scope; where a runtime value is
  needed we freeze it at a static "frame 0 / spawn" value.
- **Why**: the goal is faithful static geometry + object placement extraction for
  rendering, not gameplay.
- **Phase-1/2 extraction additions**: the refactor unified every interpreter/decoder
  onto the `LevelScriptVM` class shape (dependencies by ref, `run()`/`runX()` entry that
  resets state, one method per opcode, decomp citations). Phase 2 extended the decoders
  to parse data the game *executes* but the original pipeline dropped — warps, camera,
  lights, CI textures, surface classification, behavior field writes, movtex, etc. These
  are **extraction-only**: most are exposed on the result but not yet consumed by the
  renderer (lights, camera, collision physics, movtex, behavior state). Each subsystem
  section below flags its additions.

A note on structure: since the Phase-1 refactor every interpreter/decoder is a class
mirroring `LevelScriptVM` (dependencies by ref, `run()`/`runX()` entry that resets
state, one method per opcode, decomp citations in comments). See `WORKLOG.md`.

---

## 2. Memory, segments, MIO0

**Original** — `memory.c`, `load_segment`: a full 32-entry segment table maps segmented
addresses `seg:offset` to RDRAM. `segmented_to_virtual` never faults: even an unloaded
segment maps to a readable RAM address, so the game tolerates garbage pointers. MIO0
decompression is done in `mio0.c`.

**Ours** — `Memory/segment.{h,cpp}`, `Memory/mio0.{h,cpp}`:
- The `SegmentTable` is **sparse**: only segments the scripts load are populated; the
  rest hold empty spans. `SegmentTable::data()` bounds-checks and throws
  `std::out_of_range` (the original silently reads RDRAM). Every decoder guards those
  throws (the DL interpreter's `run()` catches them; `getNextCommand`, `TextureDecoder`
  and `LevelExtractor::run` convert them into errors/empty results).
- MIO0: identical algorithm (`decompressMIO0`); we additionally validate the header
  ranges and a decompressed-size upper bound so malformed data returns an error instead
  of overrunning.
- `SegmentTable::loadSegment` bounds-checks the ROM range so a bad `LOAD_RAW` (hacks can
  emit ranges past EOF) cannot build an out-of-bounds span.

**Why**: our sparse table + throwing reads are what makes the extractor safe against the
placeholder/garbage addresses that binary hacks ship (see `Quirks.md` "null /
invalid display-list addresses").

### main segment (0x00) and preset tables

**Original** — the boot entry (`asm/entry.s`) DMA's the whole code+data main segment to
RDRAM `0x80200000` from ROM `0x1000` and BSS-clears `_mainSegmentNoloadStart`; segment 0's
base is `0x80000000`, so a main datum at RDRAM X is `0x00 | (X - 0x80000000)`. The preset
tables (`sMacroObjectPresets`, `sSpecialObjectPresets`) live in main's data and are used
by `spawn_macro_objects`/`spawn_special_objects`.

**Ours** — `LevelExtract::loadMainSegment` locates main by the boot-entry prologue and
loads it into seg 0 with **within-main offsets** (0x00 offset 0 = ROM 0x1000; the game's
seg-0 address = within-main offset + 0x200000). The preset tables are found by a
**model-column fingerprint** (a run of known model ids), so binary hacks that move main
are still handled. We parse both tables; special presets are indexed by their stored
`presetID` (not consecutive past entry 40) and end at the `special_null_end` (0xFF)
sentinel.

---

## 3. Level script (`src/engine/level_script.c`)

**Original** — a state machine that loads segments, builds the scene graph, spawns
objects/warps/whirlpools, configures music/camera, then pauses at `CALL_LOOP` while the
game renders. Memory comes from a per-level pool (`alloc_only_pool`). `CALL`/`CALL_LOOP`
run MIPS functions; `SLEEP`/`DELAY` pause across frames.

**Ours** — `LevelScriptVM` runs the script **once to completion** and stops at
`CALL_LOOP` (the "level loaded" point). Differences:

- **Control flow is faithful** (jump/loop/conditional/skip/get-or-set); the game-global
  registers (`gCurrSaveFileNum`, `gCurrCourseNum`, `gCurrActNum`, `gCurrLevelNum`,
  `gCurrAreaIndex`) are mirrored.
- **`CALL` / `CALL_LOOP`** are not executed (MIPS): we skip `CALL`, and `CALL_LOOP`
  terminates the run.
- **`SLEEP`/`SLEEP2`** are skipped (a static run does not pause).
- **Pool commands** (`PUSH/POP_POOL`, `ALLOC/FREE_POOL`, `LOAD_TO_FIXED_ADDRESS`,
  `LOAD_MARIO_HEAD`) are no-ops — we don't emulate the memory pool.
- **Runtime-only commands** (`LOAD_AREA`/`UNLOAD_AREA`, `CMD2C`/`CMD2D`) are no-ops.
- **Data we now RECORD but the game executes** (Phase 2): `WARP_NODE` (0x26),
  `PAINTING_WARP_NODE` (0x27), `INSTANT_WARP` (0x28), `WHIRLPOOL` (0x3B),
  `SHOW_DIALOG` (0x30), `SET_BACKGROUND_MUSIC` (0x36), `TRANSITION` (0x33), `CMD3A`
  (0x3A), `INIT_MARIO` (0x25). The game allocates warp/whirlpool structs and plays
  music; we store the raw values on `Level`/`Area` (exposed on `LevelExtract::Result`).
  Whirlpool *conditions* are evaluated at runtime in the game (save-file/act state); we
  keep the raw condition.
- **Area→camera wiring**: `level_cmd_begin_area` (0x1F) reads the geo root's `views[0]`
  camera node. We do the same via the geo views registry (Phase 2), storing a **value
  copy** of the camera (not a dangling scene-graph pointer) on the result.
- **`SET_MARIO_START_POS`** (0x2B): the game fills a `SpawnInfo`; we store pos + yaw on
  `Level` (and `Result`).
- **Entry point**: we run `level_main_scripts_entry` from seg 0x15 offset 0 (not the
  jump table) so common models (stars, coins, ...) load; the menu is skipped because
  `level_main_menu_entry_2`'s `JUMP_IF(reg == 0)` jumps straight to `EXIT` (reg is never
  set before the menu). See `Quirks.md` "running level_main_scripts_entry".
- **`LOAD_RAW` (0x17)**: SM64-editor hacks redefine this command; we special-case their
  `0x100+seg` encoding. See `Quirks.md`.

---

## 4. Geo layout & scene graph (`src/engine/geo_layout.c`, `graph_node.c`,
`rendering_graph_node.c`)

**Original** — `process_geo_layout` builds the graph once; `geo_process_*` walks it
every frame with per-object/per-frame context: switch-case selected by a function (anim
state, room), LOD selected by camera distance, `GEO_ASM` nodes call a function that
emits a runtime DL (movtex water, envfx, paintings), billboard nodes are rendered
**always facing the camera** (`geo_process_camera` pushes the camera look-at onto the
modelview stack, so `mtxf_billboard`'s `R_z(camera roll)` — which keeps only the
parent's position row — is axis-aligned in *camera space*), `GEO_CAMERA` builds the
look-at matrix + viewport, shadow/culling use runtime state.

**Ours** — `GeoLayoutProcessor` builds the graph once (frame-0 static). Differences:

- **Out-of-range geo addresses**: `processGeoLayout` catches a segment read past the
  loaded data and returns `nullptr` instead of throwing; the model/area is skipped. The
  game reads garbage at such addresses without checking (e.g. a binary hack's
  `script_func_global_16` adds `LOAD_MODEL_FROM_GEO(MODEL_UNKNOWN_DOOR_2A, 0x005F9FE0)`
  — seg-0 beyond the main segment). `LevelExtractor::run` also guards `runLevelScript`
  with try/catch so any residual out-of-bounds data is an extraction error, not a crash.
- **Guarded-exception logging**: every guarded `std::out_of_range` / skipped geometry /
  decode failure is recorded into a per-extraction `WarningLog` (`cpp/src/Log.h`) —
  level script paused on a read past its segment, unknown command, geo address past its
  segment (with address + segment size), display list command past its segment, terrain
  / object-collision decode failure, texture decode failure, and the top-level `run()`
  catches. The log lands in `Result::warnings` and is exposed to Godot via
  `getWarnings()` for a pop-up (vanilla = 0 warnings; the SMTW Dream Edition hack = 2
  geo warnings per level).
- **Switch-case (0x0E)**: the game's case-selection function (anim state, room) is
  runtime. For **area/room geometry** (`collectDisplayLists`) we now collect **all**
  branches (all rooms) — repeated DLs shared between cases are not deduplicated. For
  **object models** (`collectDisplayListsWithTransform`) we still take `selected_case`
  (frame-0 animation state).
- **LOD (0x0D)**: we take the band containing camera distance 0 (near). The game picks
  by the actual camera distance.
- **Camera (0x0F)**: we now record `mode`, `pos`, `focus`, `func` (Phase 2) and register
  the node as `views[0]`. The game also computes the look-at matrix and viewport each
  frame from these — we don't (Godot does projection). Previously (pre-Phase-2) this
  node was created empty: a documented gap.
- **Generated (0x18, `GEO_ASM`)**: we record `parameter` + `func`. The game calls `func`
  to generate a runtime DL (movtex/water/envfx/paintings). We do not call it; movtex
  *data* is instead extracted by `MovtexDecoder` (Phase 2). Pre-Phase-2 the node carried
  nothing.
- **Background (0x19)**: we store the raw `s16` (`background` = id, or RGBA5551 fill
  color when `func == NULL`) and the function pointer. The game renders the skybox/fill.
  (Pre-Phase-2 we mangled the value as `(id<<16)|id` — fixed.)
- **Culling radius (0x20)**: we store it; the game culls at runtime. (Pre-Phase-2 we
  read the field at the wrong offset — fixed to `s16 @ 0x02`.)
- **Views (0x06/0x1B)**: we now maintain the views registry (`ASSIGN_AS_VIEW`) and
  `COPY_VIEW` creates an object-parent node. The shared child of a copied view is
  runtime-only, so the copied node is structurally empty.
- **Ortho/perspective (0x09/0x0A)**: we record `scale` / `fov`/`near`/`far` (+ optional
  frustum func). The game sets the projection matrix; we leave projection to Godot.
- **Perspective command length**: `GEO_CAMERA_FRUSTUM_WITH_FUNC` is 12 bytes (opcode +
  fov/near/far + func ptr). `geo_layout.c` advances only 0x08; we advance by the actual
  command length (12 when the func flag is set) or the stream mis-aligns. (Decomp quirk.)
- **Animated parts / billboards**: `GEO_ANIMATED_PART` animation is baked by
  `ObjectExtract::Frame0Animator` at the game's first rendered frame — mirroring
  `geo_obj_init_animation` (`animFrame = startFrame - 1`) plus the first
  `geo_update_animation_frame`: normally advances to `startFrame`, but `ANIM_FLAG_2`
  (0x04) does not advance (`startFrame-1` forward / `startFrame+1` backward), and a
  `startFrame` outside the loop range wraps (`>= loopEnd` → `loopStart`, or `loopEnd-1`
  with `ANIM_FLAG_NOLOOP`; backward `< loopStart` → `loopEnd-1`, or `loopStart` with
  NOLOOP). The first part's translation type comes from `ANIM_FLAG_HOR_TRANS` (0x08,
  only Y) / `ANIM_FLAG_VERT_TRANS` (0x10, only X/Z) / `ANIM_FLAG_6` (0x40, none) — using
  frame 0 or wrong flag bits laid objects with `startFrame != 0` too low, e.g. castle
  toads sunk 9.4 units. Each part's rotation uses `mtxfRotationXYZ` (= decomp
  `mtxf_rotate_xyz_and_translate`, the matrix `geo_process_animated_part` uses), composed
  as `R × T` (rotation first, then the part translation in the *unrotated* parent frame,
  matching the decomp's direct matrix build). `GEO_ROTATION`/`GEO_TRANSLATE_ROTATE` use
  `mtxfRotationZXY` instead (matching `mtxf_rotate_zxy_and_translate`, also `R × T`).
  The two conventions differ for non-yaw rotations — using ZXY for animated parts laid
  animated objects flat (e.g. castle toads), and `T × R` rotated part pivots into wrong
  positions (Whomp feet/hands, King Bob-omb's two feet collapsing onto each other).
  Every
  `GEO_ANIMATED_PART` (with or without a display list) consumes a frame value and applies
  its transform; the no-DL nodes' `{0,0}` addresses are skipped at decode.
- **Billboards (GEO_BILLBOARD / 0x0D, object-level GRAPH_RENDER_BILLBOARD)**: the game's
  `geo_process_billboard` builds `mtxf_billboard(parent, translation, camera_roll)`
  (`math_util.c`): the result has the parent's *position only* (parent rotation AND
  parent scale are discarded) and a pure `R_z(camera roll)` rotation. Because the
  modelview stack under `geo_process_camera` is already in camera space, this is
  axis-aligned **in camera space** — billboarded quads always face the camera (roll≈0 →
  upright; `obj_behaviors.c` calls coins/trees "billboard" in this sense). The position
  row uses the full parent chain (rotations/scales included) → the billboard pivot.
  `geo_process_object` does the same for objects carrying `GRAPH_RENDER_BILLBOARD`
  (behavior command 0x21, e.g. `bhvTree`, and `obj_set_billboard`): their face angle is
  ignored entirely.
  We bake each geo-level billboard as the pivot only (identity rotation, roll statically
  0), split the subtree triangles per billboard node into `ObjectModel::billboard_parts`
  (`{pivot, pivot-relative mesh}`), and the renderer places each part on a node at the
  pivot with its `global_basis` set to the **camera's world basis** every frame (the
  game's world orientation = view⁻¹·R_z(roll) = the camera basis). Object-level
  billboard objects get the same per-frame basis on their body node (`getObjects`
  exports `billboard`; `main.gd` registers it). In-game the object scale is applied to
  billboard children *twice* (object root + `geo_process_billboard`); we apply it once
  via the instance node — a documented deviation that only matters for scale ≠ 1.
  See `Quirks.md`.
- **Root / master list / shadow / object-parent / held-object**: recorded structurally;
  the master list is empty (runtime), shadow geometry is not generated, held-object
  records `playerIndex`/`func` (Phase 2).

---

## 5. Display list / RSP (fast3d) (`rsp/fast3d.s`, `include/PR/gbi.h`)

**Original** — the RSP microcode maintains DMEM state (matrix stack, vertex buffer,
lights, look-at, fog, viewport, segment table, geometry/other modes), transforms +
lights + clips vertices, and issues RDP commands. Fast3d (F3D) — not F3DEX2 — is the
microcode, so all GBI encodings here are the F3D layouts.

**Ours** — `GBI::CommandDecoder` + `DLInterpreter` decode the 8-byte commands in software:
- **Matrix stack**: we keep parallel float and signed Q16.16 model-view stacks. `G_VTX`
  processes position, normal, texture coordinates, lighting, and projection state at
  load time; later matrix or texture changes do not alter the cached vertex. Projection
  matrices are captured, and the exported perspective divide uses the ROM-compatible
  `VRCP` path from `fast3d.s`; Godot still performs the live camera projection. No
  near-plane clipping.
- **Vertices**: the fixed path transforms them with the RSP-style 48-bit accumulator;
  the float position remains available for the normal renderer. Texture scale from
  `G_TEXTURE` is applied at `G_VTX`, and the 4th word is kept as the normal/color.
- **Lights (Phase 2 + fixed shade)**: `gsSPLight` (`G_MOVEMEM G_MV_L0-7`) is parsed into
  `RSPState` (`Light` = col/dir), and `G_MOVEWORD G_MW_NUMLIGHT`/`G_MW_LIGHTCOL`/
  `G_MW_FOG` are parsed. The ambient is at slot `num_lights` (the game's convention;
  `Ambient_t` is only 8 bytes, so `gsSPLight(&light.a, 2)` over-reads into the next
  light — identified by slot, not `dir==0`). `num_lights` defaults to 1 (the game's
  persistent NUMLIGHTS_1; terrain DLs don't set G_MW_NUMLIGHT).
   The C++ path uses signed Q16.16 values, wrapped 48-bit products, the RSP reciprocal
   and reciprocal-square-root ROM paths, and VMULF rounding/saturation for the exported
   `SHADE` bytes. The Godot shaders keep the raw Light_t directions and apply the active model-view
   transform for both static area and object geometry; the captured shade remains
   available for export/tests but is not multiplied into the live renderer a second
   time. Lit materials without captured lights fall back to white (no modulation).
- **Light arithmetic**: the fixed path now mirrors the RSP's signed 16-bit vector lanes,
  48-bit accumulator overflow, VMULF fractional rounding, and signed saturation for
  matrix/light products and shade accumulation. Remaining matrix/vector edge cases and
  exact camera/depth behavior are still Milestone 1 validation work.
- **`G_TEXTURE`**: F3D on-bit = `w0 bit 0`; tile/lod (bits 8-13) recorded. We always
  sample render tile 0 (SM64's convention); multi-tile/mipmap DLs are not emulated.
- **`G_TEXTURE_GEN`** (environment mapping): not emulated (the star's reflection-mapped
  UVs; the RSP generates them from the view). Documented in `WORKLOG.md` "Next".
- **`G_SETOTHERMODE_H/L` (0xBA/0xB9) and `G_RDPSETOTHERMODE` (0xEF)**: parsed (F3D:
  `w0 = (op<<24)|(sft<<8)|(len)`), so the LUT type / cycle type / texture filter are
  known. `G_SETRENDERMODE` is not parsed — SM64 DLs don't set it; the per-layer render
  modes are applied by layer (see "Drawing layers" below).
- **`G_LOADTLUT` (0xF0)**: recorded as a tmem → palette-image binding; CI textures are
  decoded through it (Phase 2). TMEM itself is not emulated — textures are decoded
  directly from DRAM (linear layout equals "DRAM → TMEM" for our purposes).
- **`G_CULLDL` (0xBE)**: skipped (runtime frustum culling).
- **`G_SETTILE`**: palette + line fields captured (Phase 2) for CI textures.
- **Geometry mode**: we start at the game's startup default (`G_SHADE|G_SHADING_SMOOTH|
  G_CULL_BACK|G_LIGHTING`, `game_init.c:120`). The RSP starts at 0 and the game sets it.
- **Persistent RDP render state**: the game renders the scene graph's display lists
  grouped by layer in numeric order (rendering_graph_node.c `geo_process_master_list`)
  and only sets the render **mode** per layer — the combine/prim/env/fog colors/tile/
  geometry mode/texture bindings/lights **persist across DLs**. We mirror this: the
  area's DLs are collected with their layer, sorted ascending, and decoded through ONE
  `DLInterpreter` with a persistent `RSPState` (`reset_state` only for the first DL;
  the initial combine is the RDP reset value 0). `Material` is snapshotted from
  `RSPState` at each triangle (so a DL that sets no combine — e.g. WF's
  `LAYER_TRANSPARENT_DECAL` yellow decal — inherits the preceding DL's G_CC_SHADE).
  The inline object path now adds ordinary object geo/DLs to the same layer-sorted
  extraction stream as area DLs. Each instance enters the `DLInterpreter` with its
  object graph matrix already active, so `G_VTX` captures per-instance position,
  normal, shade, clip, and projection state; the interpreter's RDP/RSP registers persist
  across the combined stream. The legacy model cache remains as the renderer fallback
  for data that cannot be submitted inline.
- **Drawing layers**: every triangle carries its layer (`Mesh::triangle_layers`,
  from the geo node's `flags >> 8` — the game's `geo_append_display_list` buckets the
  same way). `meshDicts` groups surfaces by (material, layer) and exports `layer`.
  The per-layer render modes (`renderModeTable_1Cycle/2Cycle` + gbi.h) are now applied
  to Godot materials: layers **0-3 OPA** (`G_RM_*_OPA_SURF`: no blending — alpha is
  ignored, z-write on) → forced opaque even if the texture/vertex alpha is < 255;
  layer **4** (`G_RM_AA_TEX_EDGE`: edge alpha → coverage) → `TRANSPARENCY_ALPHA_SCISSOR`
  (hard-edge alpha cutout, opaque pass, z-write on); layers **5-7**
  (`G_RM_AA_XLU_SURF`: `Z_CMP` without `Z_UPD`) → alpha blend with
  `DEPTH_DRAW_DISABLED`. Within a layer the game draws in append order; the
  Godot renderer depth-sorts transparent geometry instead (see `Quirks.md`).
- **Untextured color source**: `combineColorSource` decodes the combine mux to decide
  whether the color comes from SHADE (vertex color), PRIMITIVE, or ENVIRONMENT; the
  bridge exports `use_vertex` (= combine uses SHADE) and the prim/env colors, so the
  renderer shows the vertex color (SHADE), prim color, or env color instead of the old
  `prim_color × vertex_color` (which rendered black when prim was unset). **Untextured
  transparency**: `combineAlphaSource` (SHADE → uniform vertex alpha, PRIMITIVE/ENV →
  their alpha) gives a per-material alpha the renderer applies as transparency — WF's
  yellow decal (vertex `{0xff,0xff,0x00,0x80}`) now renders semi-transparent yellow.
- **Textured vs flat**: `textured` = the combine samples TEXEL0/1 (NOT gated on
  `G_TEXTURE_ENABLE`, because some DLs rely on a parent DL's `G_ON`; the RDP render
  state is now persistent across top-level DLs). See `Quirks.md` "textured vs flat".
- **Ignored RDP state**: fog color recorded but fog not rendered; `G_SETCIMG`/`G_SETZIMG`/
  scissor/fill/rect are recognized but not used (runtime render targets/backgrounds).

---

## 6. RDP & textures (`texture.cpp`, the RDP)

**Original** — the RDP samples TMEM via the tile config; formats RGBA/CI/IA/I/YUV at
4/8/16/32-bit; the color combiner does `texel × shade` (or prim/env) etc.; blend modes
control transparency.

**Ours** — `TextureDecoder` decodes to RGBA8 directly from the DRAM image:

- **Formats (Phase 2)**: RGBA16/RGBA32, CI4/CI8 (via the `G_LOADTLUT` palette + the
  tile's palette index, entries read as RGBA16 or IA16 per the OTHERMODE LUT type),
  IA16/IA8/IA4, I8/I4, with 4-bit nibble packing. YUV is not implemented (RDP almost
  never uses it).
- **Combine**: we only classify textured-vs-flat by whether the combine references
  TEXEL0/1, and we do not emulate the full combine math (prim/env modulation, decal, fog
  blend). Lit materials get `texel × shade` (textured) or `shade` (flat) from the
  per-vertex lighting; unlit colors are the vertex/prim color. IA/RGBA shape textures
  (e.g. the 0x900BC00 overlay) decode as their raw RGB.
- **UV convention**: N64 t=0 is the texture top; Godot ARRAY_TEX_UV v=0 is also the top,
  so t maps directly to v — **no V flip** (a flip turns every texture upside down).
- **Clamp/repeat**: from `G_SETTILE` cms/cmt; Godot has one repeat flag for both axes, so
  asymmetric tiles fall back to repeat (SM64 tiles are usually symmetric).
- **Transparency**: derived from decoded alpha; the RDP's blend-mode coefficients are not
  replicated.

---

## 7. Collision (`src/engine/surface_load.c`, `src/game/macro_special_objects.c`)

**Original** — `load_static_surfaces`/`read_vertex_data` parse the terrain stream and
build a 16×16 **spatial partition** with per-cell floor/ceiling/wall node lists; surface
normals/origin-offset/lower-upper Y are computed; object collision models are transformed
by the object matrix and added to the dynamic partition; environment regions (water
boxes) and rooms are assigned.

**Ours** — `CollisionDecoder` parses the same stream:

- Vertices, surfaces (`{type, v1,v2,v3, force, room}`), special objects, water boxes,
  rooms — all decoded.
- **No spatial partition** (cells/floor-ceiling-wall node lists): the game builds it for
  physics; we don't need it for rendering.
- **Surface flags/classification/lower-upper Y (Phase 2)**: we compute them in
  `finalizeSurfaces` (NO_CAM_COLLISION for types 0x76/0x77/0x78/0x7A; floor/ceiling/wall
  by normal-y; X_PROJECTION for walls with |nx|>0.707; lowerY = minY-5, upperY = maxY+5)
  — mirroring `surface_load.c` — but nothing consumes them yet.
- **Object collision (Phase 2)**: `CollisionDecoder::runObject` decodes an object's
  `LOAD_COLLISION_DATA` stream (local-space vertices, `SURFACE_FLAG_DYNAMIC`). The game
  transforms the vertices by the object matrix and inserts them into the dynamic
  partition; we keep local coordinates (transformation is runtime). Objects that set
  collision via C data tables (e.g. `sRotatingPlatformData[...].collisionData`) are not
  handled.
- **Triangle normals**: our `buildTriangleMesh` uses `(b−a)×(c−a)`; the decomp's
  `read_vertex_data` effectively uses `(v2−v1)×(v3−v2)`. Both yield the same face normal
  up to orientation for valid triangles.
- **Special-object extra params**: `CollisionDecoder::run` reads each special object's
  trailing s16s from its preset's SPTYPE, mirroring `spawn_special_objects`
  (`macro_special_objects.c`): 0 = none, 1 = yaw, 2 = yaw+param, 3 = 3 s16s
  (`SPTYPE_UNKNOWN`), 4 = yaw only (`SPTYPE_DEF_PARAM_AND_YROT` — the param comes from the
  preset table's `defParam`, not the stream). An earlier `if (type>=1/2/3)` version
  over-read types 3 and 4 (5 s16s instead of 3/1), which desynced every level whose
  special-object block contains star/key doors (e.g. castle-inside) — fixed with the
  explicit switch.
- **Rooms (per-triangle)**: `TriangleMesh` carries `rooms` (from `Surface::room` /
  `has_room`) so the renderer can show/hide surfaces per room (like the game's
  `geo_switch_area`).

---

## 8. Behavior scripts (`src/engine/behavior_script.c`, `data/behavior_data.c`,
`include/object_fields.h`)

**Original** — `cur_obj_update` runs the script every frame against the object's state
(fields, timers, randomness, `CALL_NATIVE` C functions). The spawn path runs until the
first `BHV_PROC_BREAK`; `BEGIN_LOOP/END_LOOP` loops forever.

**Ours** — `BehaviorScriptVM` statically walks a behavior once (bounded budget, 100k
commands) **directly against the object** (`ObjectExtract::Object`, like the game's
`gCurrentObject`), so `SET/ADD/OR/SUM` field arithmetic applies in command order:

- **`CALL_NATIVE` is opaque** (we can't run C), skipped; random commands (0x13-0x17)
  are skipped too (see the note below).
- **Frame break at the first `BHV_PROC_BREAK`**: `DELAY`/`DELAY_VAR`/`END_REPEAT`
  (0x06)/`END_LOOP`/`BREAK`/`DEACTIVATE` end the spawn-path walk; `END_REPEAT_CONTINUE`
  (0x07) unrolls the loop in the same frame like the game.
- **`DROP_TO_FLOOR` (0x1E)** queries the area terrain via `Collision::findFloorHeight`
  (mirrors `find_floor`/`find_floor_from_list`: highest floor under (x,z), skips
  `SURFACE_CAMERA_BOUNDARY` 0x72, requires `y ≥ floor−78`). We return the **highest**
  floor rather than the game's first-in-cell-list match (a "surface cucking" list-order
  quirk we can't reproduce — deterministic choice).
- **Loops run once** (`BEGIN_LOOP`/`END_LOOP`), then the walk terminates — the game
  re-enters forever; bytes after the loop are a neighbor behavior, not reachable.
- **Child objects**: `SPAWN_CHILD`/`SPAWN_OBJ`/`SPAWN_CHILD_WITH_PARAM` in the spawn
  path are expanded into child objects at the parent's pos/angle (their behavior runs);
  per-frame runtime children (e.g. goomba trios) are still not spawned.
- **`cur_obj_scale` (runtime scale) is not applied** — only `GEO_SCALE` (geo data) and
  the behavior `SCALE` command. See `Quirks.md`.

Not implemented (opcodes we skip, plus spawn-path side effects we don't mirror):

- **Random commands (0x13-0x17)** (`SET_RANDOM_INT/FLOAT`, `SET_INT_RAND_RSHIFT`,
  `ADD_RANDOM_FLOAT`, `ADD_INT_RAND_RSHIFT`): skipped. They only touch water/particle
  fields in vanilla, so no render impact. Note the game's PRNG **is deterministic**
  (`gRandomSeed16` is static, never seeded, starts at 0) — mirroring `random_u16` would
  reproduce the exact values (see WORKLOG Next).
- **`PARENT_BIT_CLEAR` (0x33)**: operates on the parent object's field (Mario's
  particles), skipped.
- **`ANIMATE_TEXTURE` (0x34)**: a per-frame `oAnimState` counter; at frame 0 the game
  increments it once (`gGlobalTimer == 0`), which we omit. No render impact today.
- **`SPAWN_WATER_DROPLET` (0x37)**: spawns runtime water particles; skipped.
- **`BEGIN` special cases** (bhv_cmd_begin): `bhvMessagePanel` gets
  `oCollisionDistance = 150`, `bhvHauntedChair`/`bhvMadPiano` init a room. We only
  record the object list.
- **`create_object` spawn-time effects** (object_list_processor.c): the
  `OBJ_LIST_UNIMPORTANT` → `ACTIVE_FLAG_UNIMPORTANT` flag and the
  GENACTOR/PUSHABLE/POLELIKE `snap_object_to_floor` call (a no-op bug — it snaps to the
  floor beneath the origin before the caller sets the position) are not mirrored.
- **Captured but not applied to rendering**: `oAnimState`, `oDrawingDistance`,
  `oCollisionDistance`. (`oGraphYOffset` IS applied — the bridge exports
  `pos.y + oGraphYOffset` for the model placement; object collision stays at `oPosY`.)
- **`oGraphYOffset`**: the bridge places the model at `oPosY + oGraphYOffset` (mirroring
  `obj_update_gfx_pos_and_angle`); object collision stays at `oPosY`.

---

## 9. Objects & the render pipeline

**Original** — objects are spawned, their behaviors run each frame, and the graphics
node is rendered with the object's transform (pos/angle/scale, possibly billboarded,
frame-animated, with per-object lights/camera). The camera position and lights are set
per frame.

**Ours** —

- **Unified `Object`** (`ObjectExtract::Object`, mirrors `struct Object`): every
  OBJECT/macro/special spawn becomes one `Object` via three static transforms
  (`fromSpawnInfo`/`fromMacroObject`/`fromSpecialObject`), mirroring the game's
  `spawn_object`/`spawn_macro_object`/`spawn_special_objects`. The behavior
  interpreter acts on this `Object` (§8), mutating it in command order.
- **Spawn state**: after the behavior walk the object's position/angle/scale/opacity/
  hidden/model are the frame-0 state; `getObjects` exports hidden/scale/opacity/
  billboard and the renderer skips hidden objects and applies scale + opacity.
  `DROP_TO_FLOOR` snaps spawn-path objects (signposts etc.) to the terrain.
  Runtime `cur_obj_scale`/per-frame children remain non-goals. `oGraphYOffset` is
  applied: `getObjects` exports `pos.y + oGraphYOffset` for the model placement; the
  object collision stays at `oPosY` (see §8).
- **Billboard parts**: `GEO_BILLBOARD` subtrees are split per node into
  `ObjectModel::billboard_parts` (`{pivot, pivot-relative mesh}`) and decoded per
  object instance with the object transform active for fixed G_VTX state. Godot renders
  each part on a second node at the pivot whose `global_basis` tracks the camera's
  world basis every frame (always facing the camera — see §4). Object-level `BILLBOARD`
  (behavior 0x21) likewise decodes a per-instance local body under a camera-facing
  parent; the extraction camera basis is used only for fixed shade/projection state.
- **Object drawing layers**: object-model DLs carry their layer (`flags >> 8`) and are
  decoded in layer order (stable); the merged body/part meshes export per-triangle
  layers, so object surfaces get the same layer-driven materials as the area mesh
  (OPA/TEX_EDGE/XLU — see §5).
- **Frame-0 animation**: the inline path runs a separate `Frame0Animator` while
  collecting each object instance, so different instances do not share a baked pose.
  The model cache still bakes frame-0 as a fallback (`Frame0Animator`, mirrors
  `geo_process_animated_part`). See `Quirks.md`.
- **Spawn transform**: objects are placed at their spawn pos + yaw (macro objects:
  preset yaw converted to SM64 angle units; special objects: the terrain-stream yaw is
  256-per-circle, converted). `oBhvParams`/`oBhvParams2ndByte` are packed game-exactly
  per source.
- **Camera**: the extracted camera/projection context is supplied to the fixed G_VTX
  pass. Godot now adopts the extracted perspective FOV/near/far range while retaining
  free-flight position/orientation controls. It reprojects static and inline meshes
  every frame, and the live model-view light branch follows camera rotation like the
  original master-list submission; exact RSP depth and camera-matrix equivalence remain
  Milestone 1 validation work.
- **Lights**: parsed (Phase 2) and used for live per-vertex shading (`texel × shade` for
  textured-lit, `shade` for flat-lit), so static castle-grounds grass and inline
  goomba/cannon/bobomb parts follow the same camera-dependent model-view timing.
- **Movtex (Phase 2)**: water/lava quads extracted by `MovtexDecoder` (heuristic scan
  for `MovtexQuadCollection` with strict content validation). The game generates the
  animated water DLs each frame. Waterfall vertex-meshes (`MOV_TEX_TRIS` + a tri-DL with
  runtime-injected vertices) are not decoded; the quad Y is left to the consumer (from
  the matching water box). Heuristic — vanilla works (castle-grounds water, LLL lava,
  DDD water); hacks with moved segments may be missed.

---

## 10. Binary (ROM hack) compatibility

Differences that exist only because we target binary ROMs (vanilla + hacks), not the
decomp build. Full details in `docs/Quirks.md`:

- **Sparse segment table + defensive bounds checks** vs the game's benign
  `segmented_to_virtual` memory (Section 2).
- **Preset-table fingerprinting** vs compile-time symbols (Section 2).
- **Main-segment location by boot-entry prologue** (hacks may move it).
- **SM64-editor command 0x17 redefinition**, **faked MIO0 segment 2**, **course-name
  table offset** — `Quirks.md`.
- **Behavior analysis requires an exact address** (behaviors have no end marker);
  we hand it the OBJECT command's behavior pointer.
