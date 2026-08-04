#ifndef GEO_LAYOUT_H
#define GEO_LAYOUT_H

#include "Level/graph_node.h"
#include "Memory/segment.h"
#include <array>
#include <cstdint>

class GeoLayoutProcessor {
    struct Frame {
        int16_t return_index;
        int16_t graph_node_index;
    };

    SegmentTable &seg_table;

    std::unique_ptr<GraphNode> root_graph_node;
    // std::unique_ptr<GraphNodeStart> obj_parent_graph_node;
    SegmentedAddress geo_layout_command;
    std::span<uint8_t> command_data;

    // Three fixed arrays with explicit cursors, mirroring the decomp's model
    // (gGeoLayoutStack + gCurGraphNodeList + gCurGraphNodeIndex): the node
    // path *contents* survive close over-runs, and End only restores the
    // cursors. A vector that pops on close cannot emulate that.
    std::array<SegmentedAddress, 16> address_stack {};
    std::array<Frame, 16> frame_stack {};
    std::array<GraphNode *, 32> graph_node_list {};
    int16_t address_stack_index { 1 };
    int16_t frame_stack_index { 1 };
    int16_t graph_node_index { 0 };

    void registerSceneGraphNode(std::unique_ptr<GraphNode> node);

    // 0x00
    void cmdBranchAndLink();
    // 0x01
    void cmdEnd();
    // 0x02
    void cmdBranch();
    // 0x03
    void cmdReturn();
    // 0x04
    void cmdOpenNode();
    // 0x05
    void cmdCloseNode();
    // 0x06
    void cmdAssignAsView(); // never used. May implement it in some day.
    // 0x07
    void cmdUpdateNodeFlags();
    // 0x08
    void cmdNodeRoot();
    // 0x09
    void cmdNodeOrthoProjection();
    // 0x0A
    void cmdNodePerspective();
    // 0x0B
    void cmdNodeStart();
    // 0x0C
    void cmdNodeMasterList();
    // 0x0D
    void cmdNodeLevelOfDetail();
    // 0x0E
    void cmdNodeSwitchCase();
    // 0x0F
    void cmdNodeCamera();
    // 0x10
    void cmdNodeTranslationRotation();
    // 0x11
    void cmdNodeTranslation();
    // 0x12
    void cmdNodeRotation();
    // 0x13
    void cmdNodeAnimatedPart();
    // 0x14
    void cmdNodeBillboard();
    // 0x15
    void cmdNodeDisplayList();
    // 0x16
    void cmdNodeShadow();
    // 0x17
    void cmdNodeObjectParent();
    // 0x18
    void cmdNodeGenerated();
    // 0x19
    void cmdNodeBackground();
    // 0x1A
    void cmdNOP();
    // 0x1B
    void cmdCopyView();
    // 0x1C
    void cmdNodeHeldObject();
    // 0x1D
    void cmdNodeScale();
    // 0x1E
    void cmdNOP2();
    // 0x1F
    void cmdNOP3();
    // 0x20
    void cmdNodeCullingRadius();

public:
    GeoLayoutProcessor(SegmentTable &seg_table);
    std::unique_ptr<GraphNode> processGeoLayout(SegmentedAddress seg_ptr);
};

#endif /* GEO_LAYOUT_H */
