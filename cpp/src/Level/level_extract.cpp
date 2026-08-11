#include "Level/level_extract.h"

#include "Scripts/behavior_script.h"
#include "Scripts/level_script.h"
#include "Scripts/preset_tables.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <span>
#include <utility>

namespace LevelExtract {

namespace {

// 与 tests/LevelScript 相同的定位模式：
// - 脚本段首命令 = 唯一的段 0x04 加载（原版 MIO0 或 hack 的原始拷贝）
// - script_exec_level_table[0] = GET_OR_SET(OP_GET, VAR_CURR_LEVEL_NUM)
constexpr std::array<uint8_t, 4> kScriptsStartMio0Pattern { 0x18, 0x0C, 0x00, 0x04 };
constexpr std::array<uint8_t, 4> kScriptsStartRawPattern { 0x17, 0x0C, 0x00, 0x04 };
constexpr std::array<uint8_t, 4> kLevelTablePattern { 0x3C, 0x04, 0x01, 0x03 };

// seg2_course_name_table is at offset 0x10F68 within decompressed segment 2
// (segmented address 0x0210F68).  It's an array of 27 segmented pointers (u32 each).
// Segment 2 is always loaded at seg 0x02 by runLevelScript().
constexpr uint32_t kCourseNameTableOffset = 0x10F68;

Fast3D::Fixed viewportFixed(int16_t value) {
    // N64 Vp values use two fractional bits; convert directly to Q16.16.
    return static_cast<Fast3D::Fixed>(static_cast<int32_t>(value) << 14);
}

GBI::ProjectionContext rootProjectionContext(const GraphNodeRoot &root) {
    GBI::ProjectionContext context;
    context.root_x = static_cast<float>(root.x);
    context.root_y = static_cast<float>(root.y);
    context.root_width = static_cast<float>(root.width);
    context.root_height = static_cast<float>(root.height);
    context.viewport.scale[0] = viewportFixed(static_cast<int16_t>(root.width * 4));
    context.viewport.scale[1] = viewportFixed(static_cast<int16_t>(root.height * 4));
    context.viewport.scale[2] = viewportFixed(511);
    context.viewport.translate[0] = viewportFixed(static_cast<int16_t>(root.x * 4));
    context.viewport.translate[1] = viewportFixed(static_cast<int16_t>(root.y * 4));
    context.viewport.translate[2] = viewportFixed(511);
    context.viewport.valid = root.width > 0 && root.height > 0;
    return context;
}

void setPerspectiveContext(GBI::ProjectionContext &context,
                           const GraphNodePerspective &perspective) {
    if (!context.viewport.valid || context.root_height == 0.0f) {
        context.valid = false;
        return;
    }
    const float aspect = context.root_width / context.root_height;
    context.projection_matrix = mtxfPerspective(
        perspective.fov, aspect, static_cast<float>(perspective.near),
        static_cast<float>(perspective.far));
    context.fixed_projection_matrix = Fast3D::fromFloatMatrix(context.projection_matrix);
    context.valid = true;
}

void setOrthoContext(GBI::ProjectionContext &context,
                     const GraphNodeOrthoProjection &ortho) {
    if (!context.viewport.valid) {
        context.valid = false;
        return;
    }
    const float left = (context.root_x - context.root_width) * 0.5f * ortho.scale;
    const float right = (context.root_x + context.root_width) * 0.5f * ortho.scale;
    const float top = (context.root_y - context.root_height) * 0.5f * ortho.scale;
    const float bottom = (context.root_y + context.root_height) * 0.5f * ortho.scale;
    context.projection_matrix = mtxfOrtho(left, right, bottom, top, -2.0f, 2.0f);
    context.fixed_projection_matrix = Fast3D::fromFloatMatrix(context.projection_matrix);
    context.valid = true;
}

void setCameraContext(GBI::ProjectionContext &context, const GraphNodeCamera &camera) {
    if (!context.valid) {
        return;
    }
    context.view_matrix = camera.look_at;
    context.fixed_view_matrix = Fast3D::fromFloatMatrix(context.view_matrix);
    if (camera.roll_screen != 0) {
        const Mtxf roll = mtxfRotationZXY({0, 0, camera.roll_screen});
        context.projection_matrix = mtxfMul(context.projection_matrix, roll);
        context.fixed_projection_matrix = Fast3D::matrixMultiply(
            context.fixed_projection_matrix, Fast3D::fromFloatMatrix(roll));
    }
}

Mtxf objectRenderTransform(const ObjectExtract::Object &object) {
    const Vec3<float> position = object.pos();
    const Vec3<int16_t> angle = object.faceAngle();
    const Mtxf rotate_translate = mtxfMul(
        mtxfRotationZXY(angle),
        mtxfTranslation(position.x, position.y + object.f32(ObjectExtract::F::GraphYOffset),
                        position.z));
    // Object behavior SCALE is uniform in SM64 (oGfx.scale is a Vec3f whose
    // components are written together). Keep the matrix form explicit so the
    // same transform is passed to both float and fixed RSP paths.
    return mtxfMul(mtxfScale(object.scale.x), rotate_translate);
}

Mtxf objectBillboardTransform(const ObjectExtract::Object &object,
                              const GBI::ProjectionContext &projection) {
    const Vec3<float> position = object.pos();
    Mtxf basis = mtxfIdentity();
    if (projection.valid) {
        const Mtxf inverse_view = mtxfInverse(projection.view_matrix);
        for (size_t i = 0; i < 3; i++) {
            for (size_t j = 0; j < 3; j++) {
                basis[i][j] = inverse_view[i][j];
            }
        }
    }
    basis[3][0] = position.x;
    basis[3][1] = position.y + object.f32(ObjectExtract::F::GraphYOffset);
    basis[3][2] = position.z;
    return mtxfMul(mtxfScale(object.scale.x), basis);
}

} // namespace

size_t LevelExtractor::findPattern(const std::vector<uint8_t> &rom,
                                   const std::array<uint8_t, 4> &pattern, size_t from) {
    auto it = std::search(rom.begin() + static_cast<ptrdiff_t>(from), rom.end(),
                          pattern.begin(), pattern.end());
    if (it == rom.end()) {
        return rom.size();
    }
    return static_cast<size_t>(it - rom.begin());
}

size_t LevelExtractor::findScriptsStart() const {
    for (const auto &pattern : { kScriptsStartMio0Pattern, kScriptsStartRawPattern }) {
        const size_t pos = findPattern(rom_.data, pattern);
        if (pos == rom_.data.size()) {
            continue;
        }
        const size_t search_end = std::min(pos + 0x8000, rom_.data.size());
        if (findPattern(rom_.data, kLevelTablePattern, pos) < search_end) {
            return pos;
        }
    }
    return rom_.data.size();
}

uint32_t LevelExtractor::readBE32(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// Load segment 2 (contains seg2_course_name_table) into seg_table_ at seg 0x02.
// Detects SM64 Editor hacks by the MIO0 header at 0x800000.
void LevelExtractor::loadSegment2() {
    seg_table_.rom_span = std::span(rom_.data);
    const bool is_editor_hack = rom_.data.size() > 0x800004 &&
        rom_.data[0x800000] == 'M' && rom_.data[0x800001] == 'I' &&
        rom_.data[0x800002] == 'O' && rom_.data[0x800003] == '0';
    if (is_editor_hack) {
        seg_table_.loadSegment(0x02, 0x803156, 0x81BB64);
    } else if (!seg_table_.loadMIO0Segment(0x02, 0x108A40, 0x114750)) {
        printf("loadSegment2: failed to load vanilla segment 2 from 0x108A40-0x114750\n");
    }
}

void LevelExtractor::loadCourseNameSegment() {
    loadSegment2();
}

LevelExtractor::DisplayListCollection
LevelExtractor::collectDisplayLists(const GraphNode &root) {
    DisplayListCollection collected;
    const auto visit = [&collected](const auto &self, const GraphNode &node,
                                    const Mtxf &parent,
                                    const Fast3D::FixedMatrix &fixed_parent,
                                    const GBI::ProjectionContext &parent_projection) -> void {
        Mtxf current = parent;
        Fast3D::FixedMatrix fixed_current = fixed_parent;
        GBI::ProjectionContext projection = parent_projection;
        std::optional<SegmentedAddress> node_dl;

        std::visit([&](const auto &data) {
            using T = std::decay_t<decltype(data)>;
            Mtxf local = mtxfIdentity();
            bool has_local = false;
            if constexpr (std::is_same_v<T, GraphNodeRoot>) {
                projection = rootProjectionContext(data);
            } else if constexpr (std::is_same_v<T, GraphNodePerspective>) {
                setPerspectiveContext(projection, data);
            } else if constexpr (std::is_same_v<T, GraphNodeOrthoProjection>) {
                setOrthoContext(projection, data);
            } else if constexpr (std::is_same_v<T, GraphNodeCamera>) {
                setCameraContext(projection, data);
            } else if constexpr (std::is_same_v<T, GraphNodeDisplayList>) {
                node_dl = data.display_list;
            } else if constexpr (std::is_same_v<T, GraphNodeScale>) {
                local = mtxfScale(data.scale);
                has_local = true;
                node_dl = data.display_list;
            } else if constexpr (std::is_same_v<T, GraphNodeTranslation>) {
                local = mtxfTranslation(data.translation.x, data.translation.y,
                                         data.translation.z);
                has_local = true;
                node_dl = data.display_list;
            } else if constexpr (std::is_same_v<T, GraphNodeRotation>) {
                local = mtxfRotationZXY(data.rotation);
                has_local = true;
                node_dl = data.display_list;
            } else if constexpr (std::is_same_v<T, GraphNodeTranslationRotation>) {
                local = mtxfMul(
                    mtxfRotationZXY(data.rotation),
                    mtxfTranslation(data.translation.x, data.translation.y,
                                     data.translation.z));
                has_local = true;
                node_dl = data.display_list;
            } else if constexpr (std::is_same_v<T, GraphNodeAnimatedPart>) {
                local = mtxfTranslation(data.translation.x, data.translation.y,
                                         data.translation.z);
                has_local = true;
                node_dl = data.display_list;
            } else if constexpr (std::is_same_v<T, GraphNodeBillboard>) {
                local = mtxfTranslation(data.translation.x, data.translation.y,
                                         data.translation.z);
                has_local = true;
                node_dl = data.display_list;
            }
            if (has_local) {
                current = mtxfMul(local, current);
                fixed_current = Fast3D::matrixMultiply(
                    Fast3D::fromFloatMatrix(local), fixed_current);
            }
        }, node.data);

        // 区域背景节点（GEO_BACKGROUND 0x19，area geo 根下的 ortho 子树）：
        // 与 DL 收集同一次遍历捕获，不再单独扫描。
        if (collected.background == nullptr
            && std::holds_alternative<GraphNodeBackGround>(node.data)) {
            collected.background = &std::get<GraphNodeBackGround>(node.data);
        }

        if (node_dl) {
            DisplayListRef ref;
            ref.dl = *node_dl;
            ref.layer = static_cast<uint8_t>(node.flags >> 8);
            ref.transform = current;
            ref.fixed_transform = fixed_current;
            ref.projection = projection;
            collected.lists.push_back(ref);
        }

        // 开关节点：游戏用 geo_switch_area 按当前房间选一个分支；静态导出收集所有
        // 分支（房间）的 DL（不跨 case 去重——重复 DL 原样收集）。
        if (std::holds_alternative<GraphNodeSwitchCase>(node.data)) {
            for (const auto &child : node.children) {
                self(self, *child, current, fixed_current, projection);
            }
            return;
        }

        // LOD 节点：取包含相机距离 0 的档位（近景）
        if (std::holds_alternative<GraphNodeLevelOfDetail>(node.data)) {
            const auto &lod = std::get<GraphNodeLevelOfDetail>(node.data);
            if (lod.min_distance <= 0 && 0 < lod.max_distance) {
                for (const auto &child : node.children) {
                    self(self, *child, current, fixed_current, projection);
                }
            }
            return;
        }

        for (const auto &child : node.children) {
            self(self, *child, current, fixed_current, projection);
        }
    };
    visit(visit, root, mtxfIdentity(), Fast3D::identityMatrix(), {});
    return collected;
}

// SM64 US character encoding → ASCII for course name strings.
// The course names use: space, 0-9, A-Z, ', -, comma, period, null.
std::string LevelExtractor::decodeSM64String(const uint8_t *data, size_t max_len) {
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
int32_t LevelExtractor::levelNumToCourseNum(int32_t level_num) {
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

std::string LevelExtractor::readCourseName(int32_t level_num) const {
    const int32_t course_num = levelNumToCourseNum(level_num);
    if (course_num <= 0 || course_num > 25) {
        return {};
    }

    const int16_t seg2 = 0x02;

    // Bounds check: the segment must be large enough for the table + the
    // requested entry's 4-byte pointer.
    const size_t table_offset = (course_num - 1) * 4;
    auto seg_data = seg_table_.data(SegmentedAddress { seg2, 0 });
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

std::vector<int> LevelExtractor::validAreaIndices() const {
    std::vector<int> areas;
    for (int i = 0; i < static_cast<int>(level_.areas.size()); i++) {
        if (level_.areas[i].root_node) {
            areas.push_back(i);
        }
    }
    return areas;
}

// 游戏主段（代码+数据，含宏/特殊对象 preset 表）由启动入口（asm/entry.s）
// DMA 到 RDRAM 0x80200000，ROM 起 0x1000；段 0 基址 0x80000000，故游戏里
// 的 seg-0 地址 = 主段内偏移 + 0x200000。关卡脚本不加载它，这里按主段内
// 偏移线性载入 seg 0。主段 ROM 范围由入口的 BSS 清零指令推出（清除从
// _mainSegmentNoloadStart 起 _mainSegmentNoloadSize 字节）。
bool LevelExtractor::loadMainSegment(SegmentTable &seg_table,
                                     const std::vector<uint8_t> &rom) {
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

bool LevelExtractor::loadMainSegment() {
    return LevelExtractor::loadMainSegment(seg_table_, rom_.data);
}

void LevelExtractor::runLevelScript(int level_num, bool load_supplemental) {
    seg_table_ = SegmentTable {};
    level_ = Level {};
    ok_ = false;
    error_.clear();

    if (!rom_.is_loaded) {
        error_ = "ROM not loaded";
        return;
    }

    const size_t scripts_start = findScriptsStart();
    if (scripts_start == rom_.data.size()) {
        error_ = "could not locate the level scripts segment";
        return;
    }
    seg_table_.rom_span = std::span(rom_.data);
    seg_table_.loadSegment(0x15, static_cast<uint32_t>(scripts_start),
                           static_cast<uint32_t>(
                               std::min(scripts_start + 0x8000, rom_.data.size())));
    if (load_supplemental) {
        // 主入口自身会加载公共段 4/3/0x17/0x16/0x13；段 2 与主段是我们的
        // 补充（课程名 / preset 表）。
        loadSegment2();
        loadMainSegment();
    }

    // 从 level_main_scripts_entry（脚本段首）开始运行整个主入口：它会加载公共
    // 模型（星星/金币/1UP 等只在这里 LOAD_MODEL_FROM_GEO）、跳过文件选择菜单
    //（menu 的 JUMP_IF(reg==0) 直接 EXIT），随后经 script_exec_level_table 按
    // 已设置的 level_num 自然分发到目标关卡脚本，关卡脚本以 CALL_LOOP 结束
    //（此时 VM 停止，即关卡已加载）。
    LevelScriptVM vm(seg_table_, level_, log_);
    vm.setLevelNum(level_num);
    vm.execute(SegmentedAddress { 0x15, 0 });

    ok_ = true;
}

void LevelExtractor::loadRomManagerAreaSegment(int area_index) {
    // Rom Manager's one-bank-0xE format stores the active area's Fast3D ROM
    // range in seg 0x19. Course scripts normally stop at CALL_LOOP before
    // LOAD_AREA, so this mirrors the runtime load_area() side effect after
    // the level script has materialized its object/warp data.
    if (area_index <= 0 || area_index >= 8) {
        return;
    }

    constexpr uint32_t kAreaTableOffset = 0x5F00;
    constexpr uint32_t kMarkerOffset = 0x5FFC;
    constexpr uint32_t kMarker = 0x4BC9189A;
    constexpr uint32_t kAreaEntrySize = 0x10;
    constexpr int16_t kLevelScriptSegment = 0x19;
    constexpr int16_t kAreaDataSegment = 0x0E;

    std::span<const uint8_t> marker_data;
    try {
        marker_data = seg_table_.data(
            SegmentedAddress { kLevelScriptSegment, kMarkerOffset }, sizeof(uint32_t));
    } catch (const std::out_of_range &) {
        // Vanilla and non-Rom-Manager level-script segments do not have the
        // extended area table.
        return;
    }

    if (readInt<uint32_t>(marker_data, 0) != kMarker) {
        return;
    }

    const uint32_t entry_offset = kAreaTableOffset +
                                  static_cast<uint32_t>(area_index) * kAreaEntrySize;
    std::span<const uint8_t> entry;
    try {
        entry = seg_table_.data(
            SegmentedAddress { kLevelScriptSegment, entry_offset }, 2 * sizeof(uint32_t));
    } catch (const std::out_of_range &) {
        log_.add(
            "level_script",
            "Rom Manager area-table marker found, but area " + std::to_string(area_index) +
                " entry at seg 0x19 offset 0x" + [&]() {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%06X", entry_offset);
                    return std::string(buf);
                }() + " is outside the loaded segment; segment 0x0E was not remapped");
        return;
    }

    const uint32_t rom_start = readInt<uint32_t>(entry, 0);
    const uint32_t rom_end = readInt<uint32_t>(entry, sizeof(uint32_t));
    if (rom_start >= rom_end || rom_end > seg_table_.rom_span.size()) {
        log_.add(
            "level_script",
            "Rom Manager area " + std::to_string(area_index) + " has invalid seg 0x0E ROM "
                "range 0x" + [&]() {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%08X..%08X", rom_start, rom_end);
                    return std::string(buf);
                }() + "; segment 0x0E was not remapped");
        return;
    }

    seg_table_.loadSegment(kAreaDataSegment, rom_start, rom_end);
}

void LevelExtractor::runScriptInternal(int level_num, bool load_supplemental) {
    result_ = {};
    log_.clear();
    try {
        runLevelScript(level_num, load_supplemental);
    } catch (const std::out_of_range &e) {
        // 关卡脚本执行过程中的越界数据（hack 的坏地址等）：转成错误而非崩溃。
        log_.add("extract", "level script out of range: " + std::string(e.what()));
        result_ = {};
        result_.error = "level script out of range: " + std::string(e.what());
        result_.warnings = log_.entries();
        return;
    }
    if (!ok_) {
        result_.error = error_;
        result_.warnings = log_.entries();
        return;
    }

    result_.areas = validAreaIndices();
    result_.ok = true;
    result_.warnings = log_.entries();
}

void LevelExtractor::runScript(int level_num) {
    runScriptInternal(level_num, false);
}

void LevelExtractor::run(int level_num, int area_index) {
    runScriptInternal(level_num, true);
    if (!result_.ok) {
        return;
    }
    // The script-only result is successful, but the full extraction is not
    // complete until extractArea() reaches its final assignment below.
    result_.ok = false;

    loadRomManagerAreaSegment(area_index);

    try {
        extractArea(level_num, area_index);
    } catch (const std::out_of_range &e) {
        log_.add("extract", "extraction failed: " + std::string(e.what()));
        result_ = {};
        result_.error = "extraction failed: " + std::string(e.what());
    }
    result_.warnings = log_.entries();
}

// 提取 area_index 区域的几何/对象/碰撞（run 的第二步；抛异常表示数据越界）。
void LevelExtractor::extractArea(int level_num, int area_index) {
    if (area_index < 0 || area_index >= 8 || !level_.areas[area_index].root_node) {
        result_.error = "area " + std::to_string(area_index) + " not found";
        return;
    }
    const Area &area = level_.areas[area_index];

    // 解码碰撞数据（TERRAIN/ROOMS 命令地址）
    if (area.terrain_addr.seg >= 0 && area.terrain_addr.seg <= 31) {
        Collision::CollisionDecoder collision_decoder(seg_table_);
        collision_decoder.run(area.terrain_addr, area.rooms_addr);
        result_.collision = collision_decoder.takeData();
        if (!result_.collision.ok) {
            log_.add("collision",
                     "the level's terrain collision data at segment " +
                         std::to_string(area.terrain_addr.seg) + " offset 0x" + [&]() {
                             char buf[16];
                             std::snprintf(buf, sizeof(buf), "%06X", area.terrain_addr.offset);
                             return std::string(buf);
                         }() +
                         " could not be decoded: " + result_.collision.error);
        }
    }

    // 收集该区域的所有 DL（按渲染层），合并进一个 GBI::Mesh（去重键与 OBJ 导出
    // 一致：材质内容 + 解析出的纹理源图像）。游戏按 layer 升序渲染且 RDP 状态跨
    // DL 继承，所以按 layer 排序后用同一个解释器连续运行（仅首个 DL 复位）。
    // 背景节点与 DL 同一次遍历捕获：func 非空 = 天空盒（background 是 id 0-9，
    // 贴图在段 0x0A，关卡脚本 LOAD_MIO0 已载入）；func 为空 = background 是
    // RGBA5551 填充色（geo_process_background 画纯色）。
    DisplayListCollection display_lists = collectDisplayLists(*area.root_node);
    std::vector<DisplayListRef> dls = std::move(display_lists.lists);
    const GraphNodeBackGround *bg_node = display_lists.background;
    std::stable_sort(dls.begin(), dls.end(),
                     [](const DisplayListRef &a, const DisplayListRef &b) {
                         return a.layer < b.layer;
                     });

    result_.background = {};
    result_.skybox = {};
    if (bg_node) {
        result_.background.background = bg_node->background;
        result_.background.func = bg_node->func;
        if (result_.background.is_skybox()) {
            Skybox::SkyboxDecoder skybox_decoder(seg_table_);
            skybox_decoder.run();
            if (skybox_decoder.ok()) {
                result_.skybox = skybox_decoder.image();
            } else {
                log_.add("skybox",
                         "skybox segment 0x0A decode failed (background id " +
                             std::to_string(bg_node->background) + "): " +
                             skybox_decoder.error());
            }
        }
    }

    // 移动纹理（水/熔岩）：扫描该区域引用的段（geo/DL 所在段）里的
    // MovtexQuadCollection，提取四边形数据。
    std::array<bool, 32> movtex_seen {};
    std::vector<int16_t> movtex_segments;
    for (const auto &dl : dls) {
        if (dl.dl.seg >= 0 && dl.dl.seg <= 31 && !movtex_seen[dl.dl.seg]) {
            movtex_seen[dl.dl.seg] = true;
            movtex_segments.push_back(dl.dl.seg);
        }
    }
    if (!movtex_segments.empty()) {
        Movtex::MovtexDecoder movtex_decoder(seg_table_);
        movtex_decoder.run(movtex_segments);
        result_.movtex = movtex_decoder.data();
    }

    // 对象 = OBJECT 命令 + MACRO_OBJECTS 展开 + 碰撞特殊对象展开，全部统一成
    // Object（镜像 decomp：spawn_object / spawn_macro_object / spawn_special_objects
    // 都产生 struct Object）。宏/特殊对象 preset 表从主段（段 0）解析。
    PresetTables::PresetTableDecoder preset_decoder(seg_table_);
    preset_decoder.runMacro();
    preset_decoder.runSpecial();
    const std::vector<PresetTables::MacroPreset> &macro_presets = preset_decoder.macroPresets();
    const std::vector<PresetTables::SpecialPreset> &special_presets =
        preset_decoder.specialPresets();

    std::vector<ObjectExtract::Object> objects;
    const int8_t area_idx = static_cast<int8_t>(area_index);
    for (const auto &info : area.object_infos) {
        ObjectExtract::Object obj = ObjectExtract::Object::fromSpawnInfo(info);
        obj.area_index = area_idx;
        obj.active_area_index = area_idx;
        objects.push_back(std::move(obj));
    }
    for (const auto &m : area.macro_objects) {
        if (m.preset < 0 || static_cast<size_t>(m.preset) >= macro_presets.size()) {
            continue;
        }
        const PresetTables::MacroPreset &p = macro_presets[m.preset];
        if (p.model == 0) {
            continue; // MODEL_NONE / 生成器（如 goomba 生成器、coin 编队）
        }
        ObjectExtract::Object obj = ObjectExtract::Object::fromMacroObject(m, p);
        obj.area_index = area_idx;
        obj.active_area_index = area_idx;
        objects.push_back(std::move(obj));
    }
    for (const auto &s : result_.collision.special_objects) {
        if (static_cast<size_t>(s.preset) >= special_presets.size()) {
            continue;
        }
        const PresetTables::SpecialPreset &p = special_presets[s.preset];
        if (p.model == 0) {
            continue; // MODEL_NONE / 生成器
        }
        ObjectExtract::Object obj = ObjectExtract::Object::fromSpecialObject(s, p);
        obj.area_index = area_idx;
        obj.active_area_index = area_idx;
        objects.push_back(std::move(obj));
    }

    // 对每个对象执行行为脚本（作用于 Object，像游戏里作用于 gCurrentObject）：
    // 位置/角度增量、DROP_TO_FLOOR（用当前区域地形碰撞）、隐藏/缩放/模型覆盖、
    // 动画与碰撞数据地址、出生子对象列表。然后在出生帧结束时展开子对象。
    BehaviorScript::BehaviorScriptVM behavior_vm(seg_table_, log_);
    for (auto &obj : objects) {
        if (obj.behavior.isNull()) {
            continue;
        }
        behavior_vm.run(obj, obj.behavior, &result_.collision);
    }
    // SPAWN_CHILD/SPAWN_OBJ 的子对象（如炮管的炮筒 bhvCannonBarrel）：在父对象
    // 位置/朝向出生（spawn_object_at_origin 复制父对象 pos/angle），然后执行
    // 子行为。用索引循环，新追加的子对象也会被处理。
    for (size_t i = 0; i < objects.size(); i++) {
        const std::vector<ObjectExtract::Object::ChildSpawn> children =
            objects[i].spawned_children; // 拷贝：push_back 会失效引用
        const Vec3<float> parent_pos = objects[i].pos();
        const int32_t face_p = objects[i].s32(ObjectExtract::F::FaceAnglePitch);
        const int32_t face_y = objects[i].s32(ObjectExtract::F::FaceAngleYaw);
        const int32_t face_r = objects[i].s32(ObjectExtract::F::FaceAngleRoll);
        const int8_t area_idx = objects[i].area_index;
        for (const auto &ch : children) {
            ObjectExtract::Object child;
            child.model_id = ch.model;
            child.behavior = ch.behavior;
            child.area_index = area_idx;
            child.active_area_index = area_idx;
            child.setPos(parent_pos);
            child.s32(ObjectExtract::F::FaceAnglePitch) = face_p;
            child.s32(ObjectExtract::F::FaceAngleYaw) = face_y;
            child.s32(ObjectExtract::F::FaceAngleRoll) = face_r;
            child.s32(ObjectExtract::F::MoveAnglePitch) = face_p;
            child.s32(ObjectExtract::F::MoveAngleYaw) = face_y;
            child.s32(ObjectExtract::F::MoveAngleRoll) = face_r;
            child.s32(ObjectExtract::F::BhvParams2ndByte) = ch.param;
            if (!child.behavior.isNull()) {
                behavior_vm.run(child, child.behavior, &result_.collision);
            }
            objects.push_back(std::move(child));
        }
    }

    // Build one ordered draw stream for the area and ordinary object display lists. The
    // game appends both kinds of DL to the layer master lists, so object vertices must
    // be processed with the instance matrix already active at G_VTX time. Billboard
    // parts keep their local pivot-relative mesh, while their camera-facing basis is
    // still supplied by Godot every frame.
    struct InlineCall {
        int object_index {-1};
        SegmentedAddress dl {};
        uint8_t layer {0};
        Mtxf transform {mtxfIdentity()};
        Fast3D::FixedMatrix fixed_transform {Fast3D::identityMatrix()};
        GBI::ProjectionContext projection {};
        bool object_billboard {false};
        bool billboard {false};
        Vec3<float> billboard_pivot {0, 0, 0};
        Mtxf object_inverse {mtxfIdentity()};
    };

    GBI::ProjectionContext area_projection {};
    for (const auto &dl : dls) {
        if (dl.projection.valid) {
            area_projection = dl.projection;
            break;
        }
    }

    std::vector<InlineCall> inline_calls;
    inline_calls.reserve(dls.size());
    for (const auto &dl : dls) {
        inline_calls.push_back(InlineCall {
            -1, dl.dl, dl.layer, dl.transform, dl.fixed_transform, dl.projection,
            false, false, {}, {mtxfIdentity()}});
    }

    result_.inline_object_models.clear();
    result_.inline_object_models.resize(objects.size());
    for (size_t object_index = 0; object_index < objects.size(); object_index++) {
        const auto &obj = objects[object_index];
        // GRAPH_RENDER_BILLBOARD uses the current extraction camera basis for the fixed
        // vertex pass, then the Godot parent follows the live camera at render time.
        // Invisible/deactivated objects do not submit display lists in the game either.
        if (obj.model_id <= 0 || obj.invisible || obj.deactivated || !obj.active) {
            continue;
        }
        if (obj.model_id >= static_cast<int16_t>(level_.loaded_graph_node.size())) {
            continue;
        }
        const GraphNode *node = level_.loaded_graph_node[obj.model_id].get();
        if (node == nullptr) {
            continue;
        }

        std::optional<ObjectExtract::Frame0Animator> frame0;
        if (obj.raw[ObjectExtract::F::Animations] != 0) {
            const int16_t anim_index = (obj.animate_index >= 0) ? obj.animate_index : 0;
            frame0.emplace(seg_table_, obj.addr(ObjectExtract::F::Animations), anim_index);
        }
        const Mtxf object_transform = obj.billboard
            ? objectBillboardTransform(obj, area_projection)
            : objectRenderTransform(obj);
        const Fast3D::FixedMatrix fixed_object_transform =
            Fast3D::fromFloatMatrix(object_transform);
        const Mtxf object_inverse = mtxfInverse(object_transform);
        for (const auto &dlt : ObjectExtract::collectDisplayLists(*node, frame0 ? &*frame0 : nullptr)) {
            if (dlt.dl.seg < 0 || dlt.dl.seg > 31 ||
                (dlt.dl.seg == 0 && dlt.dl.offset == 0)) {
                continue;
            }
            // collectDisplayLists uses row-vector matrices: model-local geometry is
            // transformed by dlt.transform first, then by the object matrix.
            inline_calls.push_back(InlineCall {
                static_cast<int>(object_index), dlt.dl, dlt.layer,
                mtxfMul(dlt.transform, object_transform),
                Fast3D::matrixMultiply(dlt.fixed_transform, fixed_object_transform),
                area_projection, obj.billboard, dlt.is_billboard, dlt.billboard_pivot,
                object_inverse});
        }
    }

    std::stable_sort(inline_calls.begin(), inline_calls.end(),
                     [](const InlineCall &a, const InlineCall &b) {
                         return a.layer < b.layer;
                     });

    GBI::Mesh &merged = result_.mesh;
    GBI::DLInterpreter inline_interpreter(seg_table_, log_);
    bool reset_state = true;
    for (const auto &call : inline_calls) {
        GBI::Mesh &decoded = inline_interpreter.run(
            call.dl, reset_state, call.layer, call.transform, call.fixed_transform,
            call.projection);
        reset_state = false;
        if (call.object_index < 0) {
            ObjectExtract::ObjectModelDecoder::mergeMesh(merged, std::move(decoded));
        } else {
            if (call.object_billboard || call.billboard) {
                for (auto &vertex : decoded.vertices) {
                    const Vec3<float> world_position {
                        vertex.position[0], vertex.position[1], vertex.position[2]};
                    const Vec3<float> local_position =
                        transformPoint(call.object_inverse, world_position);
                    vertex.position[0] = local_position.x;
                    vertex.position[1] = local_position.y;
                    vertex.position[2] = local_position.z;
                    const Vec3<float> local_normal = transformNormal(
                        call.object_inverse, {vertex.normal[0], vertex.normal[1], vertex.normal[2]});
                    vertex.normal[0] = local_normal.x;
                    vertex.normal[1] = local_normal.y;
                    vertex.normal[2] = local_normal.z;
                    if (call.billboard) {
                        vertex.position[0] -= call.billboard_pivot.x;
                        vertex.position[1] -= call.billboard_pivot.y;
                        vertex.position[2] -= call.billboard_pivot.z;
                    }
                }
            }
            if (!call.billboard) {
                ObjectExtract::ObjectModelDecoder::mergeMesh(
                    result_.inline_object_models[static_cast<size_t>(call.object_index)].mesh,
                    std::move(decoded));
                continue;
            }
            auto &parts = result_.inline_object_models[static_cast<size_t>(call.object_index)]
                              .billboard_parts;
            auto part = std::find_if(parts.begin(), parts.end(), [&](const auto &candidate) {
                return candidate.pivot.x == call.billboard_pivot.x &&
                       candidate.pivot.y == call.billboard_pivot.y &&
                       candidate.pivot.z == call.billboard_pivot.z;
            });
            if (part == parts.end()) {
                parts.push_back(ObjectExtract::BillboardPart {call.billboard_pivot, {}, {}});
                part = parts.end() - 1;
            }
            ObjectExtract::ObjectModelDecoder::mergeMesh(part->mesh, std::move(decoded));
        }
    }

    struct TextureCacheEntry {
        GBI::Material material;
        uint32_t image {0};
        uint32_t tlut {0};
        GBI::Texture texture;
        bool ok {false};
    };
    std::vector<TextureCacheEntry> texture_cache;
    auto decodeTextures = [&](const GBI::Mesh &mesh, std::vector<GBI::Texture> &textures) {
        textures.resize(mesh.materials.size());
        GBI::TextureDecoder texture_decoder(seg_table_);
        for (size_t m = 0; m < mesh.materials.size(); m++) {
            if (!mesh.materials[m].textured || mesh.material_images[m] == 0) {
                continue;
            }
            const auto cached = std::find_if(
                texture_cache.begin(), texture_cache.end(), [&](const TextureCacheEntry &entry) {
                    return entry.image == mesh.material_images[m] &&
                           entry.tlut == mesh.material_tlut[m] &&
                           entry.material == mesh.materials[m];
                });
            if (cached != texture_cache.end()) {
                if (cached->ok) {
                    textures[m] = cached->texture;
                }
                continue;
            }
            texture_cache.push_back(TextureCacheEntry {
                mesh.materials[m], mesh.material_images[m], mesh.material_tlut[m], {}, false});
            TextureCacheEntry &entry = texture_cache.back();
            if (texture_decoder.run(mesh.materials[m], segAddress(mesh.material_images[m]),
                                    mesh.material_tlut[m])) {
                entry.texture = texture_decoder.takeTexture();
                entry.ok = true;
                textures[m] = entry.texture;
            } else {
                log_.add("texture",
                         "material " + std::to_string(m) + " (image 0x" + [&]() {
                             char buf[16];
                             std::snprintf(buf, sizeof(buf), "%08X",
                                           mesh.material_images[m]);
                             return std::string(buf);
                         }() +
                             ") could not be decoded to RGBA8; it renders as a flat color: " +
                             texture_decoder.error());
            }
        }
    };
    decodeTextures(merged, result_.textures);
    for (auto &model : result_.inline_object_models) {
        if (!model.mesh.indices.empty()) {
            decodeTextures(model.mesh, model.textures);
        }
        for (auto &part : model.billboard_parts) {
            if (!part.mesh.indices.empty()) {
                decodeTextures(part.mesh, part.textures);
            }
        }
    }

    // 对象模型：每个唯一 model id 只解码一次（复用同模型的所有对象实例）。
    // model_id 0（MODEL_NONE，如传送点）没有几何，跳过。有动画行为的对象
    // （LOAD_ANIMATIONS + ANIMATE）在烘焙时叠加动画 frame-0 值（如门）。动画是
    // 原生代码选的（LOAD_ANIMATIONS 但无 ANIMATE 命令，如 goomba 由 goomba_update
    // 固定用索引 0）：静态导出默认取第一个动画的 frame-0（通常是静止姿态），
    // 否则部件停在裸 geo 锚点上（goomba 会陷入地面且朝向错误，见 docs/Quirks.md）。
    std::map<int16_t, ObjectExtract::ObjectModel> object_models;
    for (const auto &obj : objects) {
        if (obj.model_id <= 0 || object_models.contains(obj.model_id)) {
            continue;
        }
        std::optional<ObjectExtract::Frame0Animator> frame0;
        if (obj.raw[ObjectExtract::F::Animations] != 0) {
            const int16_t anim_index = (obj.animate_index >= 0) ? obj.animate_index : 0;
            frame0.emplace(seg_table_, obj.addr(ObjectExtract::F::Animations), anim_index);
        }
        ObjectExtract::ObjectModelDecoder model_decoder(seg_table_, log_);
        const GraphNode *node = level_.loaded_graph_node[obj.model_id].get();
        if (node == nullptr) {
            continue; // geo 未加载（越界/无效地址被跳过）
        }
        model_decoder.runModel(node, frame0 ? &*frame0 : nullptr);
        ObjectExtract::ObjectModel model = model_decoder.takeModel();
        if (model.mesh.indices.empty()) {
            continue;
        }
        object_models[obj.model_id] = std::move(model);
    }
    result_.object_models = std::move(object_models);

    result_.objects = std::move(objects);

    // 各对象的碰撞数据（行为 LOAD_COLLISION_DATA；本地空间，不做对象变换）。
    // 行为已在上面的统一走查中设置 obj.collision_data。
    result_.object_collisions.resize(result_.objects.size());
    Collision::CollisionDecoder collision_decoder(seg_table_);
    for (size_t i = 0; i < result_.objects.size(); i++) {
        const auto &obj = result_.objects[i];
        if (obj.deactivated || obj.collision_data.isNull()) {
            continue;
        }
        collision_decoder.runObject(obj.collision_data);
        result_.object_collisions[i] = collision_decoder.takeData();
        if (!result_.object_collisions[i].ok) {
            log_.add("collision",
                     "the collision model of object " + std::to_string(i) +
                         " (behavior 0x" + [&]() {
                             char buf[16];
                             std::snprintf(buf, sizeof(buf), "%08X",
                                           (uint32_t(obj.behavior.seg) << 24) |
                                               (obj.behavior.offset & 0xFFFFFF));
                             return std::string(buf);
                         }() + ", collision data at segment " +
                         std::to_string(obj.collision_data.seg) + " offset 0x" + [&]() {
                             char buf[16];
                             std::snprintf(buf, sizeof(buf), "%06X", obj.collision_data.offset);
                             return std::string(buf);
                         }() + ") could not be decoded: " + result_.object_collisions[i].error);
        }
    }
    result_.mario_start_pos = { static_cast<float>(level_.mario_start_pos.x),
                                static_cast<float>(level_.mario_start_pos.y),
                                static_cast<float>(level_.mario_start_pos.z) };
    result_.mario_start_angle_y = level_.mario_start_angle_y;
    result_.level_name = readCourseName(level_num);

    // 关卡脚本记录的区域/关卡级数据（镜像 Level/Area）
    const Area &sel = level_.areas[area_index];
    result_.warp_nodes = sel.warp_nodes;
    result_.painting_warp_nodes = sel.painting_warp_nodes;
    result_.instant_warps = sel.instant_warps;
    result_.whirlpools = sel.whirlpools;
    result_.dialog[0] = sel.dialog[0];
    result_.dialog[1] = sel.dialog[1];
    result_.music_param = sel.music_param;
    result_.music_param2 = sel.music_param2;
    for (int i = 0; i < 5; i++) {
        result_.unused_area_28[i] = sel.unused_area_28[i];
    }
    if (sel.camera && std::holds_alternative<GraphNodeCamera>(sel.camera->data)) {
        result_.camera = std::get<GraphNodeCamera>(sel.camera->data);
    }
    result_.mario_model_id = level_.mario_model_id;
    result_.mario_behavior_arg = level_.mario_behavior_arg;
    result_.mario_behavior_script = level_.mario_behavior_script;
    result_.transition = level_.transition;
    result_.ok = true;
}

Result LevelExtractor::extract(ROM &rom, int level_num, int area_index) {
    LevelExtractor extractor(rom);
    extractor.run(level_num, area_index);
    return extractor.takeResult();
}

std::vector<int> LevelExtractor::listAreas(ROM &rom, int level_num) {
    LevelExtractor extractor(rom);
    extractor.runScript(level_num);
    return extractor.result().areas;
}

std::string LevelExtractor::extractLevelName(ROM &rom, int level_num) {
    if (!rom.is_loaded) {
        return {};
    }

    LevelExtractor extractor(rom);
    extractor.loadCourseNameSegment();
    try {
        return extractor.readCourseName(level_num);
    } catch (const std::out_of_range &) {
        return {};
    }
}

std::map<int, std::string> LevelExtractor::loadAllLevelNames(ROM &rom) {
    std::map<int, std::string> names;
    if (!rom.is_loaded) return names;

    LevelExtractor extractor(rom);
    extractor.loadCourseNameSegment();

    constexpr int kLevels[] = {
        4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
        20, 21, 22, 23, 24, 26, 27, 28, 29, 30, 31, 33, 34, 36
    };
    for (int lv : kLevels) {
        std::string name = extractor.readCourseName(lv);
        if (!name.empty()) {
            names[lv] = name;
        }
    }
    return names;
}

Result extract(ROM &rom, int level_num, int area_index) {
    return LevelExtractor::extract(rom, level_num, area_index);
}

std::vector<int> listAreas(ROM &rom, int level_num) {
    return LevelExtractor::listAreas(rom, level_num);
}

std::string extractLevelName(ROM &rom, int level_num) {
    return LevelExtractor::extractLevelName(rom, level_num);
}

std::map<int, std::string> loadAllLevelNames(ROM &rom) {
    return LevelExtractor::loadAllLevelNames(rom);
}

bool loadMainSegment(SegmentTable &seg_table, const std::vector<uint8_t> &rom) {
    return LevelExtractor::loadMainSegment(seg_table, rom);
}

} // namespace LevelExtract
