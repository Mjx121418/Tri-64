#ifndef LEVEL_EXTRACT_H
#define LEVEL_EXTRACT_H

#include "Level/Object.h"
#include "Level/dl_interpreter.h"
#include "Level/texture.h"
#include "Memory/segment.h"
#include "ROM.h"
#include "Scripts/level_script.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// 在引擎内直接提取关卡几何（无需先导出 OBJ）。
//
// 流程与 tests/LevelScript 的导出一致：定位脚本段 → 运行关卡脚本（按
// LevelNum 分发到目标关卡）→ 遍历该关卡的 Area 场景图收集 DL → DL 解释器
// 生成网格 → 跨 DL 合并（复用 GBI::Mesh/Material，按内容 + 纹理源图像去重）
// → 解码每材质纹理为 RGBA8（复用 GBI::Texture）。
// 对象模型解码与 MACRO_OBJECTS 展开在 ObjectExtract（Level/Object.*）完成。
namespace LevelExtract {

struct Result {
    bool ok {false};
    std::string error;
    GBI::Mesh mesh;                      // 合并后的网格（含材质表与纹理源图像）
    std::vector<GBI::Texture> textures;  // 与 mesh.materials 并行：解码纹理
    std::vector<ObjectSpawnInfo> objects; // 对象出生点（未来的对象列表）
    std::map<int16_t, ObjectExtract::ObjectModel> object_models; // 对象模型缓存（按 model id 去重）
    std::vector<int> areas;              // 该关卡所有有效区域索引（有 root_node）
    std::string level_name;             // 从 ROM 段2提取的关卡名称（可为空）
    Vec3<float> mario_start_pos {};     // Mario 的初始位置（关卡脚本 cmdSetMarioStartPos）
    float mario_start_angle_y {0};      // Mario 的初始朝向（Y 轴旋转角度，弧度）
};

// 提取 rom 中 level_num（LevelNum，如 BOB=9）的 area_index 号区域。
// rom 必须已通过 ROM::load 加载，数据在调用期间保持有效。
Result extract(ROM &rom, int level_num, int area_index);

// 返回 level_num 关卡的所有有效区域索引（供 UI 的 Area 下拉列表使用）。
std::vector<int> listAreas(ROM &rom, int level_num);

// 仅提取关卡名称（从 seg2_course_name_table），不进行完整的几何提取。
std::string extractLevelName(ROM &rom, int level_num);

// 一次性加载 segment 2 并提取所有已知关卡的名称，无需运行关卡脚本。
// 返回 LevelNum → 名称 的映射，提取失败的关卡不在映射中。
std::map<int, std::string> loadAllLevelNames(ROM &rom);

} // namespace LevelExtract

#endif /* LEVEL_EXTRACT_H */
