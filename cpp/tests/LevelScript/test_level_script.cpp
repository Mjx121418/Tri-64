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
// (0x15). level_main_scripts_entry[0] loads segment 0x04, which happens
// nowhere else, so the command is unique in both the vanilla ROM and binary
// hacks. Binary hacks often replace the MIO0 load with a raw copy:
//   - LOAD_MIO0(0x04, ...) -> 18 0C 00 04 (vanilla)
//   - LOAD_RAW (0x04, ...) -> 17 0C 00 04 (hacks)
// script_exec_level_table[0] = GET_OR_SET(OP_GET, VAR_CURR_LEVEL_NUM)
//   -> 3C 04 01 03 (opcode, size, op=OP_GET, var=VAR_CURR_LEVEL_NUM; also
//   unique in the whole game). Only searched inside the scripts segment.
constexpr std::array<uint8_t, 4> kScriptsStartMio0Pattern { 0x18, 0x0C, 0x00, 0x04 };
constexpr std::array<uint8_t, 4> kScriptsStartRawPattern { 0x17, 0x0C, 0x00, 0x04 };
constexpr std::array<uint8_t, 4> kLevelTablePattern { 0x3C, 0x04, 0x01, 0x03 };

size_t findPattern(const std::vector<uint8_t> &rom, const std::array<uint8_t, 4> &pattern, size_t from = 0) {
    auto it = std::search(rom.begin() + from, rom.end(), pattern.begin(), pattern.end());
    if (it == rom.end()) {
        return rom.size();
    }
    return static_cast<size_t>(it - rom.begin());
}

// Locates the scripts segment by its first command (a unique load of segment
// 0x04). Accepts either the vanilla MIO0 load or the raw load used by binary
// hacks. A candidate is only trusted if the level-jump-table pattern occurs
// within the following 0x8000 bytes (the table is near the segment start).
size_t findScriptsStart(const std::vector<uint8_t> &rom) {
    for (const auto &pattern : { kScriptsStartMio0Pattern, kScriptsStartRawPattern }) {
        const size_t pos = findPattern(rom, pattern);
        if (pos == rom.size()) {
            continue;
        }
        const size_t search_end = std::min(pos + 0x8000, rom.size());
        if (findPattern(rom, kLevelTablePattern, pos) < search_end) {
            return pos;
        }
    }
    return rom.size();
}

uint32_t readBE32(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// The game loads these five common segments at the top of
// level_main_scripts_entry, before any course script runs. Our test starts at
// the level jump table instead (skipping the menu), so we must perform the
// same loads ourselves. The ROM ranges are read from the commands themselves
// (they may point anywhere in the ROM, e.g. the high region of a 64MB hack).
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

// Collects every ROM the test should run: the vanilla baserom plus any ROM
// hack named "Super Mario Treasure World*.z64" (relative to the test's
// working directory, so no absolute paths are baked in).
std::vector<std::filesystem::path> findRoms() {
    std::vector<std::filesystem::path> roms;
    for (const char *candidate : { "baserom.us.z64", "tests/baserom.us.z64" }) {
        if (std::filesystem::exists(candidate)) {
            roms.emplace_back(candidate);
        }
    }

    for (const char *dir : { ".", "tests" }) {
        if (!std::filesystem::is_directory(dir)) {
            continue;
        }
        for (const auto &entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("Super Mario Treasure World", 0) == 0) {
                roms.push_back(entry.path());
            }
        }
    }
    return roms;
}

void runLevelScript(const std::filesystem::path &rom_path) {
    ROM rom;
    rom.load(rom_path);
    if (!rom.is_loaded) {
        printf("test_level_script: could not load %s\n", rom_path.string().c_str());
        return;
    }

    // Locate the level-scripts segment (0x15) and the level jump table inside
    // it. The jump table pattern can also occur in unrelated data (e.g. MIPS
    // code bytes), so it is only searched within the scripts segment.
    const size_t scripts_start = findScriptsStart(rom.data);
    const size_t table_pos = findPattern(rom.data, kLevelTablePattern, scripts_start);
    if (scripts_start == rom.data.size() || table_pos == rom.data.size()) {
        printf("test_level_script: %s: could not locate the level scripts segment\n",
               rom_path.string().c_str());
        return;
    }

    const size_t table_offset = table_pos - scripts_start;
    printf("== %s ==\n", rom_path.filename().string().c_str());
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

} // namespace

void testRunLevelScript() {
    const auto roms = findRoms();
    if (roms.empty()) {
        printf("test_level_script: no ROM found (baserom.us.z64 or Super Mario Treasure World*.z64)\n");
        return;
    }

    for (const auto &rom : roms) {
        runLevelScript(rom);
    }
}
