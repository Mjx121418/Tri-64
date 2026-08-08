#include "test_behavior_script.h"

#include "Level/area.h"
#include "Level/level_extract.h"
#include "Math/math.h"
#include "Memory/segment.h"
#include "Scripts/behavior_script.h"
#include "test_level_script.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <vector>

namespace {

// BehaviorScriptVM 的便捷包装：跑完 entry 处的行为脚本，返回作用对象与是否
// 正常走查到底。
struct AnalyzedBehavior {
    ObjectExtract::Object obj;
    bool ok {false};
};

AnalyzedBehavior analyzeBehavior(const SegmentTable &seg_table, SegmentedAddress entry) {
    AnalyzedBehavior out;
    BehaviorScript::BehaviorScriptVM vm(seg_table);
    vm.run(out.obj, entry);
    out.ok = vm.ok();
    return out;
}

// 在段 0x13 的 span 里按命令形状扫描 bhvDoor：
//   BEGIN + SET_INT + OR_INT + LOAD_ANIMATIONS(0x27, 门动画 0x030156C0) +
//   ANIMATE(0x28) + LOAD_COLLISION_DATA(0x2A, 门碰撞 0x0301CE78)
// 两版 ROM 的门行为地址不同（原版 0x13000B0C，Treasure World 0x13000B08），
// 不能写死，所以按命令 + 数据指针匹配。返回匹配的 BEGIN 命令所在偏移。
int32_t findDoorBehavior(const std::span<uint8_t> &seg13) {
    const auto word = [&](size_t i) -> uint32_t {
        return readInt<uint32_t>(seg13, i);
    };

    for (size_t i = 0; i + 32 <= seg13.size(); i += 4) {
        const uint32_t w0 = word(i);      // BEGIN
        const uint32_t w1 = word(i + 4);  // SET_INT
        const uint32_t w2 = word(i + 8);  // OR_INT
        const uint32_t w3 = word(i + 12); // LOAD_ANIMATIONS
        const uint32_t w4 = word(i + 16); // 门动画数组（段 3）
        const uint32_t w5 = word(i + 20); // ANIMATE
        const uint32_t w6 = word(i + 24); // LOAD_COLLISION_DATA
        const uint32_t w7 = word(i + 28); // 门碰撞（段 3）
        if ((w0 >> 24) != 0x00 || (w1 >> 24) != 0x10 || (w2 >> 24) != 0x11 ||
            (w3 >> 24) != 0x27 || w4 != 0x030156C0 || (w5 >> 24) != 0x28 ||
            (w6 >> 24) != 0x2A || w7 != 0x0301CE78) {
            continue;
        }
        return static_cast<int32_t>(i);
    }
    return -1;
}

void testDoorBehavior(const LevelScriptSetup &setup) {
    const std::span<uint8_t> seg13 =
        setup.seg_table.data(SegmentedAddress { 0x13, 0 });
    if (seg13.empty()) {
        printf("  [SKIP] no behavior segment 0x13\n");
        return;
    }

    const int32_t begin_off = findDoorBehavior(seg13);
    if (begin_off < 0) {
        printf("  [FAIL] could not locate a door behavior in segment 0x13\n");
        return;
    }
    const SegmentedAddress door { 0x13, static_cast<uint32_t>(begin_off) };
    printf("  door behavior @ 0x%04x%06x\n", door.seg, door.offset);

    const AnalyzedBehavior a = analyzeBehavior(setup.seg_table, door);
    const ObjectExtract::Object &obj = a.obj;
    const SegmentedAddress anims = obj.addr(ObjectExtract::F::Animations);
    const SegmentedAddress collision = obj.collision_data;
    if (!a.ok) {
        printf("  [FAIL] walk returned ok=false for the door behavior\n");
        return;
    }
    if (obj.animate_index != 0 || anims.seg != 0x03 || anims.isNull() ||
        collision.seg != 0x03 || collision.isNull()) {
        printf("  [FAIL] door animations/animate/collision: anims=seg%#04x off%#06x "
               "idx=%d collision=seg%#04x off%#06x\n",
               anims.seg, anims.offset, obj.animate_index, collision.seg, collision.offset);
        return;
    }
    if (obj.hitbox_radius != 80 || obj.hitbox_height != 100) {
        printf("  [FAIL] door hitbox %.0fx%.0f (expected 80x100)\n", obj.hitbox_radius,
               obj.hitbox_height);
        return;
    }
    printf("  door anims=0x%04x%06x idx=%d collision=0x%04x%06x hitbox=%.0fx%.0f "
           "interact=0x%X flags=0x%X\n",
           anims.seg, anims.offset, obj.animate_index, collision.seg, collision.offset,
           obj.hitbox_radius, obj.hitbox_height, obj.u32(ObjectExtract::F::InteractType),
           obj.u32(ObjectExtract::F::Flags));
    // 门的行为设置了交互类型（INTERACT_DOOR = 0x8）与 OBJ_FLAG（OR_INT）
    if (obj.u32(ObjectExtract::F::InteractType) == 0) {
        printf("  [FAIL] door interact type not extracted\n");
    }
    if (obj.u32(ObjectExtract::F::Flags) == 0) {
        printf("  [FAIL] door OBJ_FLAG (OR_INT) not extracted\n");
    }
    // 门没有 SET_HURTBOX
    if (obj.hurtbox_radius != 0) {
        printf("  [FAIL] door should have no hurtbox\n");
    }
}

void testRobustness(const LevelScriptSetup &setup) {
    // 对关卡所有对象的行为脚本做健壮性走查：不崩溃；合法脚本正常走查到底。
    size_t analyzed = 0;
    size_t failed = 0;
    for (const auto &area : setup.level.areas) {
        for (const auto &obj : area.object_infos) {
            if (obj.behavior_script.isNull()) {
                continue;
            }
            const AnalyzedBehavior a =
                analyzeBehavior(setup.seg_table, obj.behavior_script);
            analyzed++;
            if (!a.ok) {
                failed++;
                printf("  [note] object model %d behavior 0x%04x%06x -> !ok\n", obj.model_id,
                       obj.behavior_script.seg, obj.behavior_script.offset);
            }
        }
    }

    // 段 0x13 开头的第一个行为（原版/两版都是一个表面对象）。
    const AnalyzedBehavior first = analyzeBehavior(
        setup.seg_table, SegmentedAddress { 0x13, 0 });
    printf("  robustness: %zu object behaviors walked (%zu !ok), first-behavior ok=%d "
           "obj_list=%d hitbox=%.0fx%.0f\n",
           analyzed, failed, first.ok, first.obj.obj_list, first.obj.hitbox_radius,
           first.obj.hitbox_height);

    // 越界/空地址要优雅地返回 !ok（不崩溃）。
    const AnalyzedBehavior bad =
        analyzeBehavior(setup.seg_table, SegmentedAddress { 0x13, 0xFFFFFF });
    if (bad.ok) {
        printf("  [FAIL] out-of-bounds behavior address returned ok\n");
        failed++;
    }
    if (failed != 0) {
        printf("  [FAIL] %zu robustness failures\n", failed);
    }
}

} // namespace

// 验证门模型烘焙了动画 frame-0：门 geo 是 TRANS(78) + SCALE(0.25) +
// AP(-300)，帧 0 动画再加 -300（pivot 变 -600），面板局部 x 从 [0,154]
// 移到 [-75,79]（居中于原点，与门自身 ±80 碰撞对齐）。
void testDoorFrame0(LevelScriptSetup &setup) {
    constexpr int32_t kCastleGrounds = 16; // LEVEL_CASTLE_GROUNDS
    LevelExtract::Result r = LevelExtract::extract(setup.rom, kCastleGrounds, 1);
    if (!r.ok) {
        printf("  [note] castle grounds extract failed: %s\n", r.error.c_str());
        return;
    }
    // 城堡门 = special_castle_door_warp → MODEL_CASTLE_CASTLE_DOOR = 0x26，
    // 行为 bhvDoorWarp（GOTO 进 bhvDoor 的 LOAD_ANIMATIONS + ANIMATE(0)）。
    const auto it = r.object_models.find(0x26);
    if (it == r.object_models.end()) {
        printf("  [note] no castle door model (0x26) in castle grounds\n");
        return;
    }
    float minx = 1e9f, maxx = -1e9f;
    for (const auto &v : it->second.mesh.vertices) {
        minx = std::min(minx, v.position[0]);
        maxx = std::max(maxx, v.position[0]);
    }
    printf("  castle door model 0x26: %zu verts, x in [%.1f, %.1f]\n",
           it->second.mesh.vertices.size(), minx, maxx);
    if (minx < -30 && maxx > 30) {
        printf("  castle door centered (frame-0 applied)\n");
    } else {
        printf("  [FAIL] castle door not centered (frame-0 not applied)\n");
    }
}

// 验证树模型的纹理映射：材质记录 G_SETTILE 的 S/T CLAMP（导出 repeat 关闭），
// 且 v 轴不翻转 —— Godot 的 ArrayMesh UV 用 v=0 为顶部（与 N64 t=0 顶部一致），
// 模型最低点应落在纹理底部（高 v = 树干）、最高点在纹理顶部（低 v = 树冠）。
void testTreeUV(LevelScriptSetup &setup) {
    constexpr int32_t kCastleGrounds = 16;
    LevelExtract::Result r = LevelExtract::extract(setup.rom, kCastleGrounds, 1);
    if (!r.ok) {
        printf("  [note] castle grounds extract failed: %s\n", r.error.c_str());
        return;
    }
    const auto it = r.object_models.find(0x17); // MODEL_BOB_BUBBLY_TREE
    if (it == r.object_models.end()) {
        printf("  [note] no bubble tree model (0x17) in castle grounds\n");
        return;
    }
    const GBI::Mesh &mesh = it->second.mesh;
    bool all_clamp = true;
    for (const auto &m : mesh.materials) {
        all_clamp = all_clamp && m.tex_clamp_s && m.tex_clamp_t;
    }
    printf("  tree model 0x17: %zu materials, all S/T clamp=%d\n", mesh.materials.size(),
           all_clamp);

    float min_y = 1e9f, min_y_uv = 0, max_y = -1e9f, max_y_uv = 0;
    for (const auto &v : mesh.vertices) {
        if (v.position[1] < min_y) {
            min_y = v.position[1];
            min_y_uv = v.uv[1];
        }
        if (v.position[1] > max_y) {
            max_y = v.position[1];
            max_y_uv = v.uv[1];
        }
    }
    printf("  tree: minY=%.0f uv_y=%.2f, maxY=%.0f uv_y=%.2f\n", min_y, min_y_uv, max_y, max_y_uv);
    if (!all_clamp || min_y_uv < 0.5f || max_y_uv > 0.5f) {
        printf("  [FAIL] tree texture mapping: clamp=%d minY_uv=%.2f maxY_uv=%.2f\n", all_clamp,
               min_y_uv, max_y_uv);
    } else {
        printf("  tree texture upright + clamped\n");
    }
}

// 验证原生选动画的对象（goomba）应用了 frame-0：行为只有 LOAD_ANIMATIONS、无
// ANIMATE，动画（索引 0）由 goomba_update 固定设置。其 part 0（最外层 AP）有
// transY=90（geo 缩放 0.25 → 世界 +22.5）与 rotY=0x3FFF（约 90°）；不应用时
// goomba 的 body 底部在 -14（陷入地面）且朝向错误。
void testGoombaFrame0(LevelScriptSetup &setup) {
    constexpr int32_t kBob = 9;
    LevelExtract::Result r = LevelExtract::extract(setup.rom, kBob, 1);
    if (!r.ok) {
        printf("  [note] BOB extract failed: %s\n", r.error.c_str());
        return;
    }
    const auto it = r.object_models.find(0xC0); // MODEL_GOOMBA
    if (it == r.object_models.end()) {
        printf("  [note] no goomba model (0xC0) in BOB\n");
        return;
    }
    float miny = 1e9f, maxy = -1e9f;
    for (const auto &v : it->second.mesh.vertices) {
        miny = std::min(miny, v.position[1]);
        maxy = std::max(maxy, v.position[1]);
    }
    printf("  goomba model 0xC0: %zu verts, y in [%.1f, %.1f]\n",
           it->second.mesh.vertices.size(), miny, maxy);
    if (miny < 0.0f) {
        printf("  [FAIL] goomba sinks below origin (frame-0 lift not applied)\n");
    } else {
        printf("  goomba lifted above origin (frame-0 applied)\n");
    }
}

// 星星（MODEL_STAR = 0x7A）只由 level_main_scripts_entry 加载（关卡脚本不加载
// 它）；从主入口开始运行后，星星对象应能解码出网格（之前 loaded_graph_node 里
// 没有 0x7A，星星对象被丢弃、不可见）。
void testStarModel(LevelScriptSetup &setup) {
    constexpr int32_t kBob = 9;
    LevelExtract::Result r = LevelExtract::extract(setup.rom, kBob, 1);
    if (!r.ok) {
        printf("  [note] BOB extract failed: %s\n", r.error.c_str());
        return;
    }
    const auto it = r.object_models.find(0x7A); // MODEL_STAR
    if (it == r.object_models.end() || it->second.mesh.indices.empty()) {
        printf("  [FAIL] star model (0x7A) not decoded in BOB\n");
        return;
    }
    printf("  star model 0x7A: %zu verts, %zu tris, %zu materials\n",
           it->second.mesh.vertices.size(), it->second.mesh.indices.size() / 3,
           it->second.mesh.materials.size());
    printf("  star model decoded\n");
}

// 验证材质判定：纹理材质（combine 采样 TEXEL0/1，如星星的 DECALFADE/MODULATERGBA、
// goomba 的 MODULATERGB）应被判定为 textured=true；G_CC_SHADE 等未纹理材质为
// false。若 combine mux 解码错位，纹理材质会被误判为未纹理（回归）。
void testMaterialDetection(LevelScriptSetup &setup) {
    constexpr int32_t kBob = 9;
    LevelExtract::Result r = LevelExtract::extract(setup.rom, kBob, 1);
    if (!r.ok) {
        printf("  [note] BOB extract failed: %s\n", r.error.c_str());
        return;
    }
    for (const auto &[mid, model] : r.object_models) {
        if (mid != 0x7A && mid != 0xC0) { // 星星 / goomba
            continue;
        }
        size_t textured = 0, lit = 0;
        for (const auto &m : model.mesh.materials) {
            if (m.textured) {
                textured++;
            }
            if (m.lit) {
                lit++;
            }
        }
        printf("  model 0x%02X: %zu materials (textured=%zu, lit=%zu)\n", mid,
               model.mesh.materials.size(), textured, lit);
        if (textured == 0) {
            printf("  [FAIL] model 0x%02X has no textured material (combine mux decode wrong)\n",
                   mid);
        }
    }
}

void testBehaviorScript() {
    const auto roms = findRoms();
    if (roms.empty()) {
        printf("test_behavior_script: no ROM found (baserom.us.z64 or "
               "Super Mario Treasure World*.z64)\n");
        return;
    }

    for (const auto &rom : roms) {
        printf("== behavior script: %s ==\n", rom.filename().string().c_str());
        LevelScriptSetup setup = setupLevelScript(rom);
        if (!setup.ok) {
            continue;
        }
        testDoorBehavior(setup);
        testRobustness(setup);
        testDoorFrame0(setup);
        testTreeUV(setup);
        testGoombaFrame0(setup);
        testStarModel(setup);
        testMaterialDetection(setup);
    }
}
