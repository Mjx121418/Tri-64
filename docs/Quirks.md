# Super Mario 64 Engine Notes

This file records **quirks of the sm64 engine and the modifications made by ROM hacking
tools** — facts that hold regardless of our implementation. Where our implementation
*deviates* from the engine, that is documented in `docs/Engine.md`; sections here
cross-link to it.

## level script command 0x17

Tri-64 supports vanilla ROMs, SM64 Editor hacks, and the Rom Manager one-bank
seg-`0x0E` layout documented below. Rom Manager-specific area metadata and scrolling
textures are still deferred.

SM64 Editor redefined the level script command 0x17.

## segment 2

In vanilla rom, segment 2 starts at the rom address 0x108A40 and ends at 0x114750.
It is MIO0 compressed.
In sm64 editor rom hacks, segment 2 starts at 0x800000 and ends at 0x81BB64.
It is a faked MIO0 segment, since it has a MIO0 head.
But the data is actually uncompressed.
The uncompressed data starts at 0x803156.

## Rom Manager one-bank segment 0x0E

Rom Manager levels can reuse segment `0x0E` for two different purposes. The initial
seg-`0x0E` load contains the level's object, warp, and scrolling-texture script data.
After the level script has built the area records, the game remaps the same segment to
the active area's Fast3D/model bank before loading terrain and rendering the area. The
area bank is not a second simultaneous segment mapping.

The format is identified by marker `0x4BC9189A` at seg `0x19:0x5FFC`. Each area entry
starts at `0x19:0x5F00 + area * 0x10`; its first two u32 values are absolute ROM start
and exclusive end offsets for the seg-`0x0E` bank. These are ROM offsets, not offsets
relative to the seg-`0x19` level-script bank. The remaining entry bytes include the
Rom Manager 2D-camera flag.

The normal course script usually stops at `CALL_LOOP` before issuing the level-script
`LOAD_AREA` opcode. Runtime startup instead calls `load_mario_area()` from
`level_update.c`, which calls `load_area()` after the level script has populated the
area records. Area changes repeat the operation after unloading the previous area.

Tri-64 mirrors this boundary in `LevelExtractor`: it runs the level script with the
initial seg-`0x0E` mapping, then, for the selected area, validates the marker and ROM
range and calls `SegmentTable::loadSegment(0x0E, start, end)` before collision, display
list, texture, and object-model decoding. If the marker is absent, the existing segment
mapping is left unchanged for vanilla and other hacks. The remaining Rom Manager area
tables and fake scrolling-texture objects are not decoded yet; see `WORKLOG.md`.

## level names

Level names are loaded in segment 2 before level script start to execute.
It starts at the segmented address 0x0210F68.

## null / invalid display-list addresses in object models

SM64-editor hacks (e.g. Super Mario Treasure World) can ship object models whose geo
layouts contain placeholder display lists: GEO_DISPLAY_LIST with a null pointer
(0x00000000) or a garbage segment (0xFF000000). The game runs these with no issues:

- rendering_graph_node.c geo_process_* checks `node->displayList != NULL` before
  queueing a DL, so a null DL is never sent to the RSP.
- segmented_to_virtual(addr) = (sSegmentTable[seg] + offset) | 0x80000000 (memory.c).
  Even an unloaded segment maps to a readable RAM address (0x80000000 = start of RDRAM);
  a garbage segment just reads past the 32-entry table into BSS. There is no MMU
  protection, so the RSP reads whatever bytes are there as fast3d commands — typically
  SP_NOOP or an early terminator — and draws nothing instead of faulting.

Our extractor must be defensive (sparse segment table, bounds-checked reads, skipped
invalid DL addresses): see `Engine.md` §2 / §10.

## object collision data

Objects (tilting platforms, tumbling bridges, doors, ...) carry their own collision
data, separate from the area terrain. It is a simplified terrain stream:

    [0x40] [numVertices] [x y z ...]        (3 s16 per vertex, object-local space)
    [surfaceType] [count] [v1 v2 v3 (+force)] ...   (repeats)
    ... until 0x41 (TERRAIN_LOAD_CONTINUE) ends the data

(No special objects / environment / TERRAIN_LOAD_END.)

Load path (decomp):
- Behavior command LOAD_COLLISION_DATA = 0x2A + 32-bit segmented pointer
  (behavior_data.c:260; bhvCmdSetCollisionData behavior_script.c:731) sets oCollisionData.
- load_object_collision_model() (surface_load.c:754) skips the leading 0x40, reads the
  vertices and transforms them by the object's matrix (transform_object_vertices,
  surface_load.c:658), then reads surface blocks until 0x41.
- Some objects set collision via C data tables indexed by bhvParam instead
  (e.g. sRotatingPlatformData[...].collisionData, sBowserFallingPlatform[...].collision);
  those are NOT plain behavior commands (we don't handle those).

We decode the stream (local-space vertices) in `CollisionDecoder::runObject`:
see `Engine.md` §7.

## object runtime scale (cur_obj_scale)

Some object models are authored at a smaller size and scaled up at runtime by their
behavior via cur_obj_scale(), which is C behavior code, not data. Since the extractor
does not run behaviors, these objects render at their authored (smaller) size.

Known cases:
- MODEL_EXCLAMATION_BOX (0x89): bhv_exclamation_box_loop -> cur_obj_scale(2.0f)
  (exclamation_box.inc.c) — ! boxes render at half size.
- MODEL_BLUE_COIN_SWITCH (0x8C): bhv_blue_coin_switch_loop -> cur_obj_scale(3.0f),
  "The switch's model is 1/3 size" (blue_coin.inc.c) — renders at a third size.

GEO_SCALE nodes (geo command 0x1D, value u32/65536) ARE applied — that is data, not
behavior. Only behavior-set scales are missing. See `Engine.md` §8/§9.

## object animation frame-0 (animated-part placement)

Object models with GEO_ANIMATED_PART nodes get their part translation/rotation from
the object's animation at runtime, not just from the geo. geo_process_animated_part
(rendering_graph_node.c:544) starts from the geo node's translation and ADDS the
animation's frame-0 values:

    translation[0] += gCurrAnimData[retrieve_animation_index(frame, &attr)]
                      * gCurrAnimTranslationMultiplier;

gCurrAnimTranslationMultiplier = oAnimInfo.animYTrans / anim.animYTransDivisor, or
1.0 when the divisor is 0 (rendering_graph_node.c:630).

Static export must therefore apply each animated part's frame-0 animation values
(`ObjectExtract::Frame0Animator`): see `Engine.md` §4/§9. Note: for animations with a
non-zero animYTransDivisor and default animYTrans (0), the multiplier is 0, so the
frame-0 translation contribution is nil — only the door class (divisor 0) shifts.

## billboards always face the camera

Despite what the function's math looks like at first glance, SM64's billboards
(`GEO_BILLBOARD` geo nodes and the object-level `GRAPH_RENDER_BILLBOARD` flag) DO
always face the camera. `geo_process_camera` (rendering_graph_node.c:322) pushes the
camera look-at matrix onto the modelview stack, so all children render in camera
space. `mtxf_billboard` (math_util.c:342) discards the parent matrix's rotation and
scale and builds a pure `R_z(camera roll)` rotation with the parent's position row
only — i.e. the part is axis-aligned *in camera space*, which is exactly "facing the
camera" (rotated by the camera roll around the view axis; roll is ~0 in gameplay).
`geo_process_billboard` (rendering_graph_node.c:440) then re-applies the object
scale. The position row uses the full parent chain (rotations/scales included) — the
billboard pivot. The object-level flag additionally makes `geo_process_object` ignore
the object's face angle; `obj_behaviors.c` calls coins/trees "billboard" objects in
this sense (bhvTree = BILLBOARD, data/behavior_data.c).

Consequences we mirror (roll = 0) — see `Engine.md` §4/§9:
- Billboard parts keep only the parent's position for their pivot; ancestor
  rotation/scale do not affect the part's own geometry.
- The object scale is applied to billboard children TWICE (object root +
  `geo_process_billboard`); we apply it once via the instance node.

## behavior scripts (segment 0x13)

Behavior scripts are fixed arrays of u32 words at segment address 0x13
(BEGIN_SEG(behavior, 0x13000000) in sm64.ld; loaded by LOAD_RAW(0x13, ...) at the
top of level_main_scripts_entry). The opcode is the top byte; arguments are packed
into the low bytes and following words (see data/behavior_data.c macros). Commands
are 1/2/3/5 words long; the lengths follow the command-number table in
src/engine/behavior_script.c / BehaviorCmdTable.

Segment 0x13 is one of the five common segments loaded by the level-script VM when
it starts at `level_main_scripts_entry` (the first commands load 0x04, 0x03, 0x17,
0x16, and 0x13), so behaviors are reachable with the same SegmentTable used for the
level script.

The game runs a behavior every frame against the object's state; we walk it once,
statically (`BehaviorScriptVM`) — see `Engine.md` §8 for the deviations (CALL_NATIVE
opaque, DELAY/loop termination, field-write capture, no runtime state). Random
commands (0x13-0x17) are skipped today, but the game's PRNG is deterministic:
`gRandomSeed16` (behavior_script.c) is a static that starts at 0 and is never seeded,
so mirroring `random_u16` would reproduce the exact values (planned — see WORKLOG).

Because behaviors are packed contiguously with no end marker, the caller must hand
the walker the exact address of a behavior's BEGIN command (e.g. an OBJECT command's
behavior pointer, or a known signature scan); walking "backwards" into a neighbor is
not reliable.

## main segment (0x00) and macro/special preset tables

The game's "main" segment (all game code + data, incl. the macro/special preset
tables) is NOT loaded by level scripts. The boot entry (asm/entry.s, at ROM 0x1000)
BSS-clears `_mainSegmentNoloadStart` (0x8034A580) for `_mainSegmentNoloadSize`, so
main runs at RDRAM 0x80200000 from ROM 0x1000; the main loaded size =
0x8034A580 - 0x80200000 = 0x14A580. Segment 0's base in the game is 0x80000000
(game_init.c set_segment_base_addr(0, 0x80000000)), so a main datum at RDRAM X is
reachable as segmented 0x00 | (X - 0x80000000). Our SegmentTable loads main at
seg 0x00 with within-main offsets (0x00 offset 0 = ROM 0x1000); the game's seg-0
address = within-main offset + 0x200000.

The preset tables live in main's data:
- sMacroObjectPresets: 8-byte entries { behavior:u32 (0x13 ptr), model:s16, param:s16 },
  366 entries in enum order.
- sSpecialObjectPresets: 8-byte entries { presetID:u8, type:u8, defParam:u8, model:u8,
  behavior:u32 }. NOT ordered by presetID past entry 40 (only some enum values are in
  the table); the last entry is the special_null_end (0xFF) sentinel. The game finds a
  preset by linear scan.
- Behavior params: macro objects combine the preset's default param with the entry's
  (preset.param != 0 replaces the entry param's low byte, spawn_macro_objects);
  special objects pass type-dependent params (SPTYPE_DEF_PARAM_AND_YROT uses
  defParam, SPTYPE_PARAMS_AND_YROT the entry's extra param).

We locate both tables by a model-column fingerprint (binary-friendly): see `Engine.md`
§2 / §10.

## texture UV convention and clamp/repeat

The RDP's texture t-coordinate is top-down (t=0 = texture top, matching the image's
row 0). Godot's ArrayMesh ARRAY_TEX_UV also uses v=0 as the top, so the N64 t maps
directly to v — do NOT flip the V axis (a `v = 1 - v` flip turns every texture
upside down; most visible on directional textures like trees, invisible on
vertically symmetric ones like bobombs).

The RDP clamps or wraps texture coordinates per G_SETTILE's 2-bit cms/cmt mode
(0=WRAP, 1=MIRROR, 2=CLAMP; w1 bits 8-9 and 18-19). Godot has one repeat flag for
both axes, so asymmetric tiles (S wrap, T clamp) fall back to repeat — SM64 tiles
are usually symmetric. See `Engine.md` §6.

## native-selected object animations (default index 0)

Some behaviors LOAD_ANIMATIONS but never issue ANIMATE — the animation index is
chosen by a CALL_NATIVE init/update function (e.g. bhvGoomba: goomba_update calls
cur_obj_init_animation_with_accel_and_sound(0, ...)). We default the frame-0 bake to
animation index 0 (the game's first animation, usually the rest pose). See
`Engine.md` §8/§9.

## running level_main_scripts_entry from the top

Models that appear in every level (MODEL_STAR, coins, 1-ups, red/blue coins, ...)
are loaded only by level_main_scripts_entry via LOAD_MODEL_FROM_GEO, before the
level table. We run the whole main entry from seg 0x15 offset 0 with curr_level_num
set; the menu EXECUTE is skipped because level_main_menu_entry_2's JUMP_IF(OP_EQ, 0,
+42) jumps straight to EXIT when reg == 0. See `Engine.md` §3.

## textured vs flat (combine mode)

The RDP samples the texture only when G_SETCOMBINE's color/alpha A/B/C/D muxes
reference TEXEL0/TEXEL1 (1/2). We classify `textured` by this alone (NOT gated on
G_TEXTURE_ENABLE, because some DLs rely on a parent DL's G_ON). G_LIGHTING is
recorded as Material.lit. Full details (flat-geometry color, mux bit layout, default
geometry mode) in `Engine.md` §5/§6.
