#include "test_level_script.h"

#include "Level/area.h"
#include "Level/dl_interpreter.h"
#include "Level/level_extract.h"
#include "Level/texture.h"
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

} // namespace

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

// Collects every GraphNodeDisplayList in the area tree.
void collectDisplayLists(const GraphNode &node, std::vector<SegmentedAddress> &out) {
    if (std::holds_alternative<GraphNodeDisplayList>(node.data)) {
        out.push_back(std::get<GraphNodeDisplayList>(node.data).display_list);
    }
    for (const auto &child : node.children) {
        collectDisplayLists(*child, out);
    }
}

LevelScriptSetup setupLevelScript(const std::filesystem::path &rom_path) {
    LevelScriptSetup setup;
    setup.rom.load(rom_path);
    if (!setup.rom.is_loaded) {
        printf("test_level_script: could not load %s\n", rom_path.string().c_str());
        return setup;
    }

    // Locate the level-scripts segment (0x15) and the level jump table inside
    // it. The jump table pattern can also occur in unrelated data (e.g. MIPS
    // code bytes), so it is only searched within the scripts segment.
    const size_t scripts_start = findScriptsStart(setup.rom.data);
    const size_t table_pos = findPattern(setup.rom.data, kLevelTablePattern, scripts_start);
    if (scripts_start == setup.rom.data.size() || table_pos == setup.rom.data.size()) {
        printf("test_level_script: %s: could not locate the level scripts segment\n",
               rom_path.string().c_str());
        return setup;
    }

    const size_t table_offset = table_pos - scripts_start;
    printf("== %s ==\n", rom_path.filename().string().c_str());
    printf("test_level_script: scripts segment @ 0x%zx, level jump table @ +0x%zx\n",
           scripts_start, table_offset);

    setup.seg_table.rom_span = std::span(setup.rom.data);
    setup.seg_table.loadSegment(0x15, static_cast<uint32_t>(scripts_start),
                                static_cast<uint32_t>(std::min(scripts_start + 0x8000, setup.rom.data.size())));

    // Same common-segment setup the game performs in level_main_scripts_entry
    // (we enter at the level jump table, skipping the menu).
    loadCommonSegments(setup.seg_table, setup.rom.data, scripts_start);

    LevelScriptVM vm(setup.seg_table, setup.level);
    vm.setLevelNum(LEVEL_BOB);

    SegmentedAddress entry { 0x15, static_cast<uint32_t>(table_offset) };
    vm.execute(entry);

    setup.ok = true;
    return setup;
}

void testRunLevelScript() {
    const auto roms = findRoms();
    if (roms.empty()) {
        printf("test_level_script: no ROM found (baserom.us.z64 or Super Mario Treasure World*.z64)\n");
        return;
    }

    for (const auto &rom : roms) {
        LevelScriptSetup setup = setupLevelScript(rom);
        if (!setup.ok) {
            continue;
        }

        // Report what the script produced.
        for (int i = 0; i < 8; i++) {
            auto &area = setup.level.areas[i];
            if (area.root_node) {
                printf("== Area %d ==\n", i);
                printNodeTree(*area.root_node, 0);
                printf("objects: %zu\n", area.object_infos.size());
            }
        }

        size_t loaded_models = 0;
        for (const auto &node : setup.level.loaded_graph_node) {
            if (node) {
                loaded_models++;
            }
        }
        printf("loaded graph nodes (models): %zu\n", loaded_models);
    }
}

// 对象驱动的 Bowser 关卡（17 BITDW / 19 BITFS / 21 BITS）静态几何极少，
// 大部分内容是 OBJECT 命令生成的对象模型。验证 model_id 被记录，且
// 每个唯一模型只解码一次（对象实例共享模型资源）。
void testObjectModels() {
    const auto roms = findRoms();
    if (roms.empty()) {
        return;
    }

    for (const auto &path : roms) {
        ROM rom;
        rom.load(path);
        if (!rom.is_loaded) {
            continue;
        }

        for (int level : {9, 17, 19, 21}) {
            LevelExtract::Result r = LevelExtract::extract(rom, level, 1);
            if (!r.ok) {
                printf("test_object_models: level %d extract failed: %s\n", level,
                       r.error.c_str());
                continue;
            }

            size_t with_model = 0;
            size_t goombas = 0;
            size_t bobombs = 0;
            size_t bubble_trees = 0;
            for (const auto &obj : r.objects) {
                if (obj.model_id != 0) {
                    with_model++;
                }
                if (obj.model_id == 0xC0) { // MODEL_GOOMBA（经 MACRO_OBJECTS 展开）
                    goombas++;
                }
                if (obj.model_id == 0xBC) { // MODEL_BLACK_BOBOMB
                    bobombs++;
                }
                if (obj.model_id == 0x17) { // MODEL_BOB_BUBBLY_TREE（碰撞特殊对象）
                    bubble_trees++;
                }
            }
            printf("test_object_models: level %d: objects=%zu (model!=0: %zu), unique models=%zu"
                   " (goombas=%zu, bobombs=%zu, bubble_trees=%zu)\n",
                   level, r.objects.size(), with_model, r.object_models.size(), goombas, bobombs,
                   bubble_trees);
            for (const auto &[mid, model] : r.object_models) {
                printf("  model 0x%02X: %zu verts, %zu tris, %zu materials\n", mid,
                       model.mesh.vertices.size(), model.mesh.indices.size() / 3,
                       model.mesh.materials.size());
            }

            // 同一模型只解码一次：去重后的模型数不能超过引用模型的对象数
            if (r.object_models.size() > with_model) {
                printf("test_object_models: FAIL: more models than objects referencing them\n");
            }
            // BOB 的 MACRO_OBJECTS 应展开出对象：原版有 goomba/黑炸弹，
            // hack（如 Treasure World）可能移除 goomba，但至少应有炸弹。
            const bool is_vanilla = path.filename().string().find("baserom") != std::string::npos;
            if (level == 9 && bobombs == 0) {
                printf("test_object_models: FAIL: BOB has no bobombs\n");
            }
            if (level == 9 && is_vanilla && goombas == 0) {
                printf("test_object_models: FAIL: vanilla BOB has no goombas\n");
            }
            // 原版 BOB 的碰撞特殊对象（special_bubble_tree）应展开成模型 0x17
            if (level == 9 && is_vanilla && bubble_trees == 0) {
                printf("test_object_models: FAIL: vanilla BOB has no special-object trees\n");
            }
        }
    }
}

void testDisplayList() {
    const auto roms = findRoms();
    if (roms.empty()) {
        return;
    }

    for (const auto &rom : roms) {
        LevelScriptSetup setup = setupLevelScript(rom);
        if (!setup.ok) {
            continue;
        }

        for (int i = 0; i < 8; i++) {
            auto &area = setup.level.areas[i];
            if (!area.root_node) {
                continue;
            }

            std::vector<SegmentedAddress> dls;
            collectDisplayLists(*area.root_node, dls);
            printf("test_display_list: Area %d: %zu display lists\n", i, dls.size());

            size_t total_triangles = 0;
            size_t total_vertices = 0;
            std::vector<GBI::Material> merged_materials;
            std::vector<uint32_t> merged_images; // 与 merged_materials 并行：纹理源图像
            for (const auto &dl : dls) {
                GBI::DLInterpreter interp(setup.seg_table);
                GBI::Mesh &mesh = interp.run(dl);
                const size_t triangles = mesh.indices.size() / 3;
                printf("  DL %02x:%06x -> %zu triangles, %zu vertices, %zu materials\n",
                       dl.seg, dl.offset, triangles, mesh.vertices.size(), mesh.materials.size());
                for (size_t mi = 0; mi < mesh.materials.size(); mi++) {
                    const auto &m = mesh.materials[mi];
                    static const char *kFmt[] = { "RGBA", "YUV", "CI", "IA", "I" };
                    static const char *kSiz[] = { "4", "8", "16", "32" };
                    const char *fmt = m.tile_fmt < 5 ? kFmt[m.tile_fmt] : "?";
                    const char *siz = m.tile_siz < 4 ? kSiz[m.tile_siz] : "?";
                    SegmentedAddress img = segAddress(mesh.material_images[mi]);
                    printf("    mat: %s%s %ux%u img=%02x:%06x tex=%d\n", fmt, siz,
                           m.tex_width(), m.tex_height(), img.seg, img.offset,
                           m.textured ? 1 : 0);
                }
                total_triangles += triangles;
                total_vertices += mesh.vertices.size();
                for (size_t mi = 0; mi < mesh.materials.size(); mi++) {
                    const auto &m = mesh.materials[mi];
                    const uint32_t img = mesh.material_images[mi];
                    bool found = false;
                    for (size_t u = 0; u < merged_materials.size(); u++) {
                        if (merged_materials[u] == m && merged_images[u] == img) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        merged_materials.push_back(m);
                        merged_images.push_back(img);
                    }
                }
            }
            size_t textured_materials = 0;
            for (const auto &m : merged_materials) {
                if (m.textured) {
                    textured_materials++;
                }
            }
            printf("  total: %zu triangles, %zu vertices, %zu distinct materials (%zu textured)\n",
                   total_triangles, total_vertices, merged_materials.size(), textured_materials);
        }
    }
}

void testMatrixSupport() {
    // 合成段 0x1E：矩阵 @0x00，3 个顶点 @0x40，DL @0x70
    std::vector<uint8_t> seg(0x100, 0);

    // 平移矩阵 (100, 200, 300)：行主序、平移在最后一行；
    // 固定点布局：字节 0-31 = 各元素高 16 位，字节 32-63 = 低 16 位
    auto putElem = [&](int k, int32_t fixed) {
        seg[2 * k] = uint8_t((fixed >> 24) & 0xFF);
        seg[2 * k + 1] = uint8_t((fixed >> 16) & 0xFF);
        seg[32 + 2 * k] = uint8_t((fixed >> 8) & 0xFF);
        seg[33 + 2 * k] = uint8_t(fixed & 0xFF);
    };
    putElem(0, 0x00010000);   // m[0][0] = 1.0
    putElem(5, 0x00010000);   // m[1][1] = 1.0
    putElem(10, 0x00010000);  // m[2][2] = 1.0
    putElem(12, 100 << 16);   // m[3][0] = 100
    putElem(13, 200 << 16);   // m[3][1] = 200
    putElem(14, 300 << 16);   // m[3][2] = 300
    putElem(15, 0x00010000);  // m[3][3] = 1.0

    auto putVtx = [&](int i, int16_t x, int16_t y, int16_t z) {
        size_t o = 0x40 + size_t(i) * 16;
        auto putS16 = [&](size_t off, int16_t v) {
            seg[o + off] = uint8_t(v >> 8);
            seg[o + off + 1] = uint8_t(v & 0xFF);
        };
        putS16(0, x);
        putS16(2, y);
        putS16(4, z);
        // flag / uv / normal 保持 0
    };
    putVtx(0, 1, 2, 3);
    putVtx(1, 10, 20, 30);
    putVtx(2, 100, 200, 300);

    auto putCmd = [&](size_t off, uint32_t w0, uint32_t w1) {
        for (int i = 0; i < 4; i++) {
            seg[off + i] = uint8_t(w0 >> (24 - 8 * i));
            seg[off + 4 + i] = uint8_t(w1 >> (24 - 8 * i));
        }
    };
    putCmd(0x70, 0x01020040, 0x1E000000); // G_MTX(矩阵@0x1E:0x00, MODELVIEW|LOAD)
    putCmd(0x78, 0x04200030, 0x1E000040); // G_VTX(顶点@0x1E:0x40, n=3, v0=0)
    putCmd(0x80, 0xBF000000, 0x00000A14); // G_TRI1(v0=0, v1=1, v2=2, flag=0)
    putCmd(0x88, 0xB8000000, 0x00000000); // G_ENDDL

    SegmentTable seg_table;
    seg_table.rom_span = std::span(seg);
    seg_table.loadSegment(0x1E, 0, static_cast<uint32_t>(seg.size()));

    GBI::DLInterpreter interp(seg_table);
    GBI::Mesh &mesh = interp.run(SegmentedAddress { 0x1E, 0x70 });
    printf("test_matrix: triangles=%zu, vertices=%zu\n", mesh.indices.size() / 3,
           mesh.vertices.size());
    if (mesh.vertices.size() >= 1) {
        printf("test_matrix: v0 = (%.1f, %.1f, %.1f)  (expect 101.0, 202.0, 303.0)\n",
               mesh.vertices[0].position[0], mesh.vertices[0].position[1],
               mesh.vertices[0].position[2]);
    }
}

// 写入 Wavefront OBJ：mtllib 引用同名 .mtl，三角形按材质分组（usemtl）
void writeObj(const std::filesystem::path &path, const GBI::Mesh &mesh,
              const std::vector<GBI::Material> &materials,
              const std::vector<uint32_t> &tri_materials, const char *name) {
    std::filesystem::create_directories(path.parent_path());
    FILE *f = fopen(path.string().c_str(), "w");
    if (!f) {
        printf("test_export_obj: cannot open %s\n", path.string().c_str());
        return;
    }
    fprintf(f, "# tri-64 DL export: %s\n", name);
    fprintf(f, "mtllib %s.mtl\n", name);
    fprintf(f, "o %s\n", name);

    for (const auto &v : mesh.vertices) {
        fprintf(f, "v %f %f %f\n", v.position[0], v.position[1], v.position[2]);
    }
    for (const auto &v : mesh.vertices) {
        fprintf(f, "vt %f %f\n", v.uv[0], v.uv[1]);
    }
    for (const auto &v : mesh.vertices) {
        fprintf(f, "vn %f %f %f\n", v.normal[0], v.normal[1], v.normal[2]);
    }

    // 面：按材质分组（usemtl 只出现一次/材质）
    for (size_t m = 0; m < materials.size(); m++) {
        bool first = true;
        for (size_t t = 0; t < tri_materials.size(); t++) {
            if (tri_materials[t] != m) {
                continue;
            }
            if (first) {
                fprintf(f, "usemtl mat%02zu\n", m);
                first = false;
            }
            fprintf(f, "f %u/%u/%u %u/%u/%u %u/%u/%u\n",
                    mesh.indices[t * 3] + 1, mesh.indices[t * 3] + 1,
                    mesh.indices[t * 3] + 1,
                    mesh.indices[t * 3 + 1] + 1, mesh.indices[t * 3 + 1] + 1,
                    mesh.indices[t * 3 + 1] + 1,
                    mesh.indices[t * 3 + 2] + 1, mesh.indices[t * 3 + 2] + 1,
                    mesh.indices[t * 3 + 2] + 1);
        }
    }
    fclose(f);
}

// 写入 MTL：每个材质一个 newmtl（Kd 用 prim 颜色，map_Kd 引用解码纹理，
// map_d 引用同一纹理的 alpha 通道供查看器/Blender 做透明）
void writeMtl(const std::filesystem::path &path,
              const std::vector<GBI::Material> &materials,
              const std::vector<std::string> &matnames, const char *name) {
    FILE *f = fopen(path.string().c_str(), "w");
    if (!f) {
        printf("test_export_obj: cannot open %s\n", path.string().c_str());
        return;
    }
    fprintf(f, "# tri-64 materials: %s\n", name);
    for (size_t m = 0; m < materials.size(); m++) {
        const GBI::Material &mat = materials[m];
        fprintf(f, "newmtl mat%02zu\n", m);
        fprintf(f, "Kd %.3f %.3f %.3f\n", mat.prim_color[0] / 255.0f,
                mat.prim_color[1] / 255.0f, mat.prim_color[2] / 255.0f);
        fprintf(f, "Ks 0 0 0\n");
        if (mat.textured && m < matnames.size() && !matnames[m].empty()) {
            fprintf(f, "map_Kd textures/%s.tga\n", matnames[m].c_str());
            fprintf(f, "map_d textures/%s.tga\n", matnames[m].c_str());
        }
        fprintf(f, "\n");
    }
    fclose(f);
}

// 图层编号（decomp 的 sm64.h）：图层存在 GraphNode.flags 的高 8 位
// （geo 处理器的 cmdNodeDisplayList 写入 (drawing_layer << 8)）。
// billboard 风格的卡片（如 BOB 的树）放在 LAYER_ALPHA 及之后的透明图层上。
enum LayerType : int16_t {
    LAYER_FORCE = 0,
    LAYER_OPAQUE = 1,
    LAYER_OPAQUE_DECAL = 2,
    LAYER_OPAQUE_INTER = 3,
    LAYER_ALPHA = 4,
    LAYER_TRANSPARENT = 5,
    LAYER_TRANSPARENT_DECAL = 6,
    LAYER_TRANSPARENT_INTER = 7,
};

// 解释一组 DL 并导出为 OBJ + MTL + 纹理（export/ 目录）。
// 材质按内容 + 纹理源图像跨 DL 去重；dls 为空时不写任何文件。
// name 同时用作文件名前缀、对象名与纹理名前缀。
void exportDlsToObj(const SegmentTable &seg_table, const std::vector<SegmentedAddress> &dls,
                    const std::string &name) {
    if (dls.empty()) {
        return;
    }

    // 合并所有 DL 的网格，同时建立统一的材质表（跨 DL 去重，图像并行记录）
    GBI::Mesh merged;
    std::vector<GBI::Material> materials;
    std::vector<uint32_t> material_images; // 与 materials 并行：纹理源图像
    std::vector<uint32_t> tri_materials; // 每三角形 → 统一材质表索引

    for (const auto &dl : dls) {
        GBI::DLInterpreter interp(seg_table);
        GBI::Mesh &mesh = interp.run(dl);

        const uint32_t base = static_cast<uint32_t>(merged.vertices.size());
        merged.vertices.insert(merged.vertices.end(), mesh.vertices.begin(),
                               mesh.vertices.end());
        merged.indices.reserve(merged.indices.size() + mesh.indices.size());
        for (uint32_t idx : mesh.indices) {
            merged.indices.push_back(base + idx);
        }

        const size_t tri_count = mesh.indices.size() / 3;
        for (size_t t = 0; t < tri_count; t++) {
            const uint32_t mi = mesh.material_ids[t];
            const GBI::Material &m = mesh.materials[mi];
            const uint32_t img = mesh.material_images[mi];
            uint32_t mid = 0;
            for (; mid < materials.size(); mid++) {
                if (materials[mid] == m && material_images[mid] == img) {
                    break;
                }
            }
            if (mid == materials.size()) {
                materials.push_back(m);
                material_images.push_back(img);
            }
            tri_materials.push_back(mid);
        }
    }

    // 合并 OBJ：mtllib + 按材质分组的 usemtl
    std::filesystem::path path = "export" / std::filesystem::path(name + std::string(".obj"));
    writeObj(path, merged, materials, tri_materials, name.c_str());
    printf("test_export_obj: wrote %s (%zu triangles, %zu materials)\n",
           path.string().c_str(), tri_materials.size(), materials.size());

    // 解码每个材质的纹理（TGA，供 MTL 引用）+ 收集材质名
    static const char *kFmt[] = { "RGBA", "YUV", "CI", "IA", "I" };
    static const char *kSiz[] = { "4", "8", "16", "32" };
    std::vector<std::string> matnames(materials.size());
    for (size_t m = 0; m < materials.size(); m++) {
        char matname[96];
        snprintf(matname, sizeof(matname), "%s_mat%02zu_%s%s_%ux%u", name.c_str(), m,
                 materials[m].tile_fmt < 5 ? kFmt[materials[m].tile_fmt] : "?",
                 materials[m].tile_siz < 4 ? kSiz[materials[m].tile_siz] : "?",
                 materials[m].tex_width(), materials[m].tex_height());
        matnames[m] = matname;

        auto tex = GBI::decodeTexture(materials[m], segAddress(material_images[m]), seg_table);
        if (tex) {
            std::filesystem::path tex_dir = "export" / std::filesystem::path("textures");
            std::filesystem::create_directories(tex_dir);
            std::filesystem::path tex_path =
                tex_dir / std::filesystem::path(matname + std::string(".tga"));
            FILE *tf = fopen(tex_path.string().c_str(), "wb");
            if (tf) {
                // TGA 未压缩 32-bit RGBA：底左原点 + 8 位 alpha + 行序 = 纹理行序。
                // alpha 通道必须保留（IA16 的圆形/遮罩、RGBA16 的 1 位透明都只在
                // alpha 里），否则透明纹素在查看器/Blender 里会变成不透明黑块。
                // 配合 OBJ 的 vt（v = t/32，t=0=纹理顶部），标准查看器中方向正确。
                uint8_t hdr[18] = {0};
                hdr[2] = 2; // true-color, uncompressed
                hdr[12] = tex->width & 0xFF;
                hdr[13] = (tex->width >> 8) & 0xFF;
                hdr[14] = tex->height & 0xFF;
                hdr[15] = (tex->height >> 8) & 0xFF;
                hdr[16] = 32; // RGBA
                hdr[17] = 0x08; // bottom-left origin + 8 alpha bits
                fwrite(hdr, 1, 18, tf);
                for (size_t i = 0; i + 3 < tex->pixels.size(); i += 4) {
                    fputc(tex->pixels[i + 2], tf); // B
                    fputc(tex->pixels[i + 1], tf); // G
                    fputc(tex->pixels[i], tf);     // R
                    fputc(tex->pixels[i + 3], tf); // A
                }
                fclose(tf);
                SegmentedAddress img = segAddress(material_images[m]);
                printf("    tex: %s (%ux%u img=%02x:%06x)\n", matname, tex->width,
                       tex->height, img.seg, img.offset);
            }
        } else {
            printf("    tex: %s: %s\n", matname, tex.error().c_str());
        }
    }

    // MTL：所有材质 + 纹理引用
    std::filesystem::path mtl_path =
        "export" / std::filesystem::path(name + std::string(".mtl"));
    writeMtl(mtl_path, materials, matnames, name.c_str());
    printf("test_export_obj: wrote %s (%zu materials)\n", mtl_path.string().c_str(),
           materials.size());
}

void testExportObj() {
    const auto roms = findRoms();
    if (roms.empty()) {
        return;
    }

    for (const auto &rom : roms) {
        LevelScriptSetup setup = setupLevelScript(rom);
        if (!setup.ok) {
            continue;
        }

        std::string stem = rom.filename().string();
        const size_t dot = stem.rfind(".z64");
        if (dot != std::string::npos) {
            stem = stem.substr(0, dot);
        }

        for (int i = 0; i < 8; i++) {
            auto &area = setup.level.areas[i];
            if (!area.root_node) {
                continue;
            }

            std::vector<SegmentedAddress> dls;
            collectDisplayLists(*area.root_node, dls);
            char name[64];
            snprintf(name, sizeof(name), "%s_area%d", stem.c_str(), i);
            exportDlsToObj(setup.seg_table, dls, name);
        }
    }
}

// 收集"billboard 风格"三角形的显示列表：
//   - GraphNodeBillboard 节点（及其子树）里的 DL（GEO_BILLBOARD）
//   - 非不透明图层（LAYER_ALPHA / LAYER_TRANSPARENT / LAYER_TRANSPARENT_DECAL）
//     的 DL 节点 —— SM64 把 billboard 卡片（如 BOB 的树）放在这些图层上，
//     它们不是 3D 实体，而是始终面向相机的平面。
void collectBillboardDisplayLists(const GraphNode &node, std::vector<SegmentedAddress> &out) {
    if (std::holds_alternative<GraphNodeBillboard>(node.data)) {
        const auto &bb = std::get<GraphNodeBillboard>(node.data);
        if (bb.display_list.seg >= 0) {
            out.push_back(bb.display_list);
        }
        collectDisplayLists(node, out); // billboard 子树里的所有 DL 都算
        return;
    }
    if (std::holds_alternative<GraphNodeDisplayList>(node.data)) {
        const int16_t layer = (node.flags >> 8) & 0x0F;
        if (layer >= LAYER_ALPHA) {
            out.push_back(std::get<GraphNodeDisplayList>(node.data).display_list);
        }
    }
    for (const auto &child : node.children) {
        collectBillboardDisplayLists(*child, out);
    }
}

// 单独导出 billboard 风格三角形及其纹理（export/<stem>_area<N>_billboards.obj/.mtl），
// 与 testExportObj 导出的地形/实体几何分开，便于单独查看这些平面卡片
// （如 BOB 的树）在 3D 下不成立的问题。
void testExportBillboards() {
    const auto roms = findRoms();
    if (roms.empty()) {
        return;
    }

    for (const auto &rom : roms) {
        LevelScriptSetup setup = setupLevelScript(rom);
        if (!setup.ok) {
            continue;
        }

        std::string stem = rom.filename().string();
        const size_t dot = stem.rfind(".z64");
        if (dot != std::string::npos) {
            stem = stem.substr(0, dot);
        }

        for (int i = 0; i < 8; i++) {
            auto &area = setup.level.areas[i];
            if (!area.root_node) {
                continue;
            }

            std::vector<SegmentedAddress> dls;
            collectBillboardDisplayLists(*area.root_node, dls);

            char name[64];
            snprintf(name, sizeof(name), "%s_area%d_billboards", stem.c_str(), i);
            exportDlsToObj(setup.seg_table, dls, name);
        }
    }
}
