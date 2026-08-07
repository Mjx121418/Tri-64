#include "test_collision.h"

#include "Level/level_extract.h"
#include "ROM.h"
#include "Scripts/Collision.h"
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

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

} // namespace

void testCollision() {
    const auto roms = findRoms();
    if (roms.empty()) {
        printf("test_collision: no ROM found\n");
        return;
    }

    for (const auto &path : roms) {
        ROM rom;
        rom.load(path);
        if (!rom.is_loaded) {
            continue;
        }
        const bool is_vanilla = path.filename().string().find("baserom") != std::string::npos;
        printf("== %s ==\n", path.filename().string().c_str());

        // BOB（关卡 9，区域 1）：原版碰撞.inc.c 已知计数。
        {
            LevelExtract::Result r = LevelExtract::extract(rom, 9, 1);
            if (!r.ok) {
                printf("test_collision: BOB extract failed: %s\n", r.error.c_str());
                continue;
            }
            const Collision::Data &c = r.collision;
            size_t rooms = 0;
            for (const auto &s : c.surfaces) {
                if (s.has_room) {
                    rooms++;
                }
            }
            printf("test_collision: BOB: ok=%d vertices=%zu surfaces=%zu special=%zu water=%zu rooms=%zu\n",
                   c.ok, c.vertices.size(), c.surfaces.size(), c.special_objects.size(),
                   c.water_boxes.size(), rooms);

            // 解析成三角形网格（平坦着色）
            const Collision::TriangleMesh tm = Collision::buildTriangleMesh(c);
            printf("test_collision: BOB: triangles=%zu indices=%zu\n", tm.positions.size() / 3,
                   tm.indices.size());
            if (!c.ok || tm.indices.empty() || tm.positions.size() != tm.indices.size()) {
                printf("test_collision: FAIL BOB triangle mesh is inconsistent\n");
            }
            if (is_vanilla && tm.indices.size() != 1060 * 3) {
                printf("test_collision: FAIL BOB triangle index count (expect %d)\n", 1060 * 3);
            }

            if (is_vanilla) {
                if (c.vertices.size() != 570) {
                    printf("test_collision: FAIL BOB vertex count (expect 570)\n");
                }
                if (c.surfaces.size() != 1060) {
                    printf("test_collision: FAIL BOB surface count (expect 1060)\n");
                }
                if (c.special_objects.size() != 17) {
                    printf("test_collision: FAIL BOB special object count (expect 17)\n");
                }
                if (!c.water_boxes.empty()) {
                    printf("test_collision: FAIL BOB should have no water boxes\n");
                }
            }

            // 对象的碰撞数据（行为 LOAD_COLLISION_DATA）：原版 BOB 应有对象碰撞。
            size_t obj_collisions = 0;
            for (const auto &oc : r.object_collisions) {
                if (oc.ok && !oc.surfaces.empty()) {
                    obj_collisions++;
                }
            }
            printf("test_collision: BOB: objects=%zu with collision=%zu\n", r.objects.size(),
                   obj_collisions);
            if (is_vanilla && obj_collisions < 20) {
                printf("test_collision: FAIL vanilla BOB has too few object collisions\n");
            }
        }

        // HMC（关卡 7，区域 1）：原版带 ROOMS 数据。
        {
            LevelExtract::Result r = LevelExtract::extract(rom, 7, 1);
            if (!r.ok) {
                printf("test_collision: HMC extract failed: %s\n", r.error.c_str());
                continue;
            }
            const Collision::Data &c = r.collision;
            size_t rooms = 0;
            for (const auto &s : c.surfaces) {
                if (s.has_room) {
                    rooms++;
                }
            }
            printf("test_collision: HMC: ok=%d vertices=%zu surfaces=%zu special=%zu water=%zu rooms=%zu\n",
                   c.ok, c.vertices.size(), c.surfaces.size(), c.special_objects.size(),
                   c.water_boxes.size(), rooms);
            if (!c.ok || c.surfaces.empty()) {
                printf("test_collision: FAIL HMC did not decode cleanly\n");
            }
            if (is_vanilla && rooms == 0) {
                printf("test_collision: FAIL HMC should have room data\n");
            }
        }
    }
}
