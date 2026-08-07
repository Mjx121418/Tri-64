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

// preset 表解码器（镜像 LevelScriptVM 的结构）：构造时绑定段表，run*() 重置并
// 解析主段里的表，结果经访问器取得。主段未载入或定位失败时对应表为空。
class PresetTableDecoder {
    const SegmentTable &seg_table_;
    std::vector<MacroPreset> macro_presets_;
    std::vector<SpecialPreset> special_presets_;

public:
    explicit PresetTableDecoder(const SegmentTable &seg_table) : seg_table_(seg_table) {}

    // 解析 sMacroObjectPresets，结果按 preset id 索引（顺序排列）。
    void runMacro();
    // 解析 sSpecialObjectPresets，结果按存储的 presetID 填入（非连续）。
    void runSpecial();

    const std::vector<MacroPreset> &macroPresets() const { return macro_presets_; }
    const std::vector<SpecialPreset> &specialPresets() const { return special_presets_; }
};

} // namespace PresetTables

#endif /* PRESET_TABLES_H */
