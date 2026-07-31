#include "test_level_script.h"

#include "Level/area.h"
#include "Memory/segment.h"
#include "ROM.h"
#include "Scripts/level_script.h"
#include "tree_printer.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace {

// Level numbers, see include/level_table.h in the decomp.
constexpr int32_t LEVEL_BOB = 9;

// Distinctive command bytes that mark the start of the level-scripts segment
// (0x15) in the vanilla baserom:
//   - level_main_scripts_entry[0] = LOAD_MIO0(0x04, ...) -> 18 0C 00 04
//     (group0_mio0 is only ever loaded there, so the pattern is unique)
//   - script_exec_level_table[0] = GET_OR_SET(OP_GET, VAR_CURR_LEVEL_NUM)
//     -> 3C 04 01 03 (opcode, size, op=OP_GET, var=VAR_CURR_LEVEL_NUM; also
//     unique in the whole game). Only searched inside the scripts segment.
constexpr std::array<uint8_t, 4> kScriptsStartPattern { 0x18, 0x0C, 0x00, 0x04 };
constexpr std::array<uint8_t, 4> kLevelTablePattern { 0x3C, 0x04, 0x01, 0x03 };

size_t findPattern(const std::vector<uint8_t> &rom, const std::array<uint8_t, 4> &pattern, size_t from = 0) {
    auto it = std::search(rom.begin() + from, rom.end(), pattern.begin(), pattern.end());
    if (it == rom.end()) {
        return rom.size();
    }
    return static_cast<size_t>(it - rom.begin());
}

uint32_t readBE32(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// The game loads these five common segments at the top of
// level_main_scripts_entry, before any course script runs. Our test starts at
// the level jump table instead (skipping the menu), so we must perform the
// same loads ourselves. The ROM ranges are read from the commands themselves.
void loadCommonSegments(SegmentTable &seg_table, const std::vector<uint8_t> &rom, size_t scripts_start) {
    for (size_t off = 0; off < 5 * 0x0C; off += 0x0C) {
        const uint8_t *cmd = rom.data() + scripts_start + off;
        const uint16_t seg = (cmd[2] << 8) | cmd[3];
        const uint32_t rom_start = readBE32(cmd + 4);
        const uint32_t rom_end = readBE32(cmd + 8);

        if (cmd[0] == 0x18) { // LOAD_MIO0
            seg_table.loadMIO0Segment(seg, rom_start, rom_end);
        } else if (cmd[0] == 0x17) { // LOAD_RAW
            seg_table.loadSegment(seg, rom_start, rom_end);
        }
    }
}

std::filesystem::path findBaserom() {
    for (const char *candidate : {
             "baserom.us.z64",
             "tests/baserom.us.z64",
         }) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

} // namespace

void testRunLevelScript() {
    ROM rom;
    rom.load(findBaserom());
    if (!rom.is_loaded) {
        printf("test_level_script: could not load baserom.us.z64\n");
        return;
    }

    // Locate the level-scripts segment (0x15) and the level jump table inside it.
    // The jump table pattern can also occur in unrelated data (e.g. MIPS code
    // bytes), so it is only searched within the scripts segment.
    const size_t scripts_start = findPattern(rom.data, kScriptsStartPattern);
    const size_t table_pos = findPattern(rom.data, kLevelTablePattern, scripts_start);
    if (scripts_start == rom.data.size() || table_pos == rom.data.size()) {
        printf("test_level_script: could not locate the level scripts segment\n");
        return;
    }

    const size_t table_offset = table_pos - scripts_start;
    printf("test_level_script: scripts segment @ 0x%zx, level jump table @ +0x%zx\n",
           scripts_start, table_offset);

    SegmentTable seg_table;
    seg_table.rom_span = std::span(rom.data);
    seg_table.loadSegment(0x15, static_cast<uint32_t>(scripts_start),
                          static_cast<uint32_t>(std::min(scripts_start + 0x8000, rom.data.size())));

    // Same common-segment setup the game performs in level_main_scripts_entry
    // (we enter at the level jump table, skipping the menu).
    loadCommonSegments(seg_table, rom.data, scripts_start);

    Level level;
    LevelScriptVM vm(seg_table, level);
    vm.setLevelNum(LEVEL_BOB);

    SegmentedAddress entry { 0x15, static_cast<uint32_t>(table_offset) };
    vm.execute(entry);

    // Report what the script produced.
    for (int i = 0; i < 8; i++) {
        auto &area = level.areas[i];
        if (area.root_node) {
            printf("== Area %d ==\n", i);
            printNodeTree(*area.root_node, 0);
            printf("objects: %zu\n", area.object_infos.size());
        }
    }

    size_t loaded_models = 0;
    for (const auto &node : level.loaded_graph_node) {
        if (node) {
            loaded_models++;
        }
    }
    printf("loaded graph nodes (models): %zu\n", loaded_models);
}
