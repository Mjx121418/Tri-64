#ifndef OBJECT_H
#define OBJECT_H

#include "Level/area.h"
#include "Level/dl_interpreter.h"
#include "Level/texture.h"
#include "Memory/segment.h"
#include "Scripts/Collision.h"
#include <cstdint>
#include <map>
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

// 从 geo 布局图节点解码对象模型：收集 (DL, 变换) 对，跳过无效 DL 地址
// （0x00000000 / 段越界，见 docs/engine-notes.md），把图节点变换（缩放/
// 旋转/平移）烘焙进顶点，合并为一个网格并解码每材质纹理。node 为 null 或
// 没有可解码 DL 时返回空模型。
ObjectModel decodeModel(const SegmentTable &seg_table, const GraphNode *node);

// 把 MACRO_OBJECTS 原始条目展开成对象出生点：preset → 模型 id 解析，
// 跳过 MODEL_NONE / 生成器（preset 表来自 decomp 的 macro_presets.inc.c）。
void expandMacroObjects(const std::vector<MacroObjectSpawnInfo> &macro_objects,
                        int8_t area_index, std::vector<ObjectSpawnInfo> &out);

// 把碰撞数据里的特殊对象（TERRAIN_LOAD_OBJECTS）展开成对象出生点：
// preset → 模型 id 解析（special_presets.inc.c），跳过 MODEL_NONE。
void expandSpecialObjects(const std::vector<Collision::SpecialObject> &special_objects,
                          int8_t area_index, std::vector<ObjectSpawnInfo> &out);

} // namespace ObjectExtract

#endif /* OBJECT_H */
