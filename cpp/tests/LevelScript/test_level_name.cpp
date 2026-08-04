#include "Level/level_extract.h"
#include "ROM.h"
#include <cstdio>
#include <filesystem>

void testLevelName() {
    const char *candidates[] = {
        "baserom.us.z64",
        "tests/baserom.us.z64",
        "Super Mario Treasure World [v1.2.1].z64",
    };

    for (const char *candidate : candidates) {
        ROM rom;
        rom.load(std::filesystem::path(candidate));
        if (!rom.is_loaded) continue;

        printf("== %s ==\n", candidate);
        auto all_names = LevelExtract::loadAllLevelNames(rom);
        printf("  all level names (one-shot): %zu entries\n", all_names.size());
        for (int lv : {9, 5, 24, 4, 22, 18, 16, 6}) {
            auto it = all_names.find(lv);
            printf("  LevelNum %2d -> '%s'\n", lv,
                   it != all_names.end() ? it->second.c_str() : "");
        }
    }
}
