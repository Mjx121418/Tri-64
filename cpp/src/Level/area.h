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
    int8_t area_index;
    int8_t active_area_index;
    uint32_t behavior_arg;
    SegmentedAddress behavior_script;
    // GraphNode;
};

struct Area {
    int8_t index;
    int8_t flag; // Is this the active area?
    uint16_t terrianType;
    std::unique_ptr<GraphNode> root_node;
    // Collision
    // macroObjects
    std::vector<ObjectSpawnInfo> object_infos;
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
