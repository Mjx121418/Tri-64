#ifndef GODOT_BRIDGE_H
#define GODOT_BRIDGE_H

#include <godot_cpp/classes/ref_counted.hpp>

#include "Level/level_extract.h"
#include "ROM.h"

using namespace godot;

class GodotBridge : public RefCounted {
    GDCLASS(GodotBridge, RefCounted);

private:
    ROM rom;
    LevelExtract::Result result_;

protected:
    static void _bind_methods();

public:
    void loadROM(String path);
    bool ROMLoaded();
    // 已加载 ROM 中 level_num 关卡的有效区域索引（供 Area 下拉列表）。
    PackedInt32Array getLevelAreas(int level_num);
    // 提取已加载 ROM 中 level_num（LevelNum，如 BOB=9）的 area_index 号区域。
    bool extractLevel(int level_num, int area_index);
    // 提取结果（复用 GBI 的 Mesh/Material/Texture 结构）：
    //   getMeshes()    每个材质一个网格：{ vertices: PackedVector3Array,
    //                  normals, uvs: PackedVector2Array,
    //                  indices: PackedInt32Array, material: int }
    //   getMaterials() 与 getMeshes 的 material 一一对应：
    //                  { textured: bool, color: Color, tex_width, tex_height:
    //                  int, tex_pixels: PackedByteArray(RGBA8) }
    //   getObjects()   对象出生点：{ pos: Vector3, angle: Vector3,
    //                  behavior_arg: int, behavior: int }
    Array getMeshes();
    Array getMaterials();
    Array getObjects();
    // 当前提取的关卡名称（从 ROM 段2的 seg2_course_name_table 提取）。
    // 必须在 extractLevel 之后调用，否则返回空字符串。
    String getLevelName();
    // 查询指定关卡的名称（运行关卡脚本提取 cur_course_num，查 seg2）。
    String getLevelNameFor(int level_num);
    // 一次性从 segment 2 加载所有已知关卡名称，返回 { level_num: name } 字典。
    Dictionary getAllLevelNames();
    GodotBridge();
};

#endif /* GODOT_BRIDGE_H */
