#ifndef AREA_H
#define AREA_H

#include "Level/graph_node.h"
#include "Math/math.h"
#include "Memory/segment.h"
#include <cstdint>
#include <vector>

struct WarpNode {
    uint8_t id;
    uint8_t dest_level;
    uint8_t dest_area;
    uint8_t dest_node;
};

struct ObjectSpawnInfo {
    Vec3<int16_t> start_pos;
    Vec3<int16_t> start_angle;
    int16_t model_id {0}; // OBJECT 命令第 3 字节：模型 id（0 = MODEL_NONE）
    int8_t area_index;
    int8_t active_area_index;
    uint32_t behavior_arg;
    SegmentedAddress behavior_script;
    // GraphNode;
};

// MACRO_OBJECTS 命令（0x39）里的原始条目：preset 索引 + 出生数据。
// preset → 模型 id 的解析在 LevelExtract 完成（LevelScriptVM 不关心模型）。
struct MacroObjectSpawnInfo {
    int16_t preset {0};
    int16_t yaw {0};       // SM64 角度单位（0x8000 = 180°）
    Vec3<int16_t> pos {};
    int16_t bhv_param {0};
};

struct Area {
    int8_t index;
    int8_t flag; // Is this the active area?
    uint16_t terrianType;
    std::unique_ptr<GraphNode> root_node;
    // Collision
    SegmentedAddress terrain_addr {}; // TERRAIN 命令（0x2E）的碰撞数据地址
    SegmentedAddress rooms_addr {};   // ROOMS 命令（0x2F）的房间列表地址
    // macroObjects
    std::vector<ObjectSpawnInfo> object_infos;
    std::vector<MacroObjectSpawnInfo> macro_objects; // MACRO_OBJECTS 原始条目
};

struct Level {
    std::array<Area, 8> areas;
    std::array<std::unique_ptr<GraphNode>, 0x100> loaded_graph_node;
    GraphNodeStart object_parent;
    Vec3<int16_t> mario_start_pos {};
    int16_t mario_start_angle_y {0};
    int8_t mario_start_area {0};
};

#endif /* AREA_H */
