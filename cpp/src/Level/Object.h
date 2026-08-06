#ifndef OBJECT_H
#define OBJECT_H

#include "Level/area.h"
#include "Level/dl_interpreter.h"
#include "Level/texture.h"
#include "Memory/segment.h"
#include "Scripts/Collision.h"
#include "Scripts/preset_tables.h"
#include <cstdint>
#include <optional>
#include <vector>

// 对象相关逻辑（对象模型解码 + MACRO_OBJECTS/特殊对象展开），与关卡几何提取
// （LevelExtract）分离，避免 level_extract.cpp 过大。
namespace ObjectExtract {

// 单个对象模型：由 OBJECT 命令的 model id 对应的 geo 布局解码而来。
// 每个唯一 model id 只解码一次，所有引用它的对象实例复用这份网格。
struct ObjectModel {
    GBI::Mesh mesh;                      // 合并后的模型网格（含材质表与纹理源图像）
    std::vector<GBI::Texture> textures;  // 与 mesh.materials 并行：解码纹理
};

// 把 src 追加进 merged：顶点/索引加 base 偏移，材质按内容 + 纹理源图像去重。
void mergeMesh(GBI::Mesh &merged, GBI::Mesh &&src);

// 动画 frame-0：每个 animated part（GEO_ANIMATED_PART）在出生帧的平移/旋转增量。
struct Frame0Part {
    Vec3<float> translation {0, 0, 0};
    Vec3<int16_t> rotation {0, 0, 0};
};

// 逐 AP 计算动画 frame-0 值，镜像 rendering_graph_node.c 的 geo_process_animated_part
// 属性游标：第一个 AP 按动画 flags 读取平移，之后只读旋转；平移乘
// gCurrAnimTranslationMultiplier（animYTransDivisor == 0 时为 1.0）。next() 按
// geo 遍历顺序返回下一个 AP 的增量；动画缺失/越界返回 nullopt。
class Frame0Animator {
public:
    // animations: LOAD_ANIMATIONS 的目标（Animation* 数组，段地址）；
    // animate_index: ANIMATE 的索引（出生即播放的动画）。
    Frame0Animator(const SegmentTable &seg_table, SegmentedAddress animations, int16_t animate_index);
    bool ok() const { return ok_; }
    std::optional<Frame0Part> next();

private:
    const SegmentTable &seg_table_;
    bool ok_ {false};
    bool rotation_mode_ {false};
    uint32_t translate_type_ {0};
    float multiplier_ {1.0f};
    SegmentedAddress index_addr_ {};   // animindex（u16 对 (start,count)）
    SegmentedAddress values_addr_ {};  // animvalue（s16）
    uint32_t attr_offset_ {0};         // 当前属性在 animindex 内的字节偏移
    float readValue();                 // values[retrieve_animation_index(0)]
    void skipAttribute();
};

// 从 geo 布局图节点解码对象模型：收集 (DL, 变换) 对，跳过无效 DL 地址
// （0x00000000 / 段越界，见 docs/engine-notes.md），把图节点变换（缩放/
// 旋转/平移）+ 可选 frame-0 动画烘焙进顶点，合并为一个网格并解码每材质纹理。
// node 为 null 或没有可解码 DL 时返回空模型。
ObjectModel decodeModel(const SegmentTable &seg_table, const GraphNode *node,
                        Frame0Animator *frame0 = nullptr);

// 把 MACRO_OBJECTS 原始条目展开成对象出生点：preset → 模型/行为解析
// （presets 来自 PresetTables::parseMacroPresets），跳过 MODEL_NONE / 生成器。
void expandMacroObjects(const std::vector<MacroObjectSpawnInfo> &macro_objects,
                        int8_t area_index, const std::vector<PresetTables::MacroPreset> &presets,
                        std::vector<ObjectSpawnInfo> &out);

// 把碰撞数据里的特殊对象（TERRAIN_LOAD_OBJECTS）展开成对象出生点：
// preset → 模型/行为解析（presets 来自 PresetTables::parseSpecialPresets），
// 跳过 MODEL_NONE。
void expandSpecialObjects(const std::vector<Collision::SpecialObject> &special_objects,
                          int8_t area_index, const std::vector<PresetTables::SpecialPreset> &presets,
                          std::vector<ObjectSpawnInfo> &out);

} // namespace ObjectExtract

#endif /* OBJECT_H */
