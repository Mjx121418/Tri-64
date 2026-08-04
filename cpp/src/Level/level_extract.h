#ifndef LEVEL_EXTRACT_H
#define LEVEL_EXTRACT_H

#include "Level/dl_interpreter.h"
#include "Level/texture.h"
#include "Memory/segment.h"
#include "ROM.h"
#include "Scripts/level_script.h"
#include <cstdint>
#include <string>
#include <vector>

// 在引擎内直接提取关卡几何（无需先导出 OBJ）。
//
// 流程与 tests/LevelScript 的导出一致：定位脚本段 → 运行关卡脚本（按
// LevelNum 分发到目标关卡）→ 遍历该关卡的 Area 场景图收集 DL → DL 解释器
// 生成网格 → 跨 DL 合并（复用 GBI::Mesh/Material，按内容 + 纹理源图像去重）
// → 解码每材质纹理为 RGBA8（复用 GBI::Texture）。
namespace LevelExtract {

struct Result {
    bool ok {false};
    std::string error;
    GBI::Mesh mesh;                      // 合并后的网格（含材质表与纹理源图像）
    std::vector<GBI::Texture> textures;  // 与 mesh.materials 并行：解码纹理
    std::vector<ObjectSpawnInfo> objects; // 对象出生点（未来的对象列表）
    std::vector<int> areas;              // 该关卡所有有效区域索引（有 root_node）
};

// 提取 rom 中 level_num（LevelNum，如 BOB=9）的 area_index 号区域。
// rom 必须已通过 ROM::load 加载，数据在调用期间保持有效。
Result extract(ROM &rom, int level_num, int area_index);

// 返回 level_num 关卡的所有有效区域索引（供 UI 的 Area 下拉列表使用）。
std::vector<int> listAreas(ROM &rom, int level_num);

} // namespace LevelExtract

#endif /* LEVEL_EXTRACT_H */
