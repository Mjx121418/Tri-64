#include "test_object.h"

#include "Level/graph_node.h"
#include "Level/level_extract.h"
#include "Log.h"
#include "Memory/segment.h"
#include "ROM.h"
#include "test_parallel.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <span>
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

    TestParallel::parallelFor(roms.size(), [&](size_t rom_index) {
        const auto &path = roms[rom_index];
        ROM rom;
        rom.load(path);
        if (!rom.is_loaded) {
            return;
        }
        const bool is_vanilla = path.filename().string().find("baserom") != std::string::npos;
        printf("== %s ==\n", path.filename().string().c_str());

        // BOB（关卡 9，区域 1）。
        LevelExtract::Result r = LevelExtract::extract(rom, 9, 1);
        if (!r.ok) {
            printf("test_object: BOB extract failed: %s\n", r.error.c_str());
            return;
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

        // Inline object extraction replays ordinary object geo/DLs with the instance
        // transform already active at G_VTX time. Billboard objects remain on the
        // camera-facing cached-part path.
        size_t inline_objects = 0;
        size_t inline_triangles = 0;
        size_t inline_billboard_parts = 0;
        bool inline_projection_ok = r.inline_object_models.size() == r.objects.size();
        for (size_t i = 0; i < r.inline_object_models.size(); i++) {
            const auto &model = r.inline_object_models[i];
            inline_billboard_parts += model.billboard_parts.size();
            if (model.mesh.indices.empty()) {
                continue;
            }
            inline_objects++;
            inline_triangles += model.mesh.indices.size() / 3;
            inline_projection_ok = inline_projection_ok &&
                model.mesh.triangle_layers.size() == model.mesh.material_ids.size();
            for (const auto &vertex : model.mesh.vertices) {
                inline_projection_ok = inline_projection_ok && vertex.projected;
            }
        }
        printf("test_object: inline instances=%zu triangles=%zu billboard_parts=%zu projected=%d\n",
               inline_objects, inline_triangles, inline_billboard_parts,
               int(inline_projection_ok));
        if (is_vanilla && (inline_objects == 0 || !inline_projection_ok)) {
            printf("test_object: FAIL inline object draw stream\n");
        }

        if (is_vanilla) {
            // 默认 act_num=0 = "所有 act"（忽略 OBJECT_WITH_ACTS 掩码）：
            // 原版 BOB 比 act 1 多 10 个 act 专属 OBJECT 放置（103 → 113）。
            if (r.objects.size() != 113 || with_model != 99) {
                printf("test_object: FAIL BOB all-acts object counts (objects=%zu model!=0=%zu, "
                       "expect 113/99)\n",
                       r.objects.size(), with_model);
            }
            // act 1 显式提取 = 原版 act-1 黄金值（OBJECT_WITH_ACTS 掩码过滤）。
            {
                LevelExtract::Result act1 = LevelExtract::extract(rom, 9, 1, 1);
                size_t act1_objects = 0;
                size_t act1_models = 0;
                for (const auto &obj : act1.objects) {
                    act1_objects++;
                    if (obj.model_id != 0) {
                        act1_models++;
                    }
                }
                if (act1_objects != 103 || act1_models != 92) {
                    printf("test_object: FAIL BOB act-1 object counts (objects=%zu model!=0=%zu, "
                           "expect 103/92)\n",
                           act1_objects, act1_models);
                }
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

            // GEO_BILLBOARD 子树按 billboard 节点拆成 billboard_parts（每个
            // { pivot, 网格 }）；网格顶点相对 pivot 定位（pivot-relative，渲染端
            // 在 pivot 上每帧面向相机）。每个三角形三顶点 z 相同（位置只取父矩阵
            // 平移，几何本身不旋转/缩放；法线字节在带灯光 DL 里可能是无效数据，
            // 只查位置平面性）。
            size_t billboard_models = 0;
            size_t billboard_bad_tris = 0;
            for (const auto &[mid, model] : r.object_models) {
                if (model.billboard_parts.empty()) {
                    continue;
                }
                billboard_models++;
                for (const auto &part : model.billboard_parts) {
                    const auto &v = part.mesh.vertices;
                    for (size_t t = 0; t < part.mesh.indices.size() / 3; t++) {
                        const uint32_t i0 = part.mesh.indices[t * 3];
                        const uint32_t i1 = part.mesh.indices[t * 3 + 1];
                        const uint32_t i2 = part.mesh.indices[t * 3 + 2];
                        if (std::abs(v[i0].position[2] - v[i1].position[2]) > 0.1f ||
                            std::abs(v[i0].position[2] - v[i2].position[2]) > 0.1f) {
                            billboard_bad_tris++;
                        }
                    }
                }
            }
            printf("test_object: billboard models=%zu bad-billboard-tris=%zu\n",
                   billboard_models, billboard_bad_tris);
            if (is_vanilla && billboard_models == 0) {
                printf("test_object: FAIL no BOB model has a billboard part (expect goomba)\n");
            }
            if (is_vanilla && billboard_bad_tris != 0) {
                printf("test_object: FAIL billboard triangles not plane-aligned\n");
            }

            // 绘制层（每三角形，geo 节点 flags 高 8 位）：区域网格与材质表平行且
            // 取值 0-7；goomba body 全 LAYER_OPAQUE(1)、眼睛 billboard 部分全
            // LAYER_ALPHA(4)（geo.inc.c 的 GEO_BILLBOARD → LAYER_ALPHA DL）。
            bool area_layers_ok = r.mesh.triangle_layers.size() == r.mesh.material_ids.size();
            for (uint8_t l : r.mesh.triangle_layers) {
                if (l > 7) {
                    area_layers_ok = false;
                }
            }
            bool goomba_body_opaque = false;
            bool goomba_eyes_alpha = false;
            bool goomba_layers_parallel = false;
            const auto goomba_it = r.object_models.find(0xC0);
            if (goomba_it != r.object_models.end()) {
                const auto &body = goomba_it->second.mesh;
                goomba_layers_parallel =
                    body.triangle_layers.size() == body.material_ids.size();
                goomba_body_opaque = !body.triangle_layers.empty();
                for (uint8_t l : body.triangle_layers) {
                    if (l != 1) {
                        goomba_body_opaque = false;
                    }
                }
                goomba_eyes_alpha = false;
                for (const auto &part : goomba_it->second.billboard_parts) {
                    if (part.mesh.triangle_layers.size() != part.mesh.material_ids.size()) {
                        goomba_layers_parallel = false;
                    }
                    for (uint8_t l : part.mesh.triangle_layers) {
                        goomba_eyes_alpha = l == 4;
                    }
                }
            }
            printf("test_object: layers area_ok=%d goomba body=OPAQUE=%d eyes=ALPHA=%d "
                   "parallel=%d\n",
                   int(area_layers_ok), int(goomba_body_opaque), int(goomba_eyes_alpha),
                   int(goomba_layers_parallel));
            if (is_vanilla && (!area_layers_ok || !goomba_body_opaque || !goomba_eyes_alpha
                               || !goomba_layers_parallel)) {
                printf("test_object: FAIL triangle layers (area 0-7, goomba body 1, eyes 4)\n");
            }
        }

        // 城堡内侧（关卡 6，区域 1）：area geo 用 GEO_SWITCH_CASE(17,
        // geo_switch_area) 分房间；静态导出应收集所有分支（所有房间）的 DL，
        // 而不是只取 case 0（主厅核心）。
        {
            LevelExtract::Result castle = LevelExtract::extract(rom, 6, 1);
            if (!castle.ok) {
                printf("test_object: castle extract failed: %s\n", castle.error.c_str());
                return;
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
                return;
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

        // 背景（geo 0x19 节点）：BOB = OCEAN_SKY(0) 天空盒，段 0x0A 解码出
        // 10×8 图块贴图集（320×256 RGBA8，有天空内容）；城堡内侧 =
        // GEO_BACKGROUND_COLOR(0x0001) 纯色填充（func = 0，无贴图集）。
        {
            if (is_vanilla) {
                if (!r.background.is_skybox() || r.background.background != 0) {
                    printf("test_object: FAIL BOB background not OCEAN_SKY skybox "
                           "(id=%d func=%08x)\n",
                           r.background.background, r.background.func);
                }
                if (r.skybox.pixels.size() != size_t(r.skybox.width) * r.skybox.height * 4
                    || r.skybox.width != 320 || r.skybox.height != 256) {
                    printf("test_object: FAIL BOB skybox size (%ux%u)\n", r.skybox.width,
                           r.skybox.height);
                    for (const auto &w : r.warnings) {
                        if (w.stage == "skybox") {
                            printf("test_object:   skybox warning: %s\n", w.message.c_str());
                        }
                    }
                } else {
                    size_t non_black = 0, blue = 0;
                    for (size_t i = 0; i < r.skybox.pixels.size(); i += 4) {
                        const uint8_t rr = r.skybox.pixels[i];
                        const uint8_t gg = r.skybox.pixels[i + 1];
                        const uint8_t bb = r.skybox.pixels[i + 2];
                        if (rr || gg || bb) {
                            non_black++;
                        }
                        if (bb > rr && bb > gg) {
                            blue++;
                        }
                    }
                    printf("test_object: BOB skybox non-black px=%zu blue px=%zu\n", non_black,
                           blue);
                    if (non_black == 0 || blue == 0) {
                        printf("test_object: FAIL BOB skybox content empty\n");
                    }
                }
            }
        }

        // 各关卡背景 id（vanilla）：CCM=4 SNOW_MOUNTAINS, SSL=5 DESERT,
        // WDW=2 UNDERWATER_CITY, BBH=6 HAUNTED, JRB=12 ABOVE_CLOUDS。
        if (is_vanilla) {
            const std::pair<int, int16_t> expect_bg[] = {
                {5, 4},   // CCM
                {8, 5},   // SSL
                {11, 2},  // WDW
                {4, 6},   // BBH
                {12, 8},  // JRB
            };
            for (const auto &[level, id] : expect_bg) {
                LevelExtract::Result lr = LevelExtract::extract(rom, level, 1);
                if (!lr.ok) {
                    printf("test_object: FAIL background level %d extract\n", level);
                    continue;
                }
                if (!lr.background.is_skybox() || lr.background.background != id) {
                    printf("test_object: FAIL level %d background id=%d (expect %d)\n", level,
                           lr.background.background, id);
                }
            }

            // 城堡内侧 = 纯色填充（func 空，background = RGBA5551 0x0001）。
            LevelExtract::Result castle = LevelExtract::extract(rom, 6, 1);
            if (castle.ok) {
                if (castle.background.is_skybox() || castle.background.background != 0x0001
                    || !castle.skybox.pixels.empty()) {
                    printf("test_object: FAIL castle-inside background not fill 0x0001 "
                           "(id=%d func=%08x)\n",
                           castle.background.background, castle.background.func);
                }
            }
        }
    });
}

// GEO_BILLBOARD 语义：billboard 子树按节点拆成 billboard_parts（每个
// { pivot, 网格 }）；pivot = 父链（含旋转/缩放）应用到节点平移，网格顶点相对
// pivot 定位，几何本身不旋转/缩放。镜像 geo_process_billboard 的 mtxf_billboard
//（decomp math_util.c）：位置取父矩阵平移，朝向在相机空间轴对齐（渲染端在
// pivot 上每帧面向相机）。合成场景：yaw 90° 旋转节点下挂
// [billboard(t=(100,0,0), DL)] 和另一个三角形 DL。期望：
// - billboard 部分：pivot = 父矩阵旋转后的平移（yaw 90° 把 (100,0,0) 转到 Z 轴：
//   |pivot.z|≈100, pivot.x≈0）；网格 pivot-relative：顶点 == 作者几何
//   （(-10,-10,0) 等，z==0），法线仍 (0,0,1)；
// - 普通三角形被 yaw 90° 旋转（法线转到 X 轴）。
void testBillboardSplit() {
    std::vector<uint8_t> seg(0x100, 0);

    auto putElem = [&](int k, int32_t fixed) {
        seg[2 * k] = uint8_t((fixed >> 24) & 0xFF);
        seg[2 * k + 1] = uint8_t((fixed >> 16) & 0xFF);
        seg[32 + 2 * k] = uint8_t((fixed >> 8) & 0xFF);
        seg[33 + 2 * k] = uint8_t(fixed & 0xFF);
    };
    putElem(0, 0x00010000); // 单位矩阵 @0x1E:0x00
    putElem(5, 0x00010000);
    putElem(10, 0x00010000);
    putElem(15, 0x00010000);

    auto putVtx = [&](int i, int16_t x, int16_t y) {
        size_t o = 0x40 + size_t(i) * 16;
        auto putS16 = [&](size_t off, int16_t v) {
            seg[o + off] = uint8_t(v >> 8);
            seg[o + off + 1] = uint8_t(v & 0xFF);
        };
        putS16(0, x);
        putS16(2, y); // z = 0（XY 平面）
        seg[o + 12] = 0;
        seg[o + 13] = 0;
        seg[o + 14] = 127; // 法线 (0,0,1)
    };
    putVtx(0, -10, -10);
    putVtx(1, 10, -10);
    putVtx(2, 0, 10);

    auto putCmd = [&](size_t off, uint32_t w0, uint32_t w1) {
        for (int i = 0; i < 4; i++) {
            seg[off + i] = uint8_t(w0 >> (24 - 8 * i));
            seg[off + 4 + i] = uint8_t(w1 >> (24 - 8 * i));
        }
    };
    putCmd(0x70, 0x01020040, 0x1E000000); // G_MTX（单位矩阵 @0x1E:0x00）
    putCmd(0x78, 0x04200030, 0x1E000040); // G_VTX（3 顶点 → 槽 3..5）
    putCmd(0x80, 0xBF000000, 0x00000A14); // G_TRI1（v0=0, v1=1, v2=2）
    putCmd(0x88, 0xB8000000, 0x00000000); // G_ENDDL
    putCmd(0x90, 0xB8000000, 0x00000000); // G_ENDDL（空 DL，旋转节点的 DL）

    SegmentTable seg_table;
    seg_table.rom_span = std::span(seg);
    seg_table.loadSegment(0x1E, 0, static_cast<uint32_t>(seg.size()));

    // geo 树：root → rotation(yaw 90° = 0x4000, 层 2) → [billboard(t=(100,0,0), 层 4,
    // DL), DL]。层的写入与游戏一致：DL 所在节点的 flags 高 8 位。
    GraphNode root;
    auto rot = std::make_unique<GraphNode>();
    rot->data = GraphNodeRotation {SegmentedAddress {0x1E, 0x90}, {0, 0x4000, 0}};
    rot->flags = (2 << 8) | GraphNodeFlag::GRAPH_RENDER_ACTIVE;
    auto bb = std::make_unique<GraphNode>();
    bb->data = GraphNodeBillboard {SegmentedAddress {0x1E, 0x70}, {100, 0, 0}};
    bb->flags = (4 << 8) | GraphNodeFlag::GRAPH_RENDER_ACTIVE;
    rot->addChild(std::move(bb));
    auto body_dl = std::make_unique<GraphNode>();
    body_dl->data = GraphNodeDisplayList {SegmentedAddress {0x1E, 0x70}};
    body_dl->flags = (2 << 8) | GraphNodeFlag::GRAPH_RENDER_ACTIVE;
    rot->addChild(std::move(body_dl));
    root.addChild(std::move(rot));

    WarningLog warnings;
    ObjectExtract::ObjectModelDecoder decoder(seg_table, warnings);
    decoder.runModel(&root);
    const ObjectExtract::ObjectModel &m = decoder.model();

    printf("test_billboard_split: body tris=%zu billboard parts=%zu\n",
           m.mesh.indices.size() / 3, m.billboard_parts.size());
    if (m.mesh.indices.size() / 3 != 1 || m.billboard_parts.size() != 1) {
        printf("test_billboard_split: FAIL expected 1/1 body tri / billboard part\n");
    }

    // 绘制层：body 三角形全在层 2，billboard 部分全在层 4（按 flags>>8 记录）。
    bool body_layer_ok = m.mesh.triangle_layers.size() == m.mesh.material_ids.size()
        && m.mesh.triangle_layers.size() == 1 && m.mesh.triangle_layers[0] == 2;
    bool part_layer_ok = !m.billboard_parts.empty()
        && m.billboard_parts[0].mesh.triangle_layers.size() == 1
        && m.billboard_parts[0].mesh.triangle_layers[0] == 4;
    if (!body_layer_ok || !part_layer_ok) {
        printf("test_billboard_split: FAIL triangle layers (body=2 part=4)\n");
    }

    if (m.billboard_parts.size() == 1) {
        const auto &part = m.billboard_parts[0];
        // pivot = 父矩阵旋转后的平移：yaw 90° 把 (100,0,0) 转到 Z 轴。
        if (std::abs(part.pivot.x) > 1.0f || std::abs(std::abs(part.pivot.z) - 100.0f) > 1.0f) {
            printf("test_billboard_split: FAIL billboard pivot not parent-rotated "
                   "(p=(%.1f,%.1f,%.1f))\n",
                   part.pivot.x, part.pivot.y, part.pivot.z);
        }
        const auto &bb_verts = part.mesh.vertices;
        if (bb_verts.size() != 3) {
            printf("test_billboard_split: FAIL billboard vertices=%zu\n", bb_verts.size());
        } else {
            // pivot-relative：顶点 == 作者几何（z==0，无旋转/缩放），法线仍 (0,0,1)。
            if (std::abs(bb_verts[0].position[0] + 10.0f) > 0.1f ||
                std::abs(bb_verts[0].position[1] + 10.0f) > 0.1f ||
                std::abs(bb_verts[0].position[2]) > 0.1f ||
                std::abs(bb_verts[0].normal[2]) < 0.9f ||
                std::abs(bb_verts[0].normal[0]) > 0.1f) {
                printf("test_billboard_split: FAIL billboard mesh not pivot-relative "
                       "(v=(%.1f,%.1f,%.1f) n=(%.2f,%.2f,%.2f))\n",
                       bb_verts[0].position[0], bb_verts[0].position[1], bb_verts[0].position[2],
                       bb_verts[0].normal[0], bb_verts[0].normal[1], bb_verts[0].normal[2]);
            }
        }
    }

    // 普通三角形：被 yaw 90° 旋转（法线 (0,0,1) → X 轴）。
    const auto &body_verts = m.mesh.vertices;
    if (body_verts.size() == 3) {
        if (std::abs(body_verts[0].normal[0]) < 0.9f ||
            std::abs(body_verts[0].normal[2]) > 0.1f) {
            printf("test_billboard_split: FAIL body triangle not rotated by yaw 90 "
                   "(n=(%.2f,%.2f,%.2f))\n",
                   body_verts[0].normal[0], body_verts[0].normal[1], body_verts[0].normal[2]);
        }
    }
}
