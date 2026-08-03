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
    GodotBridge();
};

#endif /* GODOT_BRIDGE_H */
