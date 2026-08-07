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
            size_t obj_collision_triangles = 0;
            for (const auto &oc : r.object_collisions) {
                if (oc.ok && !oc.surfaces.empty()) {
                    obj_collisions++;
                    // 每个有碰撞的对象都应构建出非空网格：每三角形 1 个类型
                    //（floor/wall/ceiling）。buildTriangleMesh 会跳过越界/退化
                    // 表面，因此三角形数 ≤ 表面数。
                    const Collision::TriangleMesh tm = Collision::buildTriangleMesh(oc);
                    if (tm.indices.empty() ||
                        tm.indices.size() > oc.surfaces.size() * 3 ||
                        tm.classes.size() != tm.indices.size() / 3) {
                        printf("test_collision: FAIL object collision mesh invalid "
                               "(indices=%zu surfaces=%zu classes=%zu)\n",
                               tm.indices.size(), oc.surfaces.size(), tm.classes.size());
                    }
                    if (tm.indices.size() != oc.surfaces.size() * 3) {
                        printf("test_collision: note %zu/%zu object surfaces skipped\n",
                               oc.surfaces.size() - tm.indices.size() / 3, oc.surfaces.size());
                    }
                    obj_collision_triangles += tm.indices.size() / 3;
                }
            }
            printf("test_collision: BOB: objects=%zu with collision=%zu triangles=%zu\n",
                   r.objects.size(), obj_collisions, obj_collision_triangles);
            if (is_vanilla && obj_collisions < 20) {
                printf("test_collision: FAIL vanilla BOB has too few object collisions\n");
            }
            if (is_vanilla && obj_collision_triangles < 100) {
                printf("test_collision: FAIL vanilla BOB has too few object collision triangles\n");
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

        // 移动纹理（水/熔岩）：城堡外侧（16）有水面，LLL（22）有熔岩面。
        for (int lv : {16, 22}) {
            LevelExtract::Result r = LevelExtract::extract(rom, lv, 1);
            if (!r.ok) {
                continue;
            }
            size_t lava = 0;
            for (const auto &q : r.movtex.quads) {
                if (q.texture_id == 4) { // TEXTURE_LAVA
                    lava++;
                }
            }
            printf("test_collision: movtex lv%d: quads=%zu (lava=%zu)\n", lv, r.movtex.quads.size(),
                   lava);
            if (is_vanilla && r.movtex.quads.empty()) {
                printf("test_collision: FAIL lv%d movtex: no quads extracted\n", lv);
            }
            if (lv == 22 && is_vanilla && lava == 0) {
                printf("test_collision: FAIL LLL movtex: expected lava quads\n");
            }
        }

        // 逐顶点光照（受光材质）：城堡外侧的灰色草地纹理（0x9004800）由绿色灯光
        // 着色，其受光顶点应偏绿（g > r）；且不应出现全黑顶点（shade 至少含环境光）。
        {
            LevelExtract::Result r = LevelExtract::extract(rom, 16, 1);
            if (r.ok) {
                size_t lit = 0, green_tinted = 0, black = 0;
                for (size_t m = 0; m < r.mesh.materials.size(); m++) {
                    if (!r.mesh.materials[m].lit || r.mesh.material_images[m] != 0x9004800u) {
                        continue;
                    }
                    for (size_t t = 0; t < r.mesh.material_ids.size(); t++) {
                        if (r.mesh.material_ids[t] != m) {
                            continue;
                        }
                        for (int k = 0; k < 3; k++) {
                            const auto &c = r.mesh.vertices[r.mesh.indices[t * 3 + k]].color;
                            lit++;
                            if (c[1] > c[0]) {
                                green_tinted++;
                            }
                            if (c[0] < 8 && c[1] < 8 && c[2] < 8) {
                                black++;
                            }
                        }
                    }
                }
                printf("test_collision: castle lighting: grey-grass lit vtx=%zu green=%zu black=%zu\n",
                       lit, green_tinted, black);
                if (is_vanilla && lit == 0) {
                    printf("test_collision: FAIL castle grey-grass has no lit vertices\n");
                }
                if (is_vanilla && green_tinted == 0) {
                    printf("test_collision: FAIL castle grey-grass not green-tinted (lighting)\n");
                }
                if (is_vanilla && black > 0) {
                    printf("test_collision: FAIL castle grey-grass has black-shaded vertices\n");
                }
            }
        }
    }
}
