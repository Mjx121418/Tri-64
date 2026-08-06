#include "Level/level_extract.h"

#include "Scripts/behavior_script.h"
#include "Scripts/level_script.h"
#include "Scripts/preset_tables.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <optional>
#include <span>

namespace LevelExtract {

namespace {

// 与 tests/LevelScript 相同的定位模式：
// - 脚本段首命令 = 唯一的段 0x04 加载（原版 MIO0 或 hack 的原始拷贝）
// - script_exec_level_table[0] = GET_OR_SET(OP_GET, VAR_CURR_LEVEL_NUM)
constexpr std::array<uint8_t, 4> kScriptsStartMio0Pattern { 0x18, 0x0C, 0x00, 0x04 };
constexpr std::array<uint8_t, 4> kScriptsStartRawPattern { 0x17, 0x0C, 0x00, 0x04 };
constexpr std::array<uint8_t, 4> kLevelTablePattern { 0x3C, 0x04, 0x01, 0x03 };

size_t findPattern(const std::vector<uint8_t> &rom, const std::array<uint8_t, 4> &pattern,
                   size_t from = 0) {
    auto it = std::search(rom.begin() + static_cast<ptrdiff_t>(from), rom.end(),
                          pattern.begin(), pattern.end());
    if (it == rom.end()) {
        return rom.size();
    }
    return static_cast<size_t>(it - rom.begin());
}

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

void loadCommonSegments(SegmentTable &seg_table, const std::vector<uint8_t> &rom,
                        size_t scripts_start) {
    for (size_t off = 0; off < 5 * 0x0C; off += 0x0C) {
        const uint8_t *cmd = rom.data() + scripts_start + off;
        const uint16_t seg = (cmd[2] << 8) | cmd[3];
        const uint32_t rom_start = readBE32(cmd + 4);
        const uint32_t rom_end = readBE32(cmd + 8);

        if (cmd[0] == 0x18) { // LOAD_MIO0
            if (!seg_table.loadMIO0Segment(seg, rom_start, rom_end)) {
                printf("loadCommonSegments: failed to load MIO0 segment %d from 0x%x-0x%x\n",
                       seg, rom_start, rom_end);
            }
        } else if (cmd[0] == 0x17) { // LOAD_RAW
            seg_table.loadSegment(seg, rom_start, rom_end);
        }
    }
}

// Load segment 2 (contains seg2_course_name_table) into the segment table
// at seg 0x02.  Detects SM64 Editor hacks by the MIO0 header at 0x800000.
void loadSegment2(SegmentTable &seg_table, const std::vector<uint8_t> &rom_data) {
    const bool is_editor_hack = rom_data.size() > 0x800004 &&
        rom_data[0x800000] == 'M' && rom_data[0x800001] == 'I' &&
        rom_data[0x800002] == 'O' && rom_data[0x800003] == '0';
    if (is_editor_hack) {
        seg_table.loadSegment(0x02, 0x803156, 0x81BB64);
    } else {
        if (!seg_table.loadMIO0Segment(0x02, 0x108A40, 0x114750)) {
            printf("loadSegment2: failed to load vanilla segment 2 from 0x108A40-0x114750\n");
        }
    }
}

void collectDisplayLists(const GraphNode &node, std::vector<SegmentedAddress> &out) {
    if (std::holds_alternative<GraphNodeDisplayList>(node.data)) {
        out.push_back(std::get<GraphNodeDisplayList>(node.data).display_list);
    }

    // 开关节点：只取选中的 case（静态导出 = case 0）
    if (std::holds_alternative<GraphNodeSwitchCase>(node.data)) {
        const auto &sw = std::get<GraphNodeSwitchCase>(node.data);
        if (!node.children.empty()) {
            const int16_t idx = sw.selected_case >= 0 ? sw.selected_case : 0;
            collectDisplayLists(*node.children[std::min<size_t>(idx, node.children.size() - 1)],
                                out);
        }
        return;
    }

    // LOD 节点：取包含相机距离 0 的档位（近景）
    if (std::holds_alternative<GraphNodeLevelOfDetail>(node.data)) {
        const auto &lod = std::get<GraphNodeLevelOfDetail>(node.data);
        if (lod.min_distance <= 0 && 0 < lod.max_distance) {
            for (const auto &child : node.children) {
                collectDisplayLists(*child, out);
            }
        }
        return;
    }

    for (const auto &child : node.children) {
        collectDisplayLists(*child, out);
    }
}

} // namespace

// 游戏主段（代码+数据，含宏/特殊对象 preset 表）由启动入口（asm/entry.s）
// DMA 到 RDRAM 0x80200000，ROM 起 0x1000；段 0 基址 0x80000000，故游戏里
// 的 seg-0 地址 = 主段内偏移 + 0x200000。关卡脚本不加载它，这里按主段内
// 偏移线性载入 seg 0。主段 ROM 范围由入口的 BSS 清零指令推出（清除从
// _mainSegmentNoloadStart 起 _mainSegmentNoloadSize 字节）。
bool loadMainSegment(SegmentTable &seg_table, const std::vector<uint8_t> &rom) {
    // 入口序言：lui t0, 0x8034 ; lui t1, 0x0002（BSS 清零循环初始化）
    constexpr std::array<uint8_t, 4> kEntrySignature { 0x3C, 0x08, 0x80, 0x34 };    const size_t pos = findPattern(rom, kEntrySignature);
    if (pos == rom.size() || pos + 12 > rom.size()) {
        printf("loadMainSegment: could not locate the boot entry\n");
        return false;
    }
    // 必须紧跟 lui t1, 0x0002（BSS 大小高 16 位），排除误报
    if (rom[pos + 4] != 0x3C || rom[pos + 5] != 0x09 || rom[pos + 6] != 0x00 || rom[pos + 7] != 0x02) {
        printf("loadMainSegment: boot entry signature mismatch\n");
        return false;
    }

    const uint32_t lui = readBE32(rom.data() + pos);          // lui t0, <bss_start_hi>
    const uint32_t addiu = readBE32(rom.data() + pos + 8);    // addiu t0, t0, <bss_start_lo>
    const uint32_t bss_start = ((lui & 0xFFFF) << 16) + static_cast<int16_t>(addiu & 0xFFFF);
    constexpr uint32_t kMainVirtualBase = 0x80200000;
    if (bss_start < kMainVirtualBase) {
        printf("loadMainSegment: implausible BSS start 0x%x\n", bss_start);
        return false;
    }
    const uint32_t main_size = bss_start - kMainVirtualBase;
    if (pos + main_size > rom.size()) {
        printf("loadMainSegment: main range 0x%zx-0x%zx out of ROM\n", pos, pos + main_size);
        return false;
    }
    seg_table.loadSegment(0x00, static_cast<uint32_t>(pos), static_cast<uint32_t>(pos + main_size));
    printf("loadMainSegment: main @ ROM 0x%zx, %u bytes -> seg 0\n", pos, main_size);
    return true;
}

// 定位脚本段、加载公共段、运行目标关卡的关卡脚本，返回构建好的段表与场景图。
// rom 必须在调用期间保持有效（seg_table.rom_span 指向 rom.data）。
struct ScriptContext {
    SegmentTable seg_table;
    Level level;
    bool ok {false};
    std::string error;
};

ScriptContext runLevelScript(ROM &rom, int level_num) {
    ScriptContext ctx;
    if (!rom.is_loaded) {
        ctx.error = "ROM not loaded";
        return ctx;
    }

    const size_t scripts_start = findScriptsStart(rom.data);
    const size_t table_pos = findPattern(rom.data, kLevelTablePattern, scripts_start);
    if (scripts_start == rom.data.size() || table_pos == rom.data.size()) {
        ctx.error = "could not locate the level scripts segment";
        return ctx;
    }
    ctx.seg_table.rom_span = std::span(rom.data);
    ctx.seg_table.loadSegment(0x15, static_cast<uint32_t>(scripts_start),
                              static_cast<uint32_t>(
                                  std::min(scripts_start + 0x8000, rom.data.size())));
    // 主入口自身会加载 4/3/0x17/0x16/0x13（loadCommonSegments 冗余但无害）；
    // 段 2 与主段是我们的补充（课程名 / preset 表）。
    loadCommonSegments(ctx.seg_table, rom.data, scripts_start);
    loadSegment2(ctx.seg_table, rom.data);
    loadMainSegment(ctx.seg_table, rom.data);

    // 从 level_main_scripts_entry（脚本段首）开始运行整个主入口：它会加载公共
    // 模型（星星/金币/1UP 等只在这里 LOAD_MODEL_FROM_GEO）、跳过文件选择菜单
    //（menu 的 JUMP_IF(reg==0) 直接 EXIT），随后经 script_exec_level_table 按
    // 已设置的 level_num 自然分发到目标关卡脚本，关卡脚本以 CALL_LOOP 结束
    //（此时 VM 停止，即关卡已加载）。
    LevelScriptVM vm(ctx.seg_table, ctx.level);
    vm.setLevelNum(level_num);
    vm.execute(SegmentedAddress { 0x15, 0 });

    ctx.ok = true;
    return ctx;
}

// SM64 US character encoding → ASCII for course name strings.
// The course names use: space, 0-9, A-Z, ', -, comma, period, null.
std::string decodeSM64String(const uint8_t *data, size_t max_len) {
    std::string result;
    for (size_t i = 0; i < max_len; i++) {
        uint8_t c = data[i];
        if (c == 0xFF) break; // null terminator
        if (c >= 0x00 && c <= 0x09) {
            result += static_cast<char>('0' + c);
        } else if (c >= 0x0A && c <= 0x23) {
            result += static_cast<char>('A' + (c - 0x0A));
        } else if (c >= 0x24 && c <= 0x3D) {
            result += static_cast<char>('a' + (c - 0x24));
        } else if (c == 0x3E) {
            result += '\'';
        } else if (c == 0x3F) {
            result += '.';
        } else if (c == 0x6F) {
            result += ',';
        } else if (c == 0x9E) {
            result += ' ';
        } else if (c == 0x9F) {
            result += '-';
        } else {
            result += '?'; // unknown character
        }
    }
    return result;
}

// Static LevelNum → CourseNum mapping, derived from decomp's level_table.h.
// Courses not listed here have COURSE_NONE (0) and no entry in the name table.
static int32_t levelNumToCourseNum(int32_t level_num) {
    switch (level_num) {
        case 4:  return 5;   // BBH
        case 5:  return 4;   // CCM
        case 7:  return 6;   // HMC
        case 8:  return 8;   // SSL
        case 9:  return 1;   // BOB
        case 10: return 10;  // SL
        case 11: return 11;  // WDW
        case 12: return 3;   // JRB
        case 13: return 13;  // THI
        case 14: return 14;  // TTC
        case 15: return 15;  // RR
        case 17: case 30: return 16; // BITDW / BOWSER_1
        case 18: return 22;  // VCUTM
        case 19: case 33: return 17; // BITFS / BOWSER_2
        case 20: return 24;  // SA
        case 21: case 34: return 18; // BITS / BOWSER_3
        case 22: return 7;   // LLL
        case 23: return 9;   // DDD
        case 24: return 2;   // WF
        case 27: return 19;  // PSS
        case 28: return 20;  // COTMC
        case 29: return 21;  // TOTWC
        case 31: return 23;  // WMOTR
        case 36: return 12;  // TTM
        default: return 0;   // COURSE_NONE (castle, grounds, unknown, etc.)
    }
}

// seg2_course_name_table is at offset 0x10F68 within decompressed segment 2
// (segmented address 0x0210F68).  It's an array of 27 segmented pointers (u32 each).
// Segment 2 is always loaded at seg 0x02 by runLevelScript().
constexpr uint32_t kCourseNameTableOffset = 0x10F68;

std::string readCourseName(SegmentTable &seg_table, int32_t level_num) {
    const int32_t course_num = levelNumToCourseNum(level_num);
    if (course_num <= 0 || course_num > 25) {
        return {};
    }

    const int16_t seg2 = 0x02;

    // Bounds check: the segment must be large enough for the table + the
    // requested entry's 4-byte pointer.
    const size_t table_offset = (course_num - 1) * 4;
    auto seg_data = seg_table.data(SegmentedAddress { seg2, 0 });
    if (seg_data.size() <= kCourseNameTableOffset + table_offset + 4) {
        return {};
    }

    auto table_data = seg_data.subspan(kCourseNameTableOffset, table_offset + 4);
    const uint8_t *ptr_bytes = table_data.data() + table_offset;
    uint32_t str_seg_addr = readBE32(ptr_bytes);
    if (str_seg_addr == 0) {
        return {};
    }

    const uint32_t str_offset = str_seg_addr & 0xFFFFFF;
    if (str_offset + 256 > seg_data.size()) {
        return {};
    }
    auto str_data = seg_data.subspan(str_offset, 256);
    if (str_data.size() < 1) {
        return {};
    }

    std::string raw = decodeSM64String(str_data.data(), str_data.size());

    // SM64 course name strings have leading spaces and a number prefix like
    // " 1 BOB-OMB BATTLEFIELD" or "   BOWSER IN THE DARK WORLD".
    // Strip leading spaces and the course number prefix.
    size_t start = 0;
    while (start < raw.size() && raw[start] == ' ') {
        start++;
    }
    while (start < raw.size() && raw[start] >= '0' && raw[start] <= '9') {
        start++;
    }
    while (start < raw.size() && raw[start] == ' ') {
        start++;
    }
    return raw.substr(start);
}

Result extract(ROM &rom, int level_num, int area_index) {
    Result result;
    ScriptContext ctx = runLevelScript(rom, level_num);
    if (!ctx.ok) {
        result.error = ctx.error;
        return result;
    }

    // 记录该关卡所有有效区域（供 UI 下拉列表使用）
    for (int i = 0; i < 8; i++) {
        if (ctx.level.areas[i].root_node) {
            result.areas.push_back(i);
        }
    }

    if (area_index < 0 || area_index >= 8 || !ctx.level.areas[area_index].root_node) {
        result.error = "area " + std::to_string(area_index) + " not found";
        return result;
    }
    const Area &area = ctx.level.areas[area_index];

    // 解码碰撞数据（TERRAIN/ROOMS 命令地址）
    if (area.terrain_addr.seg >= 0 && area.terrain_addr.seg <= 31) {
        result.collision = Collision::decode(ctx.seg_table, area.terrain_addr, area.rooms_addr);
    }

    // 收集该区域的所有 DL，合并进一个 GBI::Mesh（去重键与 OBJ 导出一致：
    // 材质内容 + 解析出的纹理源图像）。
    std::vector<SegmentedAddress> dls;
    collectDisplayLists(*area.root_node, dls);

    GBI::Mesh &merged = result.mesh;
    for (const auto &dl : dls) {
        GBI::DLInterpreter interp(ctx.seg_table);
        GBI::Mesh &mesh = interp.run(dl);
        ObjectExtract::mergeMesh(merged, std::move(mesh));
    }

    // 每材质解码一个 RGBA8 纹理（与 merged.materials 并行；解码失败留空）
    result.textures.resize(merged.materials.size());
    for (size_t m = 0; m < merged.materials.size(); m++) {
        if (merged.materials[m].textured && merged.material_images[m] != 0) {
            auto tex = GBI::decodeTexture(merged.materials[m],
                                          segAddress(merged.material_images[m]), ctx.seg_table);
            if (tex) {
                result.textures[m] = std::move(*tex);
            }
        }
    }

    // 对象出生点 = OBJECT 命令 + MACRO_OBJECTS 展开 + 碰撞特殊对象展开。
    // 宏/特殊对象 preset 表从主段（段 0）解析出 model/param/behavior。
    const std::vector<PresetTables::MacroPreset> macro_presets =
        PresetTables::parseMacroPresets(ctx.seg_table);
    const std::vector<PresetTables::SpecialPreset> special_presets =
        PresetTables::parseSpecialPresets(ctx.seg_table);

    std::vector<ObjectSpawnInfo> objects = area.object_infos;
    ObjectExtract::expandMacroObjects(area.macro_objects, static_cast<int8_t>(area_index),
                                      macro_presets, objects);
    ObjectExtract::expandSpecialObjects(result.collision.special_objects,
                                        static_cast<int8_t>(area_index), special_presets, objects);

    // 对象模型：每个唯一 model id 只解码一次（复用同模型的所有对象实例）。
    // model_id 0（MODEL_NONE，如传送点）没有几何，跳过。有动画行为的对象
    // （LOAD_ANIMATIONS + ANIMATE）在烘焙时叠加动画 frame-0 值（如门）。
    std::map<int16_t, ObjectExtract::ObjectModel> object_models;
    for (const auto &obj : objects) {
        if (obj.model_id <= 0 || object_models.contains(obj.model_id)) {
            continue;
        }
        std::optional<ObjectExtract::Frame0Animator> frame0;
        if (!obj.behavior_script.isNull()) {
            const BehaviorScript::Info bi =
                BehaviorScript::analyze(ctx.seg_table, obj.behavior_script);
            // 动画是原生代码选的（LOAD_ANIMATIONS 但无 ANIMATE 命令，如 goomba
            // 由 goomba_update 固定用索引 0）：静态导出默认取第一个动画的 frame-0
            //（通常是静止姿态），否则部件停在裸 geo 锚点上（goomba 会陷入地面且
            // 朝向错误，见 docs/engine-notes.md）。
            if (bi.ok && !bi.animations.isNull()) {
                const int16_t anim_index = (bi.animate_index >= 0) ? bi.animate_index : 0;
                frame0.emplace(ctx.seg_table, bi.animations, anim_index);
            }
        }
        ObjectExtract::ObjectModel model = ObjectExtract::decodeModel(
            ctx.seg_table, ctx.level.loaded_graph_node[obj.model_id].get(),
            frame0 ? &*frame0 : nullptr);
        if (model.mesh.indices.empty()) {
            continue;
        }
        object_models[obj.model_id] = std::move(model);
    }
    result.object_models = std::move(object_models);

    result.objects = std::move(objects);
    result.mario_start_pos = { static_cast<float>(ctx.level.mario_start_pos.x),
                               static_cast<float>(ctx.level.mario_start_pos.y),
                               static_cast<float>(ctx.level.mario_start_pos.z) };
    result.mario_start_angle_y = ctx.level.mario_start_angle_y;
    result.level_name = readCourseName(ctx.seg_table, level_num);
    result.ok = true;
    return result;
}

std::vector<int> listAreas(ROM &rom, int level_num) {
    std::vector<int> areas;
    ScriptContext ctx = runLevelScript(rom, level_num);
    if (!ctx.ok) {
        return areas;
    }
    for (int i = 0; i < 8; i++) {
        if (ctx.level.areas[i].root_node) {
            areas.push_back(i);
        }
    }
    return areas;
}

std::string extractLevelName(ROM &rom, int level_num) {
    ScriptContext ctx = runLevelScript(rom, level_num);
    if (!ctx.ok) {
        return {};
    }
    return readCourseName(ctx.seg_table, level_num);
}

std::map<int, std::string> loadAllLevelNames(ROM &rom) {
    std::map<int, std::string> names;
    if (!rom.is_loaded) return names;

    SegmentTable seg_table;
    seg_table.rom_span = std::span(rom.data);
    loadSegment2(seg_table, rom.data);

    constexpr int kLevels[] = {
        4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
        20, 21, 22, 23, 24, 26, 27, 28, 29, 30, 31, 33, 34, 36
    };
    for (int lv : kLevels) {
        std::string name = readCourseName(seg_table, lv);
        if (!name.empty()) {
            names[lv] = name;
        }
    }
    return names;
}

} // namespace LevelExtract
