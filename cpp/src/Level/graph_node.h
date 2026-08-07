#ifndef GRAPH_NODE_H
#define GRAPH_NODE_H

#include "display_list.h"
#include "Math/math.h"
#include <cstdint>
#include <list>
#include <memory>
#include <variant>
#include <vector>

enum GraphNodeFlag : int16_t {
    GRAPH_RENDER_ACTIVE = 1 << 0,
    GRAPH_RENDER_CHILDREN_FIRST = 1 << 1,
    GRAPH_RENDER_BILLBOARD = 1 << 2,
    GRAPH_RENDER_Z_BUFFER = 1 << 3,
    GRAPH_RENDER_INVISIBLE = 1<< 4,
    GRAPH_RENDER_HAS_ANIMATION = 1 << 5
};

struct GraphNode; // 前置声明（GraphNodeRoot.views 保存非拥有的节点指针）

struct GraphNodeRoot {
    uint8_t area_index;
    int16_t x;
    int16_t y;
    int16_t width; // half width, 160
    int16_t height; // half height
    // 视图注册表（decomp 的 gGeoViews）：ASSIGN_AS_VIEW / NODE_CAMERA 把节点
    // 注册进 views，views[0] 通常是区域相机。原始指针非拥有（指向场景图内节点）。
    int16_t num_views {0};
    std::vector<GraphNode *> views;
};

struct GraphNodeOrthoProjection {
    float scale;
};

struct GraphNodePerspective {
    // fn
    // int32_t unused;
    float fov;
    int16_t near;
    int16_t far;
    // GEO_PERSPECTIVE 的可选 frustum 函数（raw 段地址，不执行）。
    uint32_t func {0};
};

struct GraphNodeMasterList {
    // SM64：主列表节点每帧把要渲染的子 DL（带各自的变换矩阵）写进这个列表
    // 并依次执行。静态导出不填充/使用它（DL 直接由 collectDisplayLists 收集）。
    std::list<DisplayListNode> display_lists;
};

struct GraphNodeStart {};

struct GraphNodeLevelOfDetail {
    int16_t min_distance;
    int16_t max_distance;
};

struct GraphNodeSwitchCase {
    // fn
    int32_t unused;
    int16_t num_cases;
    int16_t selected_case;
    // case 选择/更新函数（raw 段地址，不执行）。
    uint32_t func {0};
};

struct GraphNodeCamera {
    // fn
    // union {mode; camera} config;
    Vec3<float> pos;
    Vec3<float> focus;
    // SM64：相机节点的 look-at 矩阵（视图矩阵，由 geo 处理计算）。
    // 导出不渲染场景，保留以忠实还原 decomp 的结构（游戏里是定点 Mtx，
    // 这里用浮点 Mtxf 记录）。
    Mtxf look_at;
    int16_t roll;
    int16_t roll_screen;
    // GEO_CAMERA 的相机类型（mode）与函数指针（raw 段地址，不执行）。
    int16_t mode {0};
    uint32_t func {0};
};

struct GraphNodeTranslationRotation {
    SegmentedAddress display_list;
    Vec3<int16_t> translation;
    Vec3<int16_t> rotation;
};

struct GraphNodeTranslation {
    SegmentedAddress display_list;
    Vec3<int16_t> translation;
    // uint8_t filler[2]; seems to be used for padding.
};

struct GraphNodeRotation {
    SegmentedAddress display_list;
    Vec3<int16_t> rotation;
    // uint8_t filler[2]; seems to be used for padding.
};

struct GraphNodeAnimatedPart {
    SegmentedAddress display_list;
    Vec3<int16_t> translation;
};

struct GraphNodeBillboard {
    SegmentedAddress display_list;
    Vec3<int16_t> translation;
};

struct GraphNodeDisplayList {
    SegmentedAddress display_list;
};

struct GraphNodeScale {
    SegmentedAddress display_list;
    float scale;
};

struct GraphNodeShadow {
    int16_t shadow_scale;
    uint8_t shadow_solidity;
    uint8_t shadow_type;
};

struct GraphNodeObjectParent {
    // should have a list of object nodes
};

struct GraphNodeGenerated {
    // 动态生成 DL 的 ASM 函数 + 参数（如 movtex/水/环境效果 id；raw 地址，不执行）。
    int32_t parameter {0};
    uint32_t func {0};
};

struct GraphNodeBackGround {
    // 背景 id 或 RGBA5551 填充色（backgroundFunc 为 null 时是颜色）；raw s16 值。
    int32_t background {0};
    uint32_t func {0};
};

struct GraphNodeHeldObject {
    // fn
    // int32_t player_index;
    // object
    Vec3<int16_t> translation;
    uint8_t player_index {0};
    uint32_t func {0};
};

struct GraphNodeCullingRadius {
    int16_t culling_radius;
    uint8_t filler[2];
};

using NodeData = std::variant<
    GraphNodeRoot,
    GraphNodeOrthoProjection,
    GraphNodePerspective,
    GraphNodeMasterList,
    GraphNodeStart,
    GraphNodeLevelOfDetail,
    GraphNodeSwitchCase,
    GraphNodeCamera,
    GraphNodeTranslationRotation,
    GraphNodeTranslation,
    GraphNodeRotation,
    GraphNodeAnimatedPart,
    GraphNodeBillboard,
    GraphNodeDisplayList,
    GraphNodeScale,
    GraphNodeShadow,
    GraphNodeObjectParent,
    GraphNodeGenerated,
    GraphNodeBackGround,
    GraphNodeHeldObject,
    GraphNodeCullingRadius
>;

struct GraphNode {
    int16_t flags;
    NodeData data;

    std::vector<std::unique_ptr<GraphNode>> children; // nodes that statically belongs to this node

    GraphNode &addChild(std::unique_ptr<GraphNode> node);

    static std::string_view variantName(const NodeData& data) noexcept;
    std::string_view variantName() const noexcept;
};

#endif /* GRAPH_NODE_H */
