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
        // Test a few levels: BOB(9), CCM(5), WF(24), BBH(4), LLL(22),
        // VCUTM(18), CastleGrounds(16, no course), Castle(6, no course)
        for (int lv : {9, 5, 24, 4, 22, 18, 16, 6}) {
            std::string name = LevelExtract::extractLevelName(rom, lv);
            printf("  LevelNum %2d -> '%s'\n", lv, name.c_str());
        }
    }
}
