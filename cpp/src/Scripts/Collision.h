#ifndef COLLISION_H
#define COLLISION_H

#include "Math/math.h"
#include "Memory/segment.h"
#include <cstdint>
#include <string>
#include <vector>

// SM64 碰撞（terrain）数据解码器。数据是 s16 大端命令流，位于关卡段内
// （地址来自 TERRAIN 关卡命令 0x2E）。解码逻辑对照 decomp 的
// src/engine/surface_load.c（load_area_terrain / load_static_surfaces /
// read_vertex_data / load_environmental_regions）与
// src/game/macro_special_objects.c（spawn_special_objects）。
namespace Collision {

// 碰撞顶点 = 位置（与 Math 的 Vec3<int16_t> 复用同一类型）。
using Vertex = Vec3<int16_t>;

struct Surface {
    uint16_t type;       // SURFACE_*（命令值本身）
    uint32_t v1, v2, v3; // 顶点索引（vertices 数组，每顶点 3 个 s16）
    int16_t force;       // 力/参数（仅 surface_has_force 的类型有，否则 0）
    int8_t room;         // ROOMS 列表按序分配的房间号
    bool has_room {false};
};

// 特殊对象（TERRAIN_LOAD_OBJECTS）。当前只解析不渲染：preset + 出生数据，
// 额外参数数量由 preset 表的 SPTYPE 决定。
struct SpecialObject {
    uint8_t preset;
    int16_t x, y, z;
    int16_t yaw;
    int16_t param;
};

struct WaterBox {
    int16_t id, x1, z1, x2, z2, y;
};

struct Data {
    bool ok {false};
    std::string error;
    std::vector<Vertex> vertices;
    std::vector<Surface> surfaces;
    std::vector<SpecialObject> special_objects;
    std::vector<WaterBox> water_boxes;
};

// 解析后的碰撞三角形网格（平坦着色：每三角形 3 个顶点，法线 = 面法线）。
// 顶点已解析为世界坐标（碰撞数据本身的坐标空间）。
struct TriangleMesh {
    std::vector<Vec3<float>> positions;
    std::vector<Vec3<float>> normals;
    std::vector<uint32_t> indices;
};

// 把表面（顶点索引）解析成三角形网格；越界/退化三角形跳过。
TriangleMesh buildTriangleMesh(const Data &data);

// 碰撞（terrain）数据解码器（镜像 LevelScriptVM 的结构）：构造时绑定段表，
// run(terrain[, rooms]) 重置并解码，结果经 data() 取得。解码失败时
// data().ok = false、data().error 给出原因。
class CollisionDecoder {
    const SegmentTable &seg_table_;
    Data data_;

public:
    explicit CollisionDecoder(const SegmentTable &seg_table) : seg_table_(seg_table) {}

    // 解码 terrain（TERRAIN 命令地址）处的碰撞数据；rooms 为可选的 ROOMS 命令
    // 地址（s8 列表，按静态表面顺序逐一分配）。
    void run(SegmentedAddress terrain, SegmentedAddress rooms = {});

    const Data &data() const { return data_; }
};

} // namespace Collision

#endif /* COLLISION_H */
