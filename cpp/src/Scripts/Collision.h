#ifndef COLLISION_H
#define COLLISION_H

#include "Math/math.h"
#include "Memory/segment.h"
#include <cstdint>
#include <optional>
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

// 表面标志 / 分类（decomp surface_terrains.h / surface_load.c）
enum SurfaceFlag : uint8_t {
    SURFACE_FLAG_DYNAMIC          = 1 << 0, // 对象碰撞（动态）
    SURFACE_FLAG_NO_CAM_COLLISION = 1 << 1, // 类型 0x76/0x77/0x78/0x7A
    SURFACE_FLAG_X_PROJECTION     = 1 << 3, // 墙且 |法线x| > 0.707
};

enum SurfaceClass : int8_t {
    SURFACE_CLASS_FLOOR    = 0, // 法线 y > 0.01
    SURFACE_CLASS_CEILING  = 1, // 法线 y < -0.01
    SURFACE_CLASS_WALL     = 2, // 其余
};

struct Surface {
    uint16_t type;       // SURFACE_*（命令值本身）
    uint32_t v1, v2, v3; // 顶点索引（vertices 数组，每顶点 3 个 s16）
    int16_t force;       // 力/参数（仅 surface_has_force 的类型有，否则 0）
    int8_t room;         // ROOMS 列表按序分配的房间号
    bool has_room {false};
    // 解码后由顶点计算派生（finalizeSurfaces）：标志 / 分类 / 垂直范围。
    uint8_t flags {0};         // SurfaceFlag
    int8_t classification {0}; // SurfaceClass
    int16_t lower_y {0};       // minY - 5
    int16_t upper_y {0};       // maxY + 5

    // 该表面的几何类型（floor / wall / ceiling），由解码后的分类决定。
    SurfaceClass surfaceClass() const {
        return static_cast<SurfaceClass>(classification);
    }
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
    // 每三角形 1 个值（indices.size()/3）：SurfaceClass，供渲染端选择颜色
    //（floor 蓝 / wall 绿 / ceiling 红）。类型而非颜色，颜色由渲染端决定。
    std::vector<uint8_t> classes;
    // 每三角形 1 个值：所属房间号（ROOMS 列表按表面顺序分配；0 = 无房间）。
    // 渲染端据此按房间开关显示/隐藏表面（对应游戏的 geo_switch_area 房间选择）。
    std::vector<uint8_t> rooms;
};

// 把表面（顶点索引）解析成三角形网格；越界/退化三角形跳过。
TriangleMesh buildTriangleMesh(const Data &data);

// 查询 (x, z) 脚下最高的 floor 高度，镜像 decomp 的 find_floor /
// find_floor_from_list（surface_collision.c，非相机路径）：只考虑 FLOOR 分类
// 表面、跳过 SURFACE_CAMERA_BOUNDARY (0x72) 与 ny==0，点在三角形内时用平面
// 插值求高，且查询点 y 必须 ≥ floor - 78。没有命中返回 nullopt。
std::optional<float> findFloorHeight(const Data &data, float x, float y, float z);

// 碰撞（terrain）数据解码器（镜像 LevelScriptVM 的结构）：构造时绑定段表，
// run(terrain[, rooms]) 重置并解码，结果经 data() 读取或 takeData() 移出。
// 解码失败时 data().ok = false、data().error 给出原因。
class CollisionDecoder {
    const SegmentTable &seg_table_;
    Data data_;

public:
    explicit CollisionDecoder(const SegmentTable &seg_table) : seg_table_(seg_table) {}

    // 解码 terrain（TERRAIN 命令地址）处的碰撞数据；rooms 为可选的 ROOMS 命令
    // 地址（s8 列表，按静态表面顺序逐一分配）。
    void run(SegmentedAddress terrain, SegmentedAddress rooms = {});

    // 解码对象的碰撞数据（行为命令 LOAD_COLLISION_DATA 的目标流，对象本地空间）。
    // 格式（surface_load.c load_object_collision_model）：[dummy][numVertices,
    // vtx*3][{surfaceType, count, (v1 v2 v3 [force])}...] 直到 TERRAIN_LOAD_CONTINUE
    // (0x41)。顶点未做对象矩阵变换（保留本地坐标；变换是运行时行为）。
    void runObject(SegmentedAddress addr);

    const Data &data() const { return data_; }
    // Move the decoded data out before the next run.
    Data takeData();
};

} // namespace Collision

#endif /* COLLISION_H */
