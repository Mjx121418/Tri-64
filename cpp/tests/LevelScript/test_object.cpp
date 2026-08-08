#include "test_object.h"

#include "Level/level_extract.h"
#include "ROM.h"
#include <cstdio>
#include <filesystem>
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

bool closeEnough(float a, float b, float eps = 1.0f) {
    return a > b - eps && a < b + eps;
}

} // namespace

// 统一 Object：OBJECT 命令 / 宏对象 / 碰撞特殊对象都变换成 ObjectExtract::Object，
// 初始状态镜像 decomp（spawn_object / spawn_macro_object / spawn_special_objects）。
void testObject() {
    const auto roms = findRoms();
    if (roms.empty()) {
        printf("test_object: no ROM found\n");
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

        // BOB（关卡 9，区域 1）。
        LevelExtract::Result r = LevelExtract::extract(rom, 9, 1);
        if (!r.ok) {
            printf("test_object: BOB extract failed: %s\n", r.error.c_str());
            continue;
        }

        size_t with_model = 0;
        size_t goombas = 0;
        size_t bobombs = 0;
        size_t bubble_trees = 0;
        bool bad_angle = false;
        for (const auto &obj : r.objects) {
            if (obj.model_id != 0) {
                with_model++;
            }
            if (obj.model_id == 0xC0) { // MODEL_GOOMBA（宏对象）
                goombas++;
            }
            if (obj.model_id == 0xBC) { // MODEL_BLACK_BOBOMB（宏对象）
                bobombs++;
            }
            if (obj.model_id == 0x17) { // MODEL_BOB_BUBBLY_TREE（碰撞特殊对象）
                bubble_trees++;
            }
            // 出生即 faceAngle == moveAngle（镜像 spawn_object_abs_with_rot）。
            if (obj.faceAngle().x != obj.moveAngle().x || obj.faceAngle().y != obj.moveAngle().y ||
                obj.faceAngle().z != obj.moveAngle().z) {
                bad_angle = true;
            }
        }

        printf("test_object: BOB objects=%zu (model!=0: %zu), goombas=%zu, bobombs=%zu, "
               "bubble_trees=%zu, bad_angle=%d\n",
               r.objects.size(), with_model, goombas, bobombs, bubble_trees, int(bad_angle));
        if (bad_angle) {
            printf("test_object: FAIL faceAngle != moveAngle\n");
        }
        if (r.objects.empty()) {
            printf("test_object: FAIL no objects\n");
        }

        if (is_vanilla) {
            if (r.objects.size() != 103 || with_model != 92) {
                printf("test_object: FAIL BOB object counts (objects=%zu model!=0=%zu, expect "
                       "103/92)\n",
                       r.objects.size(), with_model);
            }
            if (goombas != 2 || bobombs != 12 || bubble_trees != 17) {
                printf("test_object: FAIL BOB per-model counts (goombas=%zu bobombs=%zu "
                       "bubble_trees=%zu, expect 2/12/17)\n",
                       goombas, bobombs, bubble_trees);
            }

            // 木牌（macro_wooden_signpost，bhvMessagePanel，DIALOG_050=0x32）：
            // oBhvParams 打包规则 + DROP_TO_FLOOR 落到脚下（出生 y=1415 → 地面）。
            const ObjectExtract::Object *signpost = nullptr;
            for (const auto &obj : r.objects) {
                if (obj.model_id == 0x7C && closeEnough(obj.pos().x, -3530) &&
                    closeEnough(obj.pos().z, 430)) {
                    signpost = &obj;
                    break;
                }
            }
            if (signpost == nullptr) {
                printf("test_object: FAIL wooden signpost (-3530,?,430) not found\n");
            } else {
                const float floor =
                    Collision::findFloorHeight(r.collision, -3530, 1415 + 200, 430)
                        .value_or(-1e9f);
                printf("test_object: signpost pos=(%.0f,%.0f,%.0f) yaw=%d floor=%.0f\n",
                       signpost->pos().x, signpost->pos().y, signpost->pos().z,
                       signpost->faceAngle().y, floor);
                if (signpost->s32(ObjectExtract::F::BhvParams) != 0x00320000 ||
                    signpost->s32(ObjectExtract::F::BhvParams2ndByte) != 0x32) {
                    printf("test_object: FAIL signpost oBhvParams=%08x 2ndByte=%x (expect "
                           "00320000/32)\n",
                           static_cast<uint32_t>(signpost->s32(ObjectExtract::F::BhvParams)),
                           static_cast<uint32_t>(signpost->s32(ObjectExtract::F::BhvParams2ndByte)));
                }
                if (signpost->pos().y >= 1415 ||
                    !closeEnough(signpost->pos().y, floor, 1.0f)) {
                    printf("test_object: FAIL signpost not dropped to floor (y=%.0f floor=%.0f)\n",
                           signpost->pos().y, floor);
                }
            }

            // 链锁汪汪（bhvChainChomp）在出生路径 SPAWN_CHILD_WITH_PARAM 出木头桩
            // （MODEL_WOODEN_POST = 0x6B）子对象（frame-0 即存在）。
            size_t wooden_posts = 0;
            for (const auto &obj : r.objects) {
                if (obj.model_id == 0x6B) {
                    wooden_posts++;
                }
            }
            if (wooden_posts == 0) {
                printf("test_object: FAIL no wooden-post children (SPAWN_CHILD expansion)\n");
            } else {
                printf("test_object: wooden-post children=%zu\n", wooden_posts);
            }

            // oGraphYOffset：行为 SET_FLOAT 捕获到字段（渲染位置 = oPosY + offset）。
            size_t off_objects = 0;
            for (const auto &obj : r.objects) {
                if (obj.f32(ObjectExtract::F::GraphYOffset) != 0.0f) {
                    off_objects++;
                    printf("test_object: graphYOffset obj model=0x%02X offset=%.0f\n",
                           obj.model_id, obj.f32(ObjectExtract::F::GraphYOffset));
                }
            }
            if (off_objects == 0) {
                printf("test_object: FAIL no BOB object sets oGraphYOffset\n");
            }

            // 世界空间光照：对象模型材质应携带自己的灯光（goomba 用 Lights1：
            // 1 个方向光 + 环境光），供渲染端 shader 用。
            bool goomba_lights = false;
            size_t goomba_light_mats = 0;
            for (const auto &[mid, model] : r.object_models) {
                if (mid != 0xC0) { // MODEL_GOOMBA
                    continue;
                }
                for (const auto &m : model.mesh.materials) {
                    if (m.lit && m.lights.loaded && m.lights.num_lights >= 1) {
                        goomba_lights = true;
                        goomba_light_mats++;
                    }
                }
            }
            printf("test_object: goomba lit materials with lights=%zu\n", goomba_light_mats);
            if (is_vanilla && !goomba_lights) {
                printf("test_object: FAIL goomba materials lack captured lights\n");
            }
        }

        // 城堡内侧（关卡 6，区域 1）：area geo 用 GEO_SWITCH_CASE(17,
        // geo_switch_area) 分房间；静态导出应收集所有分支（所有房间）的 DL，
        // 而不是只取 case 0（主厅核心）。
        {
            LevelExtract::Result castle = LevelExtract::extract(rom, 6, 1);
            if (!castle.ok) {
                printf("test_object: castle extract failed: %s\n", castle.error.c_str());
                continue;
            }
            const size_t triangles = castle.mesh.indices.size() / 3;
            printf("test_object: castle_inside geometry triangles=%zu materials=%zu\n", triangles,
                   castle.mesh.materials.size());
            if (is_vanilla && triangles < 5000) {
                printf("test_object: FAIL castle_inside geometry too small (expect all 17 "
                       "rooms, not just case 0)\n");
            }
        }

        // BITS（关卡 21）：八边形平台（0x39）顶面局部法线 (0,127,0)，网格导出的
        // 法线应仍朝上（世界空间光照 shader 依赖它）。
        {
            LevelExtract::Result bits = LevelExtract::extract(rom, 21, 1);
            if (bits.ok) {
                const auto it = bits.object_models.find(0x39);
                if (it != bits.object_models.end()) {
                    float up_count = 0, total = 0;
                    for (const auto &v : it->second.mesh.vertices) {
                        total++;
                        if (v.normal[1] > 0.9f) {
                            up_count++;
                        }
                    }
                    printf("test_object: BITS octa platform up-normals=%.0f/%.0f\n", up_count,
                           total);
                    if (is_vanilla && up_count < 8) { // 顶面朝上（不应反转）
                        printf("test_object: FAIL BITS octa platform normals not up\n");
                    }
                }
            }
        }

        // WF（关卡 24）：LAYER_TRANSPARENT_DECAL 的黄色三角形（顶点色
        // {0xff,0xff,0x00,0x80}，G_LIGHTING 清除，combine 未设置 —— 继承自前面
        // DL 的 G_CC_SHADE）。经状态继承后材质应为 未纹理 + 未受光 +
        // 颜色源 SHADE（顶点色 = 黄），验证 combine 感知修复。
        {
            LevelExtract::Result wf = LevelExtract::extract(rom, 24, 1);
            if (!wf.ok) {
                printf("test_object: WF extract failed: %s\n", wf.error.c_str());
                continue;
            }
            size_t yellow = 0;
            bool shade_material = false;
            int shade_material_alpha = -1;
            bool shade_material_uniform = true;
            for (size_t t = 0; t < wf.mesh.material_ids.size(); t++) {
                const GBI::Material &m = wf.mesh.materials[wf.mesh.material_ids[t]];
                for (int k = 0; k < 3; k++) {
                    const auto &c = wf.mesh.vertices[wf.mesh.indices[t * 3 + k]].color;
                    if (c[0] == 0xFF && c[1] == 0xFF && c[2] == 0x00 && c[3] == 0x80) {
                        yellow++;
                        if (!m.textured && !m.lit &&
                            GBI::combineColorSource(m.combine_w0, m.combine_w1) ==
                                GBI::CombineSource::Shade) {
                            shade_material = true;
                            // 该材质所有顶点 alpha 应一致（0x80），桥端据此导出
                            // 材质透明度（否则按不透明处理）。
                            if (shade_material_alpha < 0) {
                                shade_material_alpha = c[3];
                            } else if (shade_material_alpha != c[3]) {
                                shade_material_uniform = false;
                            }
                        }
                    }
                }
            }
            printf("test_object: WF yellow-decal vtx=%zu shade-material=%d alpha=%d uniform=%d\n",
                   yellow, int(shade_material), shade_material_alpha, int(shade_material_uniform));
            if (is_vanilla && yellow == 0) {
                printf("test_object: FAIL WF yellow-decal vertices not found\n");
            }
            if (is_vanilla && !shade_material) {
                printf("test_object: FAIL WF yellow-decal not color-source Shade\n");
            }
            if (is_vanilla && (shade_material_alpha != 0x80 || !shade_material_uniform)) {
                printf("test_object: FAIL WF yellow-decal alpha not uniform 0x80\n");
            }
        }
    }
}
