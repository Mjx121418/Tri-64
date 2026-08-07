#ifndef AREA_H
#define AREA_H

#include "Level/graph_node.h"
#include "Math/math.h"
#include "Memory/segment.h"
#include <array>
#include <cstdint>
#include <vector>

struct WarpNode {
    uint8_t id {0};
    uint8_t dest_level {0};
    uint8_t dest_area {0};
    uint8_t dest_node {0};
};

// 0x28 INSTANT_WARP：decomp 的 InstantWarp（id=1 表示已定义）。
struct InstantWarp {
    uint8_t id {0};
    uint8_t area {0};
    Vec3<int16_t> displacement {0, 0, 0};
};

// 0x3B WHIRLPOOL：index/condition 来自命令本身（decomp 只在运行时按 condition
// 求值后才创建 Whirlpool{pos, strength}）；这里保留完整命令数据。
struct Whirlpool {
    uint8_t index {0};
    uint8_t condition {0};
    Vec3<int16_t> pos {0, 0, 0};
    int16_t strength {0};
};

// 0x33 TRANSITION：decomp 的 WarpTransitionData（type, time, r, g, b）。
struct WarpTransition {
    uint8_t type {0};
    uint8_t time {0};
    uint8_t r {0};
    uint8_t g {0};
    uint8_t b {0};
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

    // --- 关卡脚本记录的区域级数据（镜像 decomp 的 struct Area） ---
    std::vector<WarpNode> warp_nodes;            // 0x26 WARP_NODE
    std::vector<WarpNode> painting_warp_nodes;   // 0x27 PAINTING_WARP_NODE（按 id 索引）
    std::array<InstantWarp, 4> instant_warps {}; // 0x28 INSTANT_WARP（id = index）
    std::vector<Whirlpool> whirlpools;           // 0x3B WHIRLPOOL（index 0/1）
    uint8_t dialog[2] {0, 0};                    // 0x30 SHOW_DIALOG（index < 2）
    uint16_t music_param {0};                    // 0x36 SET_BACKGROUND_MUSIC settingsPreset
    uint16_t music_param2 {0};                   // 0x36 SET_BACKGROUND_MUSIC seq
    int16_t unused_area_28[5] {0, 0, 0, 0, 0};   // 0x3A CMD3A（游戏里未使用）
    // 0x1F BEGIN_AREA：区域绑定的相机（geo 根节点 views[0] 的相机节点，见
    // level_cmd_begin_area / GeoLayoutProcessor 的 ASSIGN_AS_VIEW）。
    const GraphNode *camera {nullptr};
};

struct Level {
    std::array<Area, 8> areas;
    std::array<std::unique_ptr<GraphNode>, 0x100> loaded_graph_node;
    GraphNodeStart object_parent;
    Vec3<int16_t> mario_start_pos {};
    int16_t mario_start_angle_y {0};
    int8_t mario_start_area {0};
    // 0x25 INIT_MARIO（MARIO 命令）：Mario 出生行为（模型/参数/行为脚本）。
    int16_t mario_model_id {0};
    uint32_t mario_behavior_arg {0};
    SegmentedAddress mario_behavior_script {};
    // 0x33 TRANSITION：全局过渡设置（decomp 存于全局 WarpTransitionData）。
    WarpTransition transition {};
};

#endif /* AREA_H */
