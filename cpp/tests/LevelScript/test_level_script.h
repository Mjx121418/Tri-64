#ifndef TEST_LEVEL_SCRIPT_H
#define TEST_LEVEL_SCRIPT_H

#include "Level/area.h"
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

// Runs the display list interpreter over every GraphNodeDisplayList in the
// areas produced by the level script and prints triangle statistics.
void testDisplayList();

// Shared setup for the level script tests: loads the ROM, locates the scripts
// segment and the level jump table, loads the common segments and runs the
// level script for BOB. Returns the populated segment table and level.
struct LevelScriptSetup {
    ROM rom; // 必须声明在 seg_table 之前：seg_table 的 span 指向 rom.data
    SegmentTable seg_table;
    Level level;
    bool ok {false};
};

LevelScriptSetup setupLevelScript(const std::filesystem::path &rom_path);

#endif /* TEST_LEVEL_SCRIPT_H */
