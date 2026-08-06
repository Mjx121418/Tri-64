#ifndef PRESET_TABLES_H
#define PRESET_TABLES_H

#include "Memory/segment.h"
#include <cstdint>
#include <vector>

// 宏对象/特殊对象的 preset 表（sMacroObjectPresets / sSpecialObjectPresets），
// 位于游戏主段（段 0x00）。主段先用 LevelExtract::loadMainSegment 载入 seg 0。
//
// 表在段 0 里的位置按 decomp 的模型列指纹定位（macro_presets.inc.c /
// special_presets.inc.c + model_ids.h 生成的锚序列），对 binary hack 友好：
// hack 移动主段时锚照样找得到。表中读出的 model/param/behavior 才是权威数据
// （因此不再需要 Object.cpp 里硬编码的模型表）。
//
// 地址约定：段 0 按主段内偏移线性载入。游戏里的 seg-0 地址 = 主段内偏移 +
// 0x200000（主段在 RDRAM 0x80200000，seg-0 基址 0x80000000，见
// docs/engine-notes.md）。
namespace PresetTables {

struct MacroPreset {
    int16_t model {0};
    int16_t param {0};
    SegmentedAddress behavior {};
};

struct SpecialPreset {
    int16_t model {0};
    uint8_t type {0};
    int16_t def_param {0};
    SegmentedAddress behavior {};
};

// 解析表并返回按 preset id 索引的条目；主段未载入或定位失败返回空。
std::vector<MacroPreset> parseMacroPresets(const SegmentTable &seg_table);
std::vector<SpecialPreset> parseSpecialPresets(const SegmentTable &seg_table);

} // namespace PresetTables

#endif /* PRESET_TABLES_H */
