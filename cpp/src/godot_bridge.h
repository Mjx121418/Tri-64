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
    //   getObjects()   对象出生点：{ pos: Vector3, angle: Vector3(弧度),
    //                  model: int, behavior_arg: int, behavior: int }
    //   getObjectModels() 对象模型（按 model id 去重，所有实例共享）：
    //                  每个 { model: int, meshes: [...] 同 getMeshes 格式,
    //                  materials: [...] 同 getMaterials 格式 }
    Array getMeshes();
    Array getMaterials();
    Array getObjects();
    Array getObjectModels();
    // 碰撞三角形（当前区域的静态碰撞）：{ vertices: PackedVector3Array,
    // normals, indices: PackedInt32Array }。平坦着色，每三角形 3 个顶点。
    Dictionary getCollisionTriangles();
    // 对象的碰撞三角形（行为 LOAD_COLLISION_DATA，本地空间，需配合
    // getObjects 的 pos/angle 变换到世界）：每个有碰撞数据的对象一个
    // { pos: Vector3, angle: Vector3(弧度), vertices, normals, classes,
    // indices }，顶点未做对象变换（与模型共用同一个 Godot 节点变换）。
    Array getObjectCollisions();
    // 本次提取产生的警告/被守卫的异常（越界数据、跳过的模型/几何、解码失败
    // 等），每个 { stage: String, message: String }。必须在 extractLevel 之后
    // 调用；没有警告时返回空数组。
    Array getWarnings();
    // 当前提取的关卡名称（从 ROM 段2的 seg2_course_name_table 提取）。
    // 必须在 extractLevel 之后调用，否则返回空字符串。
    String getLevelName();
    // 查询指定关卡的名称（直接查 seg2_course_name_table，不运行关卡脚本）。
    String getLevelNameFor(int level_num);
    // 一次性从 segment 2 加载所有已知关卡名称，返回 { level_num: name } 字典。
    Dictionary getAllLevelNames();
    // Mario 的初始位置与朝向，返回 { pos: Vector3, angle_y: float }。
    Dictionary getMarioStartPos();
    // 当前区域的背景（geo 0x19 节点）：{ background: int（天空盒 id 或
    // RGBA5551 填充色）, func: int, is_skybox: bool, fill_color: Color,
    // skybox_width/height: int, skybox_pixels: PackedByteArray(RGBA8) }。
    // is_skybox 时 skybox_* 是 10×8 图块的贴图集（320×256）；否则只有
    // fill_color（geo_process_background 的纯色填充）。
    Dictionary getBackground();
    GodotBridge();
};

#endif /* GODOT_BRIDGE_H */
