# Nintendo 64 RDP command-state specification

This document models the Reality Display Processor (RDP) as a command-driven state machine. Its primary references are the attached **SGI RDP Command Summary, version 2.0 (1996)** and the N64brew pages [RDP Commands](https://n64brew.dev/wiki/Reality_Display_Processor/Commands), [RDP Pipeline](https://n64brew.dev/wiki/Reality_Display_Processor/Pipeline), and [RDP Hazards](https://n64brew.dev/wiki/Reality_Display_Processor/Hazards). When the 1996 summary and later hardware-tested documentation disagree, the behavior documented by N64brew is used.

## 1. Command stream conventions

The RDP consumes big-endian 64-bit words. In this document, `W0` is the first 64-bit word of a command, `W1` the next, and so on. Bit 63 is the most significant bit.

The hardware opcode is only six bits:

```text
opcode = (W0 >> 56) & 0x3F
```

Bits 63:62 are not part of the RDP opcode. RSP graphics microcodes and SDK headers normally set them to `11`, so the first byte seen in a display list is commonly `0xC0 | opcode`: for example, raw byte `0xE4` is RDP opcode `0x24` (Texture Rectangle), and raw byte `0xF5` is RDP opcode `0x35` (Set Tile). A raw-RDP decoder must mask the byte with `0x3F`.

This specification begins at the raw RDP command stream, after any RSP microcode translation. In an RSP GBI display list, the same first bytes can have microcode packaging semantics; notably `0xF1` is commonly `RDPHALF_2` even though opcode `0x31` is a no-op once presented directly to the RDP.

Except for triangle packets and texture rectangles, commands occupy one 64-bit word. A texture rectangle occupies two 64-bit words. Triangle packet length is encoded by opcode bits 2:0.

Fixed-point notation is `uI.F` for an unsigned value with `I` integer and `F` fractional bits, and `sI.F` for one sign bit, `I` magnitude/integer bits, and `F` fractional bits. Decode a signed `N`-bit field by sign-extending bit `N-1`, then divide by `2^F`.

Two corrections to the 1996 summary are important for decoding:

- Sync Load is opcode `0x26`, not `0x31`. Opcode `0x31` behaves as a no-op.
- Image addresses are effectively the low 24 bits, and image width is effectively the low 10 bits plus one. The summary labels a wider address field, while some SDK macros accept a wider width field; those extra bits do not enlarge the effective RDP state.

All reserved bits should be ignored on decode and written as zero when generating commands.

## 2. Complete persistent and transient state

At power-on, software must treat every attribute register, every tile descriptor, TMEM, and pipeline history latch as undefined. A command changes only the state named in its transition; every other state persists.

```text
RdpState {
  texture_image: { format[3], size[2], width[10], address[24] }
  color_image:   { format[3], size[2], width[10], address[24] }
  depth_image:   { address[24] }

  tmem: byte[4096]
  tile[8]: {
    format[3], size[2], line_words[9], tmem_word[9], palette[4],
    clamp_t, mirror_t, mask_t[4], shift_t[4],
    clamp_s, mirror_s, mask_s[4], shift_s[4],
    sl[12], tl[12], sh[12], th[12]
  }

  scissor: { xh[12], yh[12], field, keep_odd, xl[12], yl[12] }

  combine: {
    rgb_a[2], rgb_b[2], rgb_c[2], rgb_d[2],
    alpha_a[2], alpha_b[2], alpha_c[2], alpha_d[2]
  }

  other_modes: {
    atomic_prim, cycle_type[2], persp_tex_en, detail_tex_en,
    sharpen_tex_en, tex_lod_en, tlut_en, tlut_type, sample_type,
    mid_texel, bi_lerp_0, bi_lerp_1, convert_one, key_en,
    rgb_dither_sel[2], alpha_dither_sel[2],
    blender[2]: { p[2], a[2], m[2], b[2] },
    force_blend, alpha_cvg_select, cvg_x_alpha, z_mode[2],
    cvg_dest[2], color_on_cvg, image_read_en, z_update_en,
    z_compare_en, antialias_en, z_source_sel,
    dither_alpha_en, alpha_compare_en
  }

  env_color:   rgba8
  prim_color:  { min_level[8], prim_lod_frac[8], rgba8 }
  blend_color: rgba8
  fog_color:   rgba8
  fill_color:  uint32
  prim_depth:  { z[16], dz[16] }
  convert:     { k0:s9, k1:s9, k2:s9, k3:s9, k4:s9, k5:s9 }
  key: {
    red:   { width[12], center[8], scale[8] },
    green: { width[12], center[8], scale[8] },
    blue:  { width[12], center[8], scale[8] }
  }

  pipeline: {
    command_fifo,
    raster_or_load_work_in_flight,
    span_buffers,
    pending_rdram_reads_and_writes,
    previous_and_next_pixel_pipeline_latches,
    dp_interrupt_pending,
    full_sync_halted
  }
}
```

RDRAM and its hidden per-pixel metadata are external memory, not RDP registers, but they are part of the state-transition environment. Texture loads read RDRAM and write TMEM. Drawing reads and writes the selected color/depth images and their coverage/depth metadata.

### 2.1 State ownership

| State | Commands that write it | Commands/stages that read it |
|---|---|---|
| Texture image | Set Texture Image | Load Tile, Load Block, Load TLUT |
| Color image | Set Color Image | All drawing pipelines; image read; color write |
| Depth image | Set Depth Image | Depth compare and depth update |
| TMEM | Load Tile, Load Block, Load TLUT | Texture sampling |
| Tile descriptor base fields | Set Tile | Texture load and texture sampling |
| Tile extents | Set Tile Size; also all three load commands | Coordinate clamp and load bookkeeping |
| Scissor | Set Scissor | Rasterizer |
| Combine muxes | Set Combine Mode | Color combiner |
| Other Modes | Set Other Modes | Rasterizer, texture unit, combiner adjuncts, depth, blender, dither, writes |
| Environment/primitive colors | Set Environment Color, Set Primitive Color | Color combiner |
| Blend/fog colors | Set Blend Color, Set Fog Color | Blender; blend alpha is also an alpha-test threshold |
| Fill color | Set Fill Color | Fill pipeline only |
| Primitive depth | Set Primitive Depth | Depth stage when `z_source_sel = 1` |
| Conversion coefficients | Set Convert | Texture filter and combiner K4/K5 inputs |
| Key parameters | Set Key R, Set Key GB | Combiner center/scale inputs and chroma-key stage |
| Pipeline/transient state | Draw, load, no-op, and sync commands | Subsequent commands and in-flight pixels |

### 2.2 TMEM and tile state

TMEM is 4096 bytes, addressed by Set Tile in 64-bit words (`byte_address = tmem_word * 8`). It is physically banked; the state-machine abstraction may store a 4096-byte logical image, but load and sample functions must reproduce the RDP's row interleaving and special formats.

- Ordinary 4/8/16-bit rows use an odd-row 32-bit word swap for bank access. Load Tile performs this rearrangement. Load Block performs it according to its `dxt` line-parity accumulator.
- RGBA32 and YUV use both halves of TMEM in a split layout. Their base address must be in the lower half, and their data must fit there in the format-specific sense. They cannot coexist with a CI TLUT in the upper half in the usual layout.
- TLUT entries occupy the upper 2048 bytes. Load TLUT replicates each 16-bit entry four times, so 256 entries occupy all 2048 bytes.

Each of the eight tile descriptors has two independently updated parts:

1. Set Tile writes format, size, stride, TMEM base, palette, and S/T address controls.
2. Set Tile Size or a load command writes `sl,tl,sh,th` without changing the other fields.

The address controls are applied per axis. Shift is applied first; the tile upper-left coordinate is then subtracted; clamp, mirror, and mask determine the final local coordinate. `mask = n` wraps to `2^n` texels; `mask = 0` applies no bit mask. Mirroring alternates direction across mask-sized periods. Shift values mean:

| Raw shift | Coordinate operation |
|---:|---|
| 0 | unchanged |
| 1..10 | arithmetic/right shift by 1..10 |
| 11..15 | left shift by `16 - raw`, namely 5..1 |

### 2.3 Pipeline timing and live global state

Attribute setters change their registers when the command reaches the command processor. An already-issued primitive does not snapshot every global register at issue time: later pipeline stages can observe a newly written value. Therefore, an unsynchronized attribute change can render the tail of an earlier primitive with new state.

The four global attributes documented as safe to change without a preceding sync are Primitive Color, Primitive Depth, Scissor, and Tile Size. Texture Image is not read by the rendering pipeline, but transitions between rendering and loading still require the appropriate load/pipe synchronization. For all other live rendering attributes, insert a Pipe Sync after prior drawing and before the setter. Insert Tile Sync before changing a tile descriptor still used by earlier drawing. Insert Load Sync when crossing into the loading pipeline.

A functional, non-cycle-accurate state machine may model every draw/load as completing before the next command. A cycle-accurate model must retain the FIFO, in-flight per-pixel work, span buffers, memory operations, and history latches because invalid combiner sources and unsynchronized updates expose previous/next-pixel values.

## 3. Opcode and packet-length table

| RDP opcode | Common first byte | Command | 64-bit words |
|---:|---:|---|---:|
| `00-07` | `C0-C7` | No Operation | 1 |
| `08` | `C8` | Triangle | 4 |
| `09` | `C9` | Triangle + Z | 6 |
| `0A` | `CA` | Triangle + Texture | 12 |
| `0B` | `CB` | Triangle + Texture + Z | 14 |
| `0C` | `CC` | Triangle + Shade | 12 |
| `0D` | `CD` | Triangle + Shade + Z | 14 |
| `0E` | `CE` | Triangle + Shade + Texture | 20 |
| `0F` | `CF` | Triangle + Shade + Texture + Z | 22 |
| `10-23` | `D0-E3` | No Operation | 1 |
| `24` | `E4` | Texture Rectangle | 2 |
| `25` | `E5` | Texture Rectangle Flip | 2 |
| `26` | `E6` | Sync Load | 1 |
| `27` | `E7` | Sync Pipe | 1 |
| `28` | `E8` | Sync Tile | 1 |
| `29` | `E9` | Sync Full | 1 |
| `2A` | `EA` | Set Key GB | 1 |
| `2B` | `EB` | Set Key R | 1 |
| `2C` | `EC` | Set Convert | 1 |
| `2D` | `ED` | Set Scissor | 1 |
| `2E` | `EE` | Set Primitive Depth | 1 |
| `2F` | `EF` | Set Other Modes | 1 |
| `30` | `F0` | Load TLUT | 1 |
| `31` | `F1` | No Operation at the RDP level | 1 |
| `32` | `F2` | Set Tile Size | 1 |
| `33` | `F3` | Load Block | 1 |
| `34` | `F4` | Load Tile | 1 |
| `35` | `F5` | Set Tile | 1 |
| `36` | `F6` | Fill Rectangle | 1 |
| `37` | `F7` | Set Fill Color | 1 |
| `38` | `F8` | Set Fog Color | 1 |
| `39` | `F9` | Set Blend Color | 1 |
| `3A` | `FA` | Set Primitive Color | 1 |
| `3B` | `FB` | Set Environment Color | 1 |
| `3C` | `FC` | Set Combine Mode | 1 |
| `3D` | `FD` | Set Texture Image | 1 |
| `3E` | `FE` | Set Depth Image / Set Z Image / Set Mask Image | 1 |
| `3F` | `FF` | Set Color Image | 1 |

## 4. No Operation (`0x00-0x07`, `0x10-0x23`, `0x31`)

| Word | Bits | Field |
|---|---:|---|
| W0 | 61:56 | opcode in one of the ranges above |
| W0 | all others | ignored |

The command changes no persistent attribute or memory. Hardware tests summarized by N64brew indicate that these opcodes consume one command/pipeline cycle. The broad no-op ranges are not described by the old command summary and remain less formally documented than opcode `0x00`.

State transition:

```text
attributes' = attributes
memory' = memory
pipeline.time += 1 GCLK
```

## 5. Triangle packets (`0x08-0x0F`)

The low three opcode bits select optional state carried inside the primitive packet:

```text
shade   = opcode bit 2
texture = opcode bit 1
zbuffer = opcode bit 0
```

The packet is concatenated in this order: four edge words, eight shade words if enabled, eight texture words if enabled, then two Z words if enabled. These coefficients are primitive-local transient state; they do not overwrite any persistent RDP attribute register.

### 5.1 Edge/base words

| Word | Bits | Field | Interpretation |
|---|---:|---|---|
| W0 | 61:56 | opcode | `0x08..0x0F` |
| W0 | 55 | `lmajor` / flip | 1: `XH` is left edge and spans proceed left-to-right; 0: `XH` is right edge |
| W0 | 53:51 | `level` | maximum mip level, zero-based (`number of levels - 1`) |
| W0 | 50:48 | `tile` | base tile descriptor |
| W0 | 45:32 | `YL` | lowest/largest Y, signed `s11.2`, exclusive end |
| W0 | 29:16 | `YM` | middle Y, signed `s11.2`, minor edge switches here |
| W0 | 13:0 | `YH` | highest/smallest Y, signed `s11.2`, start |
| W1 | 59:32 | `XL` | X on low minor edge at `YM`, signed `s11.16` |
| W1 | 29:0 | `DxLDy` | low minor-edge slope, signed `s13.16` |
| W2 | 59:32 | `XH` | X on major edge at `floor(YH)`, signed `s11.16` |
| W2 | 29:0 | `DxHDy` | major-edge slope, signed `s13.16` |
| W3 | 59:32 | `XM` | X on middle minor edge at `floor(YH)`, signed `s11.16` |
| W3 | 29:0 | `DxMDy` | middle minor-edge slope, signed `s13.16` |

Bits 54, 47:46 and 31:30 in W0, and the high padding bits of X/slope halves, are unused. In a bit-accurate implementation, some low slope bits have less effective precision than their packet storage suggests.

The rasterizer advances the major edge from `YH` to `YL`. For `YH <= y < YM`, the minor edge is `XM + (y-floor(YH))*DxMDy`; from `YM <= y < YL`, it is `XL + (y-YM)*DxLDy`. The major edge is `XH + (y-floor(YH))*DxHDy`. `lmajor` chooses which result is the left/right span boundary. Scissor and subpixel coverage rules then determine the pixels emitted.

`tile` is the base texture tile. With texture LOD enabled, `level` limits how far the tile selector may advance through the eight descriptors; tile arithmetic wraps modulo 8.

### 5.2 Shade suffix

When shade is enabled, append eight words. Each RGBA quantity is reconstructed by concatenating its signed integer half from an even-numbered suffix word with its unsigned 16-bit fractional half two words later.

| Suffix word | Bits 63:48 | Bits 47:32 | Bits 31:16 | Bits 15:0 |
|---:|---|---|---|---|
| S0 | `R.i` | `G.i` | `B.i` | `A.i` |
| S1 | `dRdx.i` | `dGdx.i` | `dBdx.i` | `dAdx.i` |
| S2 | `R.f` | `G.f` | `B.f` | `A.f` |
| S3 | `dRdx.f` | `dGdx.f` | `dBdx.f` | `dAdx.f` |
| S4 | `dRde.i` | `dGde.i` | `dBde.i` | `dAde.i` |
| S5 | `dRdy.i` | `dGdy.i` | `dBdy.i` | `dAdy.i` |
| S6 | `dRde.f` | `dGde.f` | `dBde.f` | `dAde.f` |
| S7 | `dRdy.f` | `dGdy.f` | `dBdy.f` | `dAdy.f` |

The base `R,G,B,A` values are signed `s8.16` values at `(XH, floor(YH))`. Each derivative is signed `s15.16`:

- `d?/dx`: horizontal change along a span.
- `d?/de`: change while advancing the major edge.
- `d?/dy`: change per scanline, used with `d?/de` for subpixel correction.

The interpolated shade value becomes the `SHADE`/`SHADE_ALPHA` input to the color combiner and may supply shade alpha to the blender. If the suffix is absent, those inputs are undefined rather than implicitly white or zero.

### 5.3 Texture suffix

When texture is enabled, append eight words after any shade suffix.

| Suffix word | Bits 63:48 | Bits 47:32 | Bits 31:16 | Bits 15:0 |
|---:|---|---|---|---|
| T0 | `S.i` | `T.i` | `W.i` | unused |
| T1 | `dSdx.i` | `dTdx.i` | `dWdx.i` | unused |
| T2 | `S.f` | `T.f` | `W.f` | unused |
| T3 | `dSdx.f` | `dTdx.f` | `dWdx.f` | unused |
| T4 | `dSde.i` | `dTde.i` | `dWde.i` | unused |
| T5 | `dSdy.i` | `dTdy.i` | `dWdy.i` | unused |
| T6 | `dSde.f` | `dTde.f` | `dWde.f` | unused |
| T7 | `dSdy.f` | `dTdy.f` | `dWdy.f` | unused |

Concatenate integer and fractional halves to obtain signed `s15.16` values at `(XH, floor(YH))` and their `dx`, major-edge (`de`), and scanline (`dy`) derivatives. `W` is normalized inverse depth used for perspective correction. If `persp_tex_en = 1`, the texture unit divides interpolated S and T by W before tile-coordinate processing. If the suffix is absent, texture coordinates are undefined and TEX0/TEX1 combiner inputs must not be used.

### 5.4 Z suffix

When zbuffer is enabled, append two words after all other suffixes.

| Suffix word | Bits 63:48 | Bits 47:32 | Bits 31:16 | Bits 15:0 |
|---:|---|---|---|---|
| Z0 | `Z.i` | `Z.f` | `dZdx.i` | `dZdx.f` |
| Z1 | `dZde.i` | `dZde.f` | `dZdy.i` | `dZdy.f` |

Each concatenated value is signed `s15.16`. Z is evaluated at `(XH, floor(YH))`; derivatives have the same meanings as the shade/texture derivatives. The depth stage reduces and clamps interpolated Z to its internal 18-bit range. Per-pixel Z is selected only when `z_source_sel = 0`; otherwise the Primitive Depth register is used. If the suffix is absent while per-pixel depth is selected, depth is undefined.

### 5.5 Triangle state transition

The triangle command enqueues a raster primitive containing the packet coefficients. It reads persistent scissor, tile/TMEM state, Other Modes, combine state, color/key/convert registers, color-image state, and optional depth-image state as each pixel reaches the corresponding pipeline stage. It may:

- read TMEM and the color/depth images;
- reject pixels by scissor, coverage, chroma key, alpha compare, or depth compare;
- update color pixels and coverage metadata;
- update depth pixels when `z_update_en = 1`.

It does not modify any persistent attribute register or tile descriptor. Because global state is live, changing a consumed attribute before the primitive drains requires synchronization.

## 6. Texture Rectangle (`0x24`) and Texture Rectangle Flip (`0x25`)

These commands occupy two 64-bit words.

| Word | Bits | Field | Interpretation |
|---|---:|---|---|
| W0 | 61:56 | opcode | `0x24` normal, `0x25` flip |
| W0 | 55:44 | `lrx` | lower-right screen X, unsigned `u10.2` |
| W0 | 43:32 | `lry` | lower-right screen Y, unsigned `u10.2` |
| W0 | 26:24 | `tile` | tile descriptor index |
| W0 | 23:12 | `ulx` | upper-left screen X, unsigned `u10.2` |
| W0 | 11:0 | `uly` | upper-left screen Y, unsigned `u10.2` |
| W1 | 63:48 | `s` | initial S at `(ulx,uly)`, signed `s10.5` |
| W1 | 47:32 | `t` | initial T at `(ulx,uly)`, signed `s10.5` |
| W1 | 31:16 | `dsdx` | S derivative, signed `s5.10` |
| W1 | 15:0 | `dtdy` | T derivative, signed `s5.10` |

For opcode `0x24`, S varies with screen X and T varies with screen Y. Opcode `0x25` swaps the routing: T varies with X and S varies with Y. It does not numerically exchange the encoded fields; it exchanges their axis use in hardware.

In 1-Cycle and 2-Cycle modes, right and lower bounds are exclusive. In Copy and Fill modes, fractional screen bits are ignored and right/lower bounds are inclusive. In Copy mode `dsdx` must express the fact that a cycle transfers 64 bits of adjacent texels; for a 16-bit source, the usual step is 4 texels (`dsdx = 4 << 10`). In Fill mode, texture parameters are ignored and the command behaves as Fill Rectangle.

The command can be represented internally as a left-major rectangle-shaped triangle with zero X slopes. It enqueues a primitive and has the same memory side effects as a triangle, but it supplies texture coordinates and no shade or per-pixel Z coefficients. Primitive depth is the only defined depth source for a rectangle. `SHADE`, and per-pixel Z inputs are undefined; Fill Rectangle also lacks a defined texture input.

## 7. Synchronization commands

All synchronization packets contain only the six-bit opcode; every other bit is ignored.

### 7.1 Sync Load (`0x26`)

Sync Load inserts 25 GCLK wait cycles before the next command. This guarantees that the loading pipeline is available after prior rendering/loading work has advanced sufficiently. Use it before Load Tile, Load Block, or Load TLUT when prior work may conflict.

The 1996 command summary prints `0x31` for this command. Actual command streams and hardware-tested documentation use `0x26`; `0x31` is a no-op.

State transition:

```text
pipeline advances for 25 GCLK cycles
persistent attributes and memory are unchanged, except completion of prior in-flight work
```

### 7.2 Sync Pipe (`0x27`)

Sync Pipe inserts 50 GCLK wait cycles. It ensures prior primitive pixels have passed all live global-attribute uses, so subsequent Set Other Modes, Set Combine Mode, image/color/key/convert setters, and similar commands cannot affect the earlier primitive.

```text
pipeline advances for 50 GCLK cycles
persistent attributes are unchanged
prior render writes may retire to span buffers/RDRAM
```

### 7.3 Sync Tile (`0x28`)

Sync Tile inserts 33 GCLK wait cycles, ensuring prior primitives have finished reading tile descriptor fields. Use it before Set Tile when the descriptor may still be in use. Set Tile Size is explicitly safe without this sync, although a conservative implementation may still accept one.

```text
pipeline advances for 33 GCLK cycles
persistent attributes are unchanged
```

### 7.4 Sync Full (`0x29`)

Sync Full waits until all staged pipeline work and RDRAM reads/writes from preceding commands complete. It then halts the relevant RDP pipeline counter and raises the DP interrupt through the MIPS Interface.

```text
drain raster/load pipeline
flush span and memory operations
dp_interrupt_pending = true
full_sync_halted = true until the interface resumes/feeds work
```

It is normally the final command before `DP_END`. Queueing more commands while Full Sync is completing, or allowing it not to be the final consumed command at the boundary, can hang real hardware.

## 8. Set Key GB (`0x2A`)

| Word | Bits | Field | Interpretation |
|---|---:|---|---|
| W0 | 61:56 | opcode | `0x2A` |
| W0 | 55:44 | `width_g` | green key half-window, raw 12-bit / `u4.8` |
| W0 | 43:32 | `width_b` | blue key half-window, raw 12-bit / `u4.8` |
| W0 | 31:24 | `center_g` | green combiner CENTER component |
| W0 | 23:16 | `scale_g` | green combiner SCALE component |
| W0 | 15:8 | `center_b` | blue combiner CENTER component |
| W0 | 7:0 | `scale_b` | blue combiner SCALE component |

State transition:

```text
key.green = { width_g, center_g, scale_g }
key.blue  = { width_b, center_b, scale_b }
```

Center and scale are also direct RGB color-combiner inputs. When chroma keying is enabled, the combiner is normally configured to produce `(X - center) * scale`; the post-combiner key stage computes per channel approximately

```text
key_channel = clamp(-abs(combined_channel) + width_channel, 0, 1)
key_alpha = min(key_red, key_green, key_blue)
```

In 2-Cycle mode, keying must be configured in the second/final combiner cycle. A Pipe Sync is required before changing these values if an earlier primitive uses them.

## 9. Set Key R (`0x2B`)

| Word | Bits | Field | Interpretation |
|---|---:|---|---|
| W0 | 61:56 | opcode | `0x2B` |
| W0 | 27:16 | `width_r` | red key half-window, raw 12-bit / `u4.8` |
| W0 | 15:8 | `center_r` | red combiner CENTER component |
| W0 | 7:0 | `scale_r` | red combiner SCALE component |

Bits 55:28 are ignored.

```text
key.red = { width_r, center_r, scale_r }
```

Interpretation and synchronization are the same as Set Key GB.

## 10. Set Convert (`0x2C`)

| Word | Bits | Field |
|---|---:|---|
| W0 | 61:56 | opcode `0x2C` |
| W0 | 53:45 | `K0`, signed 9-bit (`s1.7`) |
| W0 | 44:36 | `K1`, signed 9-bit (`s1.7`) |
| W0 | 35:27 | `K2`, signed 9-bit (`s1.7`) |
| W0 | 26:18 | `K3`, signed 9-bit (`s1.7`) |
| W0 | 17:9 | `K4`, signed 9-bit (`s1.7`) |
| W0 | 8:0 | `K5`, signed 9-bit (`s1.7`) |

Sign-extend each raw 9-bit value before interpreting it. The transition is simply:

```text
convert = { K0, K1, K2, K3, K4, K5 }
```

K0 through K3 drive the texture-filter color-conversion path. Conceptually its intermediate result is

```text
R' = Y + K0 * V
G' = Y + K1 * U + K2 * V
B' = Y + K3 * U
A' = Y
```

K4 and K5 are available to the color combiner. The intended second step is `(C' - K4) * K5 + C'`, though they may be used as general-purpose combiner constants. Typical raw conversion values from the command summary are `K0=175, K1=-43, K2=-89, K3=222, K4=114, K5=42`.

The active texture-filter behavior is selected by `bi_lerp_0`, `bi_lerp_1`, and `convert_one` in Other Modes. A Pipe Sync is required before changing conversion state consumed by prior in-flight pixels.

## 11. Set Scissor (`0x2D`)

| Word | Bits | Field | Interpretation |
|---|---:|---|---|
| W0 | 61:56 | opcode | `0x2D` |
| W0 | 55:44 | `xh` | upper-left X, unsigned `u10.2` |
| W0 | 43:32 | `yh` | upper-left Y, unsigned `u10.2` |
| W0 | 25 | `field` | 1 enables interlaced line rejection |
| W0 | 24 | `keep_odd` | if field=1: 0 keep even, 1 keep odd |
| W0 | 23:12 | `xl` | lower-right X, unsigned `u10.2` |
| W0 | 11:0 | `yl` | lower-right Y, unsigned `u10.2` |

```text
scissor = { xh, yh, field, keep_odd, xl, yl }
```

Pixels outside this region never enter the pixel pipeline. The color-image width does not clip rasterization; software must set a compatible scissor.

In 1-Cycle and 2-Cycle modes, right and lower edges are exclusive. In Fill/Copy mode, the lower edge is exclusive but the right edge behaves inclusively. Interlaced mode rejects every line of the parity not selected by `keep_odd`.

Scissor is one of the four attributes that can be changed without a Pipe Sync. It must nevertheless be initialized before drawing; there is no disable value. Copy and Fill modes have strict horizontal alignment hazards: Copy normally requires `xh.x = 0` and a right bound aligned to a four-pixel group; Fill should use four-pixel-aligned horizontal bounds to avoid corruption.

## 12. Set Primitive Depth (`0x2E`)

| Word | Bits | Field |
|---|---:|---|
| W0 | 61:56 | opcode `0x2E` |
| W0 | 31:16 | `z` primitive depth |
| W0 | 15:0 | `dz` primitive depth delta |

```text
prim_depth.z  = z
prim_depth.dz = dz
```

When `z_source_sel = 1`, the depth comparator uses this state instead of interpolated per-pixel Z. It is the only defined depth source for rectangle primitives. The 16-bit `z` becomes the integer portion of the RDP's internal depth value; unavailable low fractional bits are zero, so this command cannot generate every possible 18-bit depth.

`dz` describes depth uncertainty/slope for depth comparison. Use a power of two for predictable hardware log2 encoding; `0xFFFF` is also known to work. Primitive Depth is specially latched and may be changed between primitives without Pipe Sync.

## 13. Set Other Modes (`0x2F`)

Set Other Modes replaces the complete 56-bit mode block below. It is not a masked update at the RDP command level.

### 13.1 Bit layout and direct state transition

| Bits | State field | Values/effect |
|---:|---|---|
| 61:56 | opcode | `0x2F` |
| 55 | `atomic_prim` | force active span segments to memory before a following primitive reads them |
| 54 | reserved | ignore/write zero |
| 53:52 | `cycle_type` | 0=1-Cycle, 1=2-Cycle, 2=Copy, 3=Fill |
| 51 | `persp_tex_en` | perspective divide S,T by W |
| 50 | `detail_tex_en` | detail-texture LOD mode |
| 49 | `sharpen_tex_en` | sharpen-texture LOD mode |
| 48 | `tex_lod_en` | enable computed texture LOD and tile selection |
| 47 | `tlut_en` | decode CI samples through high-TMEM palette |
| 46 | `tlut_type` | 0=RGBA5551, 1=IA88 palette entries |
| 45 | `sample_type` | 0=point/1x1, 1=2x2 sample |
| 44 | `mid_texel` | 0=3-point filtering, 1=average at the exact half-texel condition |
| 43 | `bi_lerp_0` | first texture-filter cycle: 0=convert, 1=filter |
| 42 | `bi_lerp_1` | second/final texture-filter cycle: 0=convert, 1=filter |
| 41 | `convert_one` | second filter input: 0=second sample stage, 1=first filter result |
| 40 | `key_en` | enable chroma-key stage after combiner |
| 39:38 | `rgb_dither_sel` | RGB dither selector |
| 37:36 | `alpha_dither_sel` | alpha dither selector |
| 35:32 | reserved | normally `0xF` in old SDK state; no defined command-state effect |
| 31:30 | `blender[0].p` | cycle 0 P input |
| 29:28 | `blender[1].p` | cycle 1 P input |
| 27:26 | `blender[0].a` | cycle 0 A factor |
| 25:24 | `blender[1].a` | cycle 1 A factor |
| 23:22 | `blender[0].m` | cycle 0 M input |
| 21:20 | `blender[1].m` | cycle 1 M input |
| 19:18 | `blender[0].b` | cycle 0 B factor |
| 17:16 | `blender[1].b` | cycle 1 B factor |
| 15 | reserved | ignore/write zero |
| 14 | `force_blend` | blend all surviving pixels |
| 13 | `alpha_cvg_select` | use coverage-derived value as blender alpha |
| 12 | `cvg_x_alpha` | multiply coverage by combiner alpha |
| 11:10 | `z_mode` | 0=opaque, 1=interpenetrating, 2=transparent, 3=decal |
| 9:8 | `cvg_dest` | 0=clamp, 1=wrap, 2=full/zap, 3=save old |
| 7 | `color_on_cvg` | write blender output only on coverage overflow; otherwise use M input |
| 6 | `image_read_en` | read memory color and coverage from color image |
| 5 | `z_update_en` | write depth when the color write survives |
| 4 | `z_compare_en` | enable depth-buffer read and comparison |
| 3 | `antialias_en` | coverage-based edge rejection/blend enable |
| 2 | `z_source_sel` | 0=per-pixel interpolated Z, 1=Primitive Depth |
| 1 | `dither_alpha_en` | alpha threshold: 0=Blend Color alpha, 1=random |
| 0 | `alpha_compare_en` | reject pixels below alpha threshold |

All listed fields are assigned from W0 in one transition. A Pipe Sync must precede this command if earlier primitives are in flight.

### 13.2 Dither selectors

| `rgb_dither_sel` | RGB behavior |
|---:|---|
| 0 | 4x4 magic-square pattern |
| 1 | 4x4 Bayer pattern |
| 2 | independent per-channel noise |
| 3 | disabled |

| `alpha_dither_sel` | Alpha behavior |
|---:|---|
| 0 | selected RGB pattern; substitutes magic/Bayer when RGB uses noise/off |
| 1 | inverse of the corresponding pattern |
| 2 | noise |
| 3 | disabled |

The RGB patterns are indexed by low screen-coordinate bits and reduce 8-bit channels for a 16-bit color image.

### 13.3 Coverage destination

| `cvg_dest` | New coverage state |
|---:|---|
| 0 | `min(old + new, full)` |
| 1 | `(old + new) mod 8` |
| 2 | full coverage |
| 3 | preserve old coverage; requires `image_read_en`, otherwise behaves as full |

### 13.4 Blender muxes

For each configured cycle the blender receives RGB colors P and M and scalar factors A and B. When blending is enabled it forms either

```text
(P*A + M*B) / (A+B)
```

or, in the non-dividing force-blend path, the corresponding unnormalized sum. When blending is disabled, P passes through. In 1-Cycle mode the `blender[0]` configuration is used; in 2-Cycle mode both are used.

| P or M selector | Source |
|---:|---|
| 0 | cycle 0: final combiner RGB; cycle 1: first blender-cycle RGB |
| 1 | memory RGB from color image |
| 2 | Blend Color RGB |
| 3 | Fog Color RGB |

| A selector | Source |
|---:|---|
| 0 | final combiner alpha |
| 1 | Fog Color alpha |
| 2 | interpolated shade alpha |
| 3 | 0 |

| B selector | Source |
|---:|---|
| 0 | `1 - A` using the other factor input |
| 1 | memory coverage |
| 2 | 1 |
| 3 | 0 |

Selecting memory color/coverage requires `image_read_en`; otherwise memory color is undefined and memory coverage behaves as full. Selecting shade alpha requires shade coefficients.

### 13.5 Mode-specific pipeline state

- **1-Cycle:** one texture/filter result, the second/final combiner mux set, and blender cycle 0 are active. Depth, alpha compare, coverage, dithering, and image writes follow configured state.
- **2-Cycle:** two texture/filter, combiner, and blender cycles are active. First-cycle results feed second-cycle selectors; several documented one-pixel pipeline shifts apply.
- **Copy:** textures are copied in 64-bit groups. Combiner, blender, depth, filtering, antialiasing, and clamping are bypassed; optional TLUT and alpha compare remain meaningful.
- **Fill:** the 32-bit Fill Color is replicated directly into 64-bit RDRAM writes. Texture, combiner, blender, depth, coverage, and image-read state are bypassed.

Relevant exposed history-latch hazards are part of transient state:

- In 1-Cycle mode, TEX1 supplies the next pixel's TEX0 value and is garbage at a scanline end.
- In the first combiner cycle, COMBINED refers to the previous pixel result and is garbage at a scanline start.
- In 2-Cycle mode's second combiner cycle, TEX0 refers to the current pixel's second texture, while TEX1 refers to the next pixel's first texture.
- In 2-Cycle mode, first-cycle memory color/coverage can refer to the previous pixel; second-cycle shade alpha can refer to the next pixel; alpha compare has a related next-pixel combiner hazard.

## 14. Load TLUT (`0x30`)

| Word | Bits | Field | Interpretation |
|---|---:|---|---|
| W0 | 61:56 | opcode | `0x30` |
| W0 | 55:44 | `sl` | low palette index as `u10.2`; fraction must be 0 |
| W0 | 43:32 | `tl` | normally 0 |
| W0 | 26:24 | `tile` | load tile descriptor |
| W0 | 23:12 | `sh` | high palette index as `u10.2`; fraction must be 0 |
| W0 | 11:0 | `th` | normally 0 |

The command reads 16-bit entries from the current Texture Image and writes a replicated TLUT into TMEM starting at `tile[tile].tmem_word * 8`. Endpoints are inclusive; the usual values for `N` entries are `sl=0`, `sh=(N-1)<<2`, `tl=th=0`.

For each source 16-bit entry, the loader writes four adjacent copies, consuming eight TMEM bytes. The selected base must be in high TMEM (`tmem_word >= 0x100`) and aligned to 16 TMEM words (128 bytes) for subsequent palette addressing. A 256-entry table occupies `0x800` bytes; a 16-entry table occupies `0x80` bytes.

State transition:

```text
for i = sl/4 .. sh/4 inclusive:
    entry = read16(texture_image.address + 2*i, using texture-image row addressing)
    tmem[tile.tmem_base + 8*(i-sl/4) .. +7] = entry repeated four times
tile[tile].{sl,tl,sh,th} = command.{sl,tl,sh,th}
```

The Texture Image must use 16-bit size. For reliable loading, the load tile itself is normally configured as a non-YUV/non-RGBA 4-bit tile even though the source entries are 16-bit. The format used when sampling the palette is selected by `tlut_type` (RGBA5551 or IA88), not by Texture Image format. Synchronize before entering the load pipeline; if the same descriptor will render later, restore its extents with Set Tile Size.

## 15. Set Tile Size (`0x32`)

| Word | Bits | Field | Interpretation |
|---|---:|---|---|
| W0 | 61:56 | opcode | `0x32` |
| W0 | 55:44 | `sl` | upper-left S, unsigned `u10.2` |
| W0 | 43:32 | `tl` | upper-left T, unsigned `u10.2` |
| W0 | 26:24 | `tile` | descriptor index |
| W0 | 23:12 | `sh` | lower-right S, unsigned `u10.2` |
| W0 | 11:0 | `th` | lower-right T, unsigned `u10.2` |

```text
tile[tile].sl = sl
tile[tile].tl = tl
tile[tile].sh = sh
tile[tile].th = th
```

The upper-left pair establishes the tile coordinate origin and lower clamp limits. The lower-right pair establishes upper clamp limits. Coordinates are inclusive texel bounds in quarter-texel units; the common exact-size encoding for a `width x height` tile at origin is `sh=(width-1)<<2`, `th=(height-1)<<2`.

This command changes no format, TMEM address, line stride, palette, mask, mirror, clamp, or shift field. Tile Size is specially latched and may be changed without Tile/Pipe Sync.

For YUV, S bounds must describe pairs: `sl` even in integer texels and `sh` odd. Filtering may require an additional texel at the edge; hardware clamps the last usable even texel so a U/V pair remains available.

## 16. Load Block (`0x33`)

| Word | Bits | Field | Interpretation |
|---|---:|---|---|
| W0 | 61:56 | opcode | `0x33` |
| W0 | 55:44 | `sl` | first source S, unsigned `u12.0` |
| W0 | 43:32 | `tl` | first source T, unsigned; effective T uses the low 10 bits |
| W0 | 26:24 | `tile` | load tile descriptor |
| W0 | 23:12 | `sh` | last source S, unsigned `u12.0` |
| W0 | 11:0 | `dxt` | line/parity increment, unsigned `u1.11` |

Load Block treats source data as one contiguous inclusive span from `(sl,tl)` through `sh`, starting at the current Texture Image address and writing at the selected tile's TMEM base. The texel count is `sh - sl + 1`. A load of more than 2048 source texels fails and writes nothing.

Every 64-bit TMEM word increments an internal 1.11 accumulator by `dxt`. Its integer/parity state tells the loader whether the word belongs to an odd texture row; odd rows have their two 32-bit halves swapped for TMEM bank access. For an ordinary source image:

```text
words_per_line = ceil(width_in_texels * bits_per_texel / 64)
dxt = ceil(2^11 / words_per_line)
```

Every source row must occupy an integral number of 64-bit words in RDRAM. If not naturally aligned, software must pad/rearrange it before Load Block. Setting `dxt=0` is appropriate for data already stored in TMEM-interleaved order.

State transition:

```text
source = texture_image.address + packed_texel_offset(sl, tl,
          texture_image.width, texture_image.size)
copy sh-sl+1 texels from RDRAM to tile[tile].tmem_word,
     applying 64-bit grouping, dxt-derived odd-row swap,
     and RGBA32/YUV split-TMEM rules
tile[tile].sl = sl
tile[tile].tl = tl
tile[tile].sh = sh
tile[tile].th = dxt
```

The extents written as a side effect are not useful render bounds, so Set Tile Size normally follows. Loads wrapping past TMEM byte `0xFFF` continue at byte `0x000`. Texture Image size and load-tile size should match. Four-bit textures are often loaded as 8-bit units (after byte alignment), then rendered through a separately configured 4-bit descriptor. Source addresses with byte offsets 1 through 7 modulo 64 can hang the RDP; use at least 8-byte alignment.

## 17. Load Tile (`0x34`)

| Word | Bits | Field | Interpretation |
|---|---:|---|---|
| W0 | 61:56 | opcode | `0x34` |
| W0 | 55:44 | `sl` | upper-left source S, unsigned `u10.2` |
| W0 | 43:32 | `tl` | upper-left source T, unsigned `u10.2` |
| W0 | 26:24 | `tile` | load tile descriptor |
| W0 | 23:12 | `sh` | lower-right source S, unsigned `u10.2` |
| W0 | 11:0 | `th` | lower-right source T, unsigned `u10.2` |

Load Tile copies an inclusive rectangular region from the current Texture Image to TMEM. Fractional endpoint bits are normally equal and select a subtexel offset; ordinary loads use zero fractions. The source row stride comes from `texture_image.width` and `texture_image.size`. The destination row stride is `tile[tile].line_words * 8` bytes, and its origin is `tile[tile].tmem_word * 8`.

```text
for source t from floor(tl) through floor(th):
    read packed texels floor(sl) through floor(sh) from Texture Image
    write them to the corresponding TMEM row,
        transparently padding to destination line stride,
        applying odd-row bank swap and special RGBA32/YUV layout
tile[tile].{sl,tl,sh,th} = command.{sl,tl,sh,th}
```

Load Tile is slower than Load Block but accepts arbitrary packed source row widths and performs row interleaving automatically. A load that passes the end of TMEM wraps. Texture Image and load-tile sizes should match. For YUV, source S low must be even and high odd so every two Y values have a U/V pair. RGBA32 and YUV require a lower-half TMEM base and cannot share the upper half with a TLUT in the normal layout.

## 18. Set Tile (`0x35`)

| Word | Bits | Field | Interpretation |
|---|---:|---|---|
| W0 | 61:56 | opcode | `0x35` |
| W0 | 55:53 | `format` | texel format |
| W0 | 52:51 | `size` | texel size |
| W0 | 49:41 | `line` | row stride in 64-bit TMEM words |
| W0 | 40:32 | `tmem` | TMEM base in 64-bit words |
| W0 | 26:24 | `tile` | descriptor index |
| W0 | 23:20 | `palette` | CI4 palette number |
| W0 | 19 | `clamp_t` | T clamp enable |
| W0 | 18 | `mirror_t` | T mirror enable |
| W0 | 17:14 | `mask_t` | T wrap mask width |
| W0 | 13:10 | `shift_t` | T coordinate shift |
| W0 | 9 | `clamp_s` | S clamp enable |
| W0 | 8 | `mirror_s` | S mirror enable |
| W0 | 7:4 | `mask_s` | S wrap mask width |
| W0 | 3:0 | `shift_s` | S coordinate shift |

Format encoding:

| Value | Format |
|---:|---|
| 0 | RGBA |
| 1 | YUV |
| 2 | CI (color index) |
| 3 | IA (intensity-alpha) |
| 4..7 | I (intensity behavior) |

Size encoding:

| Value | Bits per texel |
|---:|---:|
| 0 | 4 |
| 1 | 8 |
| 2 | 16 |
| 3 | 32 |

Officially supported combinations are RGBA16/32, YUV16, CI4/8, IA4/8/16, and I4/8.

State transition:

```text
tile[tile].format = format
tile[tile].size = size
tile[tile].line_words = line
tile[tile].tmem_word = tmem
tile[tile].palette = palette
tile[tile].clamp_t/mirror_t/mask_t/shift_t = encoded T controls
tile[tile].clamp_s/mirror_s/mask_s/shift_s = encoded S controls
// sl,tl,sh,th are unchanged
```

For a normal rendered tile, `line = ceil(width * bits_per_texel / 64)`. Load Tile transparently pads each destination row to this stride. Load Block requires equivalent padding already present in RDRAM.

For CI4, `palette` supplies the high four bits of the eight-bit TLUT index; the texel supplies the low four. It is ignored for CI8 and non-CI formats. The corresponding 16-entry palette therefore begins at high-TMEM byte address `0x800 + palette*0x80` in the standard layout.

The address controls are interpreted as described in section 2.2. Mask `n` creates a `2^n` wrap period; a mask of 1 is the minimum nonzero period and passes one address bit (two texels). Mirroring for RGBA32 and mask/mirror for YUV are unsupported/undefined.

Set Tile does not alter TMEM itself. It changes a descriptor read by load and render stages. Use Tile Sync before modifying a descriptor still in use by an earlier primitive.

## 19. Fill Rectangle (`0x36`)

| Word | Bits | Field | Interpretation |
|---|---:|---|---|
| W0 | 61:56 | opcode | `0x36` |
| W0 | 55:44 | `lrx` | lower-right X, unsigned `u10.2` |
| W0 | 43:32 | `lry` | lower-right Y, unsigned `u10.2` |
| W0 | 23:12 | `ulx` | upper-left X, unsigned `u10.2` |
| W0 | 11:0 | `uly` | upper-left Y, unsigned `u10.2` |

The command enqueues a rectangle-shaped raster primitive. Its exact state dependencies are selected by `cycle_type`:

- **Fill:** ignore fractional coordinate bits for pixel selection, include right/lower bounds, and write the Fill Color directly in 64-bit groups.
- **1-Cycle/2-Cycle:** right/lower bounds are exclusive; use the normal coverage, combiner, depth, blender, dither, and write pipeline. The rectangle supplies no shade, texture, or per-pixel Z coefficients, so any selected such source is undefined. Primitive Depth remains valid.
- **Copy:** behaves like a Texture Rectangle with undefined/zero-like texture attributes and should not be used as a general copy primitive.

No persistent register changes. The command may update color/depth RDRAM and coverage metadata as selected by the active pipeline. Fill mode writes 64 bits per operation: eight I8 pixels, four RGBA16 pixels, or two RGBA32 pixels.

## 20. Set Fill Color (`0x37`)

| Word | Bits | Field |
|---|---:|---|
| W0 | 61:56 | opcode `0x37` |
| W0 | 31:0 | `fill_color` raw packed 32-bit value |

```text
fill_color = W0 & 0xFFFFFFFF
```

Fill mode repeats this 32-bit pattern into each 64-bit memory write. Interpretation therefore depends on Color Image size:

| Color-image size | Pattern interpretation |
|---|---|
| 32-bit | one RGBA8888 pixel, repeated twice per 64-bit write |
| 16-bit | two RGBA5551 pixels; even pixel uses high half, odd uses low half |
| 8-bit | four intensity bytes, repeated |
| 4-bit | unsupported; Fill drawing can crash the RDP |

This register is bypassed outside Fill mode and is not a combiner/blender source. Use Pipe Sync before changing it if prior Fill work remains in flight.

## 21. Set Fog Color (`0x38`)

| Word | Bits | Field |
|---|---:|---|
| W0 | 61:56 | opcode `0x38` |
| W0 | 31:24 | red |
| W0 | 23:16 | green |
| W0 | 15:8 | blue |
| W0 | 7:0 | alpha |

```text
fog_color = rgba8(red, green, blue, alpha)
```

Fog RGB is selectable as blender P or M; Fog alpha is selectable as blender A. The hardware gives the register no intrinsic fog behavior. Pipe Sync is required before changing it if an earlier primitive uses it.

## 22. Set Blend Color (`0x39`)

| Word | Bits | Field |
|---|---:|---|
| W0 | 61:56 | opcode `0x39` |
| W0 | 31:24 | red |
| W0 | 23:16 | green |
| W0 | 15:8 | blue |
| W0 | 7:0 | alpha |

```text
blend_color = rgba8(red, green, blue, alpha)
```

Blend RGB is selectable as blender P or M. Blend alpha is the non-random alpha-compare threshold when `alpha_compare_en=1` and `dither_alpha_en=0`. The name does not otherwise force a particular blend equation. Pipe Sync is required if a prior primitive consumes this register.

## 23. Set Primitive Color (`0x3A`)

| Word | Bits | Field | Interpretation |
|---|---:|---|---|
| W0 | 61:56 | opcode | `0x3A` |
| W0 | 47:40 | `min_level` | minimum texture-LOD clamp/control, raw 8-bit fraction |
| W0 | 39:32 | `prim_lod_frac` | direct combiner primitive-LOD-fraction input |
| W0 | 31:24 | red | primitive color red |
| W0 | 23:16 | green | primitive color green |
| W0 | 15:8 | blue | primitive color blue |
| W0 | 7:0 | alpha | primitive color alpha |

```text
prim_color = { min_level, prim_lod_frac, rgba8(red,green,blue,alpha) }
```

Primitive RGB/alpha and `prim_lod_frac` are color-combiner inputs. `prim_lod_frac` is a fixed programmable fraction; despite its name, the texture LOD unit does not compute it. `min_level` participates in texture LOD lower-bound/detail/sharpen selection. A value below the minimum selects the base tile before detail/sharpen adjustments.

Primitive Color is specially latched and can change between primitives without Pipe Sync.

## 24. Set Environment Color (`0x3B`)

| Word | Bits | Field |
|---|---:|---|
| W0 | 61:56 | opcode `0x3B` |
| W0 | 31:24 | red |
| W0 | 23:16 | green |
| W0 | 15:8 | blue |
| W0 | 7:0 | alpha |

```text
env_color = rgba8(red, green, blue, alpha)
```

Environment RGB and alpha are color-combiner inputs. The name does not impose a particular meaning. Pipe Sync is required before changing it if earlier pixels use it.

## 25. Set Combine Mode (`0x3C`)

The color combiner independently evaluates RGB and alpha in two configured cycles using

```text
out = (A - B) * C + D
```

Mux set 0 is the first cycle and mux set 1 the second/final cycle. In 2-Cycle mode both are evaluated. In 1-Cycle mode only set 1 is evaluated; set 0 is ignored. SDK macros often duplicate a one-cycle expression into both sets, but hardware does not require them to be equal.

### 25.1 Packet layout and transition

| Bits | State field |
|---:|---|
| 61:56 | opcode `0x3C` |
| 55:52 | `rgb_a[0]` |
| 51:47 | `rgb_c[0]` |
| 46:44 | `alpha_a[0]` |
| 43:41 | `alpha_c[0]` |
| 40:37 | `rgb_a[1]` |
| 36:32 | `rgb_c[1]` |
| 31:28 | `rgb_b[0]` |
| 27:24 | `rgb_b[1]` |
| 23:21 | `alpha_a[1]` |
| 20:18 | `alpha_c[1]` |
| 17:15 | `rgb_d[0]` |
| 14:12 | `alpha_b[0]` |
| 11:9 | `alpha_d[0]` |
| 8:6 | `rgb_d[1]` |
| 5:3 | `alpha_b[1]` |
| 2:0 | `alpha_d[1]` |

All 16 mux fields replace the corresponding `combine` state. Changing them requires Pipe Sync when an earlier primitive is in flight.

### 25.2 RGB A selector

| Value | Source |
|---:|---|
| 0 | COMBINED RGB from first cycle/history latch |
| 1 | TEX0 RGB |
| 2 | TEX1 RGB |
| 3 | Primitive RGB |
| 4 | Shade RGB |
| 5 | Environment RGB |
| 6 | constant 1 (`256` in combiner precision) |
| 7 | per-pixel noise |
| 8..15 | 0 |

### 25.3 RGB B selector

| Value | Source |
|---:|---|
| 0 | COMBINED RGB |
| 1 | TEX0 RGB |
| 2 | TEX1 RGB |
| 3 | Primitive RGB |
| 4 | Shade RGB |
| 5 | Environment RGB |
| 6 | key CENTER RGB |
| 7 | conversion K4 |
| 8..15 | 0 |

### 25.4 RGB C selector

| Value | Source |
|---:|---|
| 0 | COMBINED RGB |
| 1 | TEX0 RGB |
| 2 | TEX1 RGB |
| 3 | Primitive RGB |
| 4 | Shade RGB |
| 5 | Environment RGB |
| 6 | key SCALE RGB |
| 7 | COMBINED alpha |
| 8 | TEX0 alpha |
| 9 | TEX1 alpha |
| 10 | Primitive alpha |
| 11 | Shade alpha |
| 12 | Environment alpha |
| 13 | computed LOD fraction |
| 14 | Primitive LOD Fraction register |
| 15 | conversion K5 |
| 16..31 | 0 |

### 25.5 RGB D selector

| Value | Source |
|---:|---|
| 0 | COMBINED RGB |
| 1 | TEX0 RGB |
| 2 | TEX1 RGB |
| 3 | Primitive RGB |
| 4 | Shade RGB |
| 5 | Environment RGB |
| 6 | 1 |
| 7 | 0 |

### 25.6 Alpha A, B, and D selectors

All three share this three-bit map:

| Value | Source |
|---:|---|
| 0 | COMBINED alpha |
| 1 | TEX0 alpha |
| 2 | TEX1 alpha |
| 3 | Primitive alpha |
| 4 | Shade alpha |
| 5 | Environment alpha |
| 6 | 1 |
| 7 | 0 |

### 25.7 Alpha C selector

| Value | Source |
|---:|---|
| 0 | computed LOD fraction |
| 1 | TEX0 alpha |
| 2 | TEX1 alpha |
| 3 | Primitive alpha |
| 4 | Shade alpha |
| 5 | Environment alpha |
| 6 | Primitive LOD Fraction register |
| 7 | 0 |

### 25.8 Source validity and state effects

TEX0 is sampled from the primitive's selected tile. TEX1 is the next tile modulo 8 after LOD selection. Shade requires a triangle shade suffix. Texture inputs require a textured triangle or texture rectangle. Computed LOD fraction requires texture LOD state. Invalid selections expose pipeline history rather than a guaranteed zero:

- COMBINED in the first active cycle reads the previous pixel result and is garbage at the start of a scanline.
- TEX1 in 1-Cycle mode is the next pixel's TEX0 and is garbage at a scanline end.
- In 2-Cycle mode's second cycle, TEX0 names the current second texture and TEX1 names the next pixel's first texture.
- Shade on a non-shaded primitive and texture on Fill Rectangle are undefined.

The combiner uses limited signed intermediate precision. Values 256..383 saturate to 255, values 384..511 can overflow to 0, and values at or above 512 lose the 9-bit color. The constant 1 is internally 256, not 255. C-input sign extension differs from A/B/D, so using COMBINED as C is especially overflow-prone.

## 26. Set Texture Image (`0x3D`)

| Word | Bits | Field | Interpretation |
|---|---:|---|---|
| W0 | 61:56 | opcode | `0x3D` |
| W0 | 55:53 | `format` | image format code |
| W0 | 52:51 | `size` | packed source texel size |
| W0 | 41:32 | `width_minus_1` | effective 10-bit row width minus one |
| W0 | 23:0 | `address` | RDRAM physical byte address |

Bits 43:42 and 31:24 do not contribute to the effective texture-image state.

Format values are RGBA=0, YUV=1, CI=2, IA=3, and I=4..7. Size values are 4/8/16/32 bits for raw values 0/1/2/3.

```text
texture_image.format  = format
texture_image.size    = size
texture_image.width   = width_minus_1 + 1
texture_image.address = address & 0xFFFFFF
```

The loading pipeline uses size to compute packed source byte addresses and width to move between source rows. Texture Image format is latched but does not determine final texel decoding; the selected tile descriptor's format does. This permits loading bytes under one format/size description and sampling them under another, provided the TMEM layout is valid.

Rendering does not read Texture Image, so it need not be Pipe-Synced merely because triangles are in flight. A following load must still be separated from conflicting rendering/loading work by Sync Load. Use an address aligned to at least eight bytes; addresses whose low six bits are 1..7 may hang when loaded.

Although libultra's encoding macros accept a 12-bit width and advertise up to 4096 pixels, the command summary and hardware-style decoders use the low 10 effective width bits. For state-machine decoding, retain the effective value `((W0 >> 32) & 0x3FF) + 1`.

## 27. Set Depth Image (`0x3E`)

This command is also called Set Z Image or Set Mask Image in older material.

| Word | Bits | Field |
|---|---:|---|
| W0 | 61:56 | opcode `0x3E` |
| W0 | 23:0 | `address` RDRAM physical byte address |

```text
depth_image.address = address & 0xFFFFFF
```

The depth image has no independent width register; it uses `color_image.width`. Its logical depth values have 18-bit internal precision and are stored through the RDP's compressed depth/`dz` representation and RDRAM hidden metadata.

The depth stage reads this address when `z_compare_en=1` and writes it when `z_update_en=1`. It must be initialized before such drawing. Use 64-byte alignment. Changing it while prior depth operations are in flight requires a Pipe/Full synchronization appropriate to the memory reuse.

## 28. Set Color Image (`0x3F`)

| Word | Bits | Field | Interpretation |
|---|---:|---|---|
| W0 | 61:56 | opcode | `0x3F` |
| W0 | 55:53 | `format` | destination image format |
| W0 | 52:51 | `size` | destination pixel size |
| W0 | 41:32 | `width_minus_1` | effective 10-bit row width minus one |
| W0 | 23:0 | `address` | RDRAM physical byte address |

Bits 43:42 and 31:24 do not contribute to effective state.

```text
color_image.format  = format
color_image.size    = size
color_image.width   = width_minus_1 + 1
color_image.address = address & 0xFFFFFF
```

The supported render targets are RGBA16, RGBA32, and 8-bit intensity behavior. All 8-bit format codes act equivalently as a destination. Unsupported 4-bit drawing can write zeros or crash, especially in Fill mode. Non-RGBA encodings at 16/32 bits do not behave as ordinary supported render targets.

Color-image width defines RDRAM row stride and also supplies depth-image width. It does not clip drawing; Scissor controls clipping. The address and width are consumed by image read, color writes, coverage metadata access, and depth addressing. Use 64-byte alignment.

Copy mode restrictions are state-dependent:

- 4/8-bit texture data can copy only to an 8-bit color image.
- 16-bit texture data can copy only to a 16-bit color image; a TLUT-decoded CI sample is treated as 16-bit.
- Copy mode is unavailable for a 32-bit color image.

Set Color Image must be treated as a live pipeline/memory attribute. Pipe/Full Sync before switching render targets or reusing the old image as a Texture Image, so earlier span writes and memory reads are complete.

## 29. Command-level transition summary

For a sequential state-machine implementation, the complete transition function is:

```text
step(state, rdram, command):
  decode opcode = bits 61:56
  decode packet length from opcode

  if attribute setter:
      replace only the named persistent state fields

  if Set Tile Size:
      replace only selected tile sl/tl/sh/th

  if Set Tile:
      replace selected tile fields except sl/tl/sh/th

  if texture load:
      read current Texture Image and selected tile load state
      write TMEM using format-specific packing/interleave
      replace selected tile sl/tl/sh/th with command fields

  if triangle or rectangle:
      create primitive-local coefficient state
      enqueue raster work that reads live global state at pipeline stages
      update color/depth RDRAM and hidden metadata for surviving pixels

  if no-op:
      advance one cycle only

  if Load/Tile/Pipe Sync:
      advance 25/33/50 cycles, allowing prior work to retire

  if Full Sync:
      drain all work and memory operations, then raise DP interrupt
```

The following commands do not overwrite any persistent RDP attribute state: all No Operations, all four Sync commands, all triangle variants, Texture Rectangle/Flip, and Fill Rectangle. Their state changes are limited to pipeline/transient state and external memory effects. Every setter and load persists until a later command explicitly overwrites the same field; there is no implicit reset at primitive or display-list boundaries.
