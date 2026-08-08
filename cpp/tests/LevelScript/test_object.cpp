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
            if (r.objects.size() != 101 || with_model != 91) {
                printf("test_object: FAIL BOB object counts (objects=%zu model!=0=%zu, expect "
                       "101/91)\n",
                       r.objects.size(), with_model);
            }
            if (goombas != 2 || bobombs != 12 || bubble_trees != 17) {
                printf("test_object: FAIL BOB per-model counts (goombas=%zu bobombs=%zu "
                       "bubble_trees=%zu, expect 2/12/17)\n",
                       goombas, bobombs, bubble_trees);
            }

            // 木牌（macro_wooden_signpost，bhvMessagePanel，DIALOG_050=0x32）：
            // 检查宏对象的 oBhvParams 打包规则（spawn_macro_objects）。
            const ObjectExtract::Object *signpost = nullptr;
            for (const auto &obj : r.objects) {
                if (obj.model_id == 0x7C &&
                    closeEnough(obj.pos().x, -3530) && closeEnough(obj.pos().y, 1415) &&
                    closeEnough(obj.pos().z, 430)) {
                    signpost = &obj;
                    break;
                }
            }
            if (signpost == nullptr) {
                printf("test_object: FAIL wooden signpost (-3530,1415,430) not found\n");
            } else {
                printf("test_object: signpost pos=(%.0f,%.0f,%.0f) yaw=%d\n", signpost->pos().x,
                       signpost->pos().y, signpost->pos().z, signpost->faceAngle().y);
                if (signpost->s32(ObjectExtract::F::BhvParams) != 0x00320000 ||
                    signpost->s32(ObjectExtract::F::BhvParams2ndByte) != 0x32) {
                    printf("test_object: FAIL signpost oBhvParams=%08x 2ndByte=%x (expect "
                           "00320000/32)\n",
                           static_cast<uint32_t>(signpost->s32(ObjectExtract::F::BhvParams)),
                           static_cast<uint32_t>(signpost->s32(ObjectExtract::F::BhvParams2ndByte)));
                }
                if (signpost->faceAngle().y == 0) {
                    printf("test_object: FAIL signpost yaw is 0\n");
                }
            }
        }
    }
}
