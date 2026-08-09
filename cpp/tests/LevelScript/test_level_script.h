#ifndef TEST_LEVEL_SCRIPT_H
#define TEST_LEVEL_SCRIPT_H

#include "Level/area.h"
#include "Log.h"
#include "Memory/segment.h"
#include "ROM.h"
#include <filesystem>

// Runs the level script of course "Bob-omb Battlefield" (LEVEL_BOB = 9) from
// the vanilla baserom and prints the resulting area graph node trees.
//
// The entry point is the game's own level jump table (script_exec_level_table,
// segment 0x15): it reads the current level number into the register, then
// JUMP_IF dispatches to the matching course script, which EXECUTEs the course
// entry. Everything downstream (segment ROM ranges, geo layouts, display
// lists) is read from the ROM itself, exactly like the game does.
void testRunLevelScript();

// Rom Manager one-bank-0xE regression: LevelExtract must replace seg 0x0E with
// the selected area's Fast3D ROM range when seg 0x19 contains the RM marker.
void testRomManagerAreaBank();

// Runs the display list interpreter over every GraphNodeDisplayList in the
// areas produced by the level script and prints triangle statistics.
void testDisplayList();

// Synthetic check of the G_MTX model-view transform: a translation matrix
// applied to a known vertex must yield position + translation.
void testMatrixSupport();

// Dumps every area's display lists to Wavefront OBJ files under export/.
void testExportObj();

// Dumps only the billboard-style triangles (GraphNodeBillboard subtrees and
// non-opaque-layer display lists, e.g. BOB's trees) plus their textures to
// export/<stem>_area<N>_billboards.obj/.mtl.
void testExportBillboards();

// Tests level name extraction from segment 2.
void testLevelName();

// Extracts the object-driven Bowser levels (17/19/21) and reports the decoded
// object models (per model id) plus object→model reuse.
void testObjectModels();

// Checks the level-script-recorded data beyond geometry/objects: warp nodes,
// painting warps, whirlpools, dialog, music params, Mario spawn behavior,
// transition (commands 0x25-0x3B that used to be no-ops).
void testLevelScriptData();

// Synthetic DL test for the DL/RSP data we extract but don't render yet:
// gsSPLight (G_MOVEMEM), G_MW_NUMLIGHT/FOG, G_SETOTHERMODE LUT type,
// G_SETTILE palette/line, G_LOADTLUT, G_TEXTURE tile/lod.
void testDlRspData();

// Synthetic texture decode test: CI8/CI4 (palette/TLUT + LUT type), IA8/IA4,
// I8/I4, RGBA32 — the formats beyond RGBA16/IA16.
void testTextureFormats();

// Shared setup for the level script tests: loads the ROM, locates the scripts
// segment and the level jump table, loads the common segments and runs the
// level script for BOB. Returns the populated segment table and level.
struct LevelScriptSetup {
    ROM rom; // 必须声明在 seg_table 之前：seg_table 的 span 指向 rom.data
    SegmentTable seg_table;
    Level level;
    WarningLog warnings;
    bool ok {false};
};

LevelScriptSetup setupLevelScript(const std::filesystem::path &rom_path);

// Collects every ROM the test should run: the vanilla baserom plus any ROM
// hack named "Super Mario Treasure World*.z64" (relative to the test's
// working directory, so no absolute paths are baked in).
std::vector<std::filesystem::path> findRoms();

#endif /* TEST_LEVEL_SCRIPT_H */
