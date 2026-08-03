#include "Level/level_extract.h"

#include <algorithm>
#include <array>
#include <cstdio>
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
            seg_table.loadMIO0Segment(seg, rom_start, rom_end);
        } else if (cmd[0] == 0x17) { // LOAD_RAW
            seg_table.loadSegment(seg, rom_start, rom_end);
        }
    }
}

void collectDisplayLists(const GraphNode &node, std::vector<SegmentedAddress> &out) {
    if (std::holds_alternative<GraphNodeDisplayList>(node.data)) {
        out.push_back(std::get<GraphNodeDisplayList>(node.data).display_list);
    }
    for (const auto &child : node.children) {
        collectDisplayLists(*child, out);
    }
}

} // namespace

Result extract(ROM &rom, int level_num, int area_index) {
    Result result;
    if (!rom.is_loaded) {
        result.error = "ROM not loaded";
        return result;
    }

    const size_t scripts_start = findScriptsStart(rom.data);
    const size_t table_pos = findPattern(rom.data, kLevelTablePattern, scripts_start);
    if (scripts_start == rom.data.size() || table_pos == rom.data.size()) {
        result.error = "could not locate the level scripts segment";
        return result;
    }
    const size_t table_offset = table_pos - scripts_start;

    SegmentTable seg_table;
    seg_table.rom_span = std::span(rom.data);
    seg_table.loadSegment(0x15, static_cast<uint32_t>(scripts_start),
                          static_cast<uint32_t>(std::min(scripts_start + 0x8000, rom.data.size())));
    loadCommonSegments(seg_table, rom.data, scripts_start);

    Level level;
    LevelScriptVM vm(seg_table, level);
    vm.setLevelNum(level_num);
    vm.execute(SegmentedAddress { 0x15, static_cast<uint32_t>(table_offset) });

    if (area_index < 0 || area_index >= 8 || !level.areas[area_index].root_node) {
        result.error = "area " + std::to_string(area_index) + " not found";
        return result;
    }
    const Area &area = level.areas[area_index];

    // 收集该区域的所有 DL，合并进一个 GBI::Mesh（去重键与 OBJ 导出一致：
    // 材质内容 + 解析出的纹理源图像）。
    std::vector<SegmentedAddress> dls;
    collectDisplayLists(*area.root_node, dls);

    GBI::Mesh &merged = result.mesh;
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
        merged.material_ids.reserve(merged.material_ids.size() + tri_count);
        for (size_t t = 0; t < tri_count; t++) {
            const uint32_t mi = mesh.material_ids[t];
            const GBI::Material &m = mesh.materials[mi];
            const uint32_t img = mesh.material_images[mi];
            uint32_t mid = 0;
            for (; mid < merged.materials.size(); mid++) {
                if (merged.materials[mid] == m && merged.material_images[mid] == img) {
                    break;
                }
            }
            if (mid == merged.materials.size()) {
                merged.materials.push_back(m);
                merged.material_images.push_back(img);
            }
            merged.material_ids.push_back(mid);
        }
    }

    // 每材质解码一个 RGBA8 纹理（与 merged.materials 并行；解码失败留空）
    result.textures.resize(merged.materials.size());
    for (size_t m = 0; m < merged.materials.size(); m++) {
        if (merged.materials[m].textured && merged.material_images[m] != 0) {
            auto tex = GBI::decodeTexture(merged.materials[m],
                                          segAddress(merged.material_images[m]), seg_table);
            if (tex) {
                result.textures[m] = std::move(*tex);
            }
        }
    }

    result.objects = area.object_infos;
    result.ok = true;
    return result;
}

} // namespace LevelExtract
