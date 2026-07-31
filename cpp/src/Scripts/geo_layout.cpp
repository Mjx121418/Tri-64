#include "geo_layout.h"

GeoLayoutProcessor::GeoLayoutProcessor(SegmentTable &seg_table) : seg_table(seg_table) {
    // The arrays are re-initialised by every processGeoLayout call.
}

void GeoLayoutProcessor::registerSceneGraphNode(std::unique_ptr<GraphNode> node) {
    if (graph_node_index < 0 || graph_node_index >= static_cast<int16_t>(graph_node_list.size())) {
        fprintf(stderr, "GeoLayout: register with invalid node index %d, dropping node\n", graph_node_index);
        return;
    }

    if (graph_node_index == 0) {
        if (root_graph_node == nullptr) {
            root_graph_node = std::move(node);
            graph_node_list[0] = root_graph_node.get();
        }
        // A second node at root level is orphaned by the decomp
        // (register_scene_graph_node only attaches children for index > 0),
        // so it is dropped here.
    } else {
        GraphNode *parent = graph_node_list[graph_node_index - 1];
        if (parent == nullptr) {
            fprintf(stderr, "GeoLayout: register with null parent at seg %#04x off %#06x, dropping node\n",
                    geo_layout_command.seg, geo_layout_command.offset);
            return;
        }
        graph_node_list[graph_node_index] = node.get();
        parent->addChild(std::move(node));
    }
}

void GeoLayoutProcessor::cmdBranchAndLink() {
    if (address_stack_index >= static_cast<int16_t>(address_stack.size())
        || frame_stack_index >= static_cast<int16_t>(frame_stack.size())) {
        fprintf(stderr, "GeoLayout: stack overflow in branch_and_link\n");
        geo_layout_command = SegmentedAddress {};
        return;
    }

    geo_layout_command.offset += 8;
    address_stack[address_stack_index++] = geo_layout_command;
    frame_stack[frame_stack_index++] = Frame {
        static_cast<int16_t>(address_stack_index),
        graph_node_index
    };
    geo_layout_command.setAddress(readInt<uint32_t>(command_data, 4));
}

void GeoLayoutProcessor::cmdEnd() {
    if (frame_stack_index <= 0) {
        fprintf(stderr, "GeoLayout: end with no frame, terminating layout\n");
        geo_layout_command = SegmentedAddress {};
        return;
    }

    const Frame frame = frame_stack[--frame_stack_index];
    address_stack_index = frame.return_index;
    graph_node_index = frame.graph_node_index;

    if (address_stack_index <= 0) {
        geo_layout_command = SegmentedAddress {};
        return;
    }
    geo_layout_command = address_stack[--address_stack_index];
}

void GeoLayoutProcessor::cmdBranch() {
    if (readInt<uint8_t>(command_data, 0x01) == 1) {
        if (address_stack_index >= static_cast<int16_t>(address_stack.size())) {
            fprintf(stderr, "GeoLayout: stack overflow in branch\n");
            geo_layout_command = SegmentedAddress {};
            return;
        }
        geo_layout_command.offset += 8;
        address_stack[address_stack_index++] = geo_layout_command;
    }

    geo_layout_command.setAddress(readInt<uint32_t>(command_data, 4));
}

void GeoLayoutProcessor::cmdReturn() {
    if (address_stack_index <= 1) {
        geo_layout_command = SegmentedAddress {};
        return;
    }
    geo_layout_command = address_stack[--address_stack_index];
}

void GeoLayoutProcessor::cmdOpenNode() {
    if (graph_node_index < 0 || graph_node_index + 1 >= static_cast<int16_t>(graph_node_list.size())) {
        fprintf(stderr, "GeoLayout: node list under/overflow in open_node, terminating layout\n");
        geo_layout_command = SegmentedAddress {};
        return;
    }
    graph_node_list[graph_node_index + 1] = graph_node_list[graph_node_index];
    graph_node_index++;
    geo_layout_command.offset += 4;
}

void GeoLayoutProcessor::cmdCloseNode() {
    // The cursor may go negative here; the node list *contents* survive, and
    // End restores the cursor from the saved frame (as in the decomp).
    graph_node_index--;
    geo_layout_command.offset += 4;
}
void GeoLayoutProcessor::cmdAssignAsView() {
    geo_layout_command.offset += 4;
}

void GeoLayoutProcessor::cmdUpdateNodeFlags() {
    // don't know why read an uint8_t into an uint16_t type
    uint16_t operation = readInt<uint8_t>(command_data, 1);
    uint16_t flagBits = readInt<int16_t>(command_data, 2);
    if (graph_node_index < 0 || graph_node_list[graph_node_index] == nullptr) {
        fprintf(stderr, "GeoLayout: update_node_flags with no current node\n");
        geo_layout_command.offset += 4;
        return;
    }
    GraphNode& node = *graph_node_list[graph_node_index];

    switch (operation) {
    case 0:
        node.flags = flagBits;
        break;
    case 1:
        node.flags |= flagBits;
        break;
    case 2:
        node.flags &= flagBits;
        break;
    }

    geo_layout_command.offset += 4;
}

void GeoLayoutProcessor::cmdNodeRoot() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
    int16_t x = readInt<int16_t>(command_data, 4);
    int16_t y = readInt<int16_t>(command_data, 6);
    int16_t width = readInt<int16_t>(command_data, 8);
    int16_t height = readInt<int16_t>(command_data, 10);

    // TODO (or not): geiViews

    node->flags = GraphNodeFlag::GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeRoot { 0, x, y, width, height };

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 12;
}

void GeoLayoutProcessor::cmdNodeOrthoProjection() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
    float scale = static_cast<float>(readInt<int16_t>(command_data, 2)) / 100.0f;

    node->flags = GraphNodeFlag::GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeOrthoProjection {scale};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 4;
}

void GeoLayoutProcessor::cmdNodePerspective() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
    float fov = static_cast<float>(readInt<int16_t>(command_data, 2));
    int16_t near = readInt<int16_t>(command_data, 4);
    int16_t far = readInt<int16_t>(command_data, 6);

    if (readInt<uint8_t>(command_data, 1) != 0) {
        // TODO (or not): ASM function
        geo_layout_command.offset += 4;
    }

    node->flags = GraphNodeFlag::GRAPH_RENDER_ACTIVE;
    node->data = GraphNodePerspective {fov, near, far};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 8;
}

void GeoLayoutProcessor::cmdNodeStart() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();

    node->flags = GraphNodeFlag::GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeStart {};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 4;
}

void GeoLayoutProcessor::cmdNodeMasterList() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
    uint8_t on = readInt<uint8_t>(command_data, 1);

    node->flags = GRAPH_RENDER_ACTIVE;
    if (on) {
        node->flags |= GRAPH_RENDER_Z_BUFFER;
    }
    node->data = GraphNodeMasterList {};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 4;
}

void GeoLayoutProcessor::cmdNodeLevelOfDetail() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
    int16_t min_distance = readInt<int16_t>(command_data, 4);
    int16_t max_distance = readInt<int16_t>(command_data, 6);

    node->flags = GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeLevelOfDetail {min_distance, max_distance};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 8;
}

void GeoLayoutProcessor::cmdNodeSwitchCase() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
    int16_t num_cases = readInt<int16_t>(command_data, 2);
    uint32_t node_func = readInt<uint32_t>(command_data, 4); // fn

    node->flags = GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeSwitchCase {0, num_cases, 0};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 8;
}

// currently do nothing but add a node.
void GeoLayoutProcessor::cmdNodeCamera() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();

    node->flags = GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeCamera {};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 20;
}

void GeoLayoutProcessor::cmdNodeTranslationRotation() { // TODO: offset calculation
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
    Vec3<int16_t> translation {0, 0, 0};
    Vec3<int16_t> rotation {0, 0, 0};
    SegmentedAddress display_list;
    int16_t drawing_layer {0};

    int16_t params = readInt<uint8_t>(command_data, 1);
    size_t display_list_offset = 4;

    switch ((params & 0x70) >> 4) {
        case 0:
            translation = readVec3s(command_data, 4);
            rotation = readVec3sAngle(command_data, 10);
            display_list_offset += 12;
            break;
        case 1:
            translation = readVec3s(command_data, 2);
            display_list_offset += 4;
            break;
        case 2:
            rotation = readVec3sAngle(command_data, 2);
            display_list_offset += 4;
            break;
        case 3:
            rotation.y = (readInt<int16_t>(command_data, 2) << 15) / 180;
            break;
    }

    if (params & 0x80) {
        display_list.setAddress(readInt<uint32_t>(command_data, display_list_offset));
        drawing_layer = params & 0x0F;
        geo_layout_command.offset += 4;
    }

    node->flags = (drawing_layer << 8) | GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeTranslationRotation {display_list, translation, rotation};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += display_list_offset;
}

void GeoLayoutProcessor::cmdNodeTranslation() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
    int16_t drawing_layer {0};
    SegmentedAddress display_list;

    int16_t params = readInt<uint8_t>(command_data, 1);
    Vec3<int16_t> translation = readVec3s(command_data, 2);

    if (params & 0x80) {
        display_list.setAddress(readInt<uint32_t>(command_data, 8));
        drawing_layer = params & 0x0F;
        geo_layout_command.offset += 4;
    }

    node->flags = (drawing_layer << 8) | GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeTranslation {display_list, translation};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 8;
}

void GeoLayoutProcessor::cmdNodeRotation() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
    int16_t drawing_layer {0};
    SegmentedAddress display_list;

    int16_t params = readInt<uint8_t>(command_data, 1);
    Vec3<int16_t> sp2c = readVec3sAngle(command_data, 2);

    if (params & 0x80) {
        display_list.setAddress(readInt<uint32_t>(command_data, 8));
        drawing_layer = params & 0x0F;
        geo_layout_command.offset += 4;
    }

    node->flags = (drawing_layer << 8) | GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeRotation {display_list, sp2c};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 8;
}

void GeoLayoutProcessor::cmdNodeAnimatedPart() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
    SegmentedAddress display_list;

    int32_t drawing_layer {readInt<uint8_t>(command_data, 1)};
    Vec3<int16_t> translation = readVec3s(command_data, 3);
    display_list.setAddress(readInt<uint32_t>(command_data, 8));

    node->flags = (drawing_layer << 8) | GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeAnimatedPart {display_list, translation};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 12;
}

void GeoLayoutProcessor::cmdNodeBillboard() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
    int16_t drawing_layer {0};
    SegmentedAddress display_list;

    int16_t params = readInt<uint8_t>(command_data, 1);
    Vec3<int16_t> translation = readVec3s(command_data, 2);

    if (params & 0x80) {
        display_list.setAddress(readInt<uint32_t>(command_data, 8));
        drawing_layer = params & 0x0F;
        geo_layout_command.offset += 4;
    }

    node->flags = (drawing_layer << 8) | GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeBillboard {display_list, translation};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 8;
}

void GeoLayoutProcessor::cmdNodeDisplayList() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
    SegmentedAddress display_list;

    int32_t drawing_layer {readInt<uint8_t>(command_data, 1)};
    display_list.setAddress(readInt<uint32_t>(command_data, 4));

    node->flags = (drawing_layer << 8) | GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeDisplayList {display_list};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 8;
}

void GeoLayoutProcessor::cmdNodeShadow() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
    uint8_t shadow_type = readInt<int16_t>(command_data, 2);
    uint8_t shadow_solidity = readInt<int16_t>(command_data, 4);
    int16_t shadow_scale = readInt<int16_t>(command_data, 6);

    node->flags = GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeShadow {shadow_scale, shadow_solidity, shadow_type};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 8;
}

void GeoLayoutProcessor::cmdNodeObjectParent() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();

    node->flags = GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeObjectParent {};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 4;
}

void GeoLayoutProcessor::cmdNodeGenerated() {
    // TODO (or not): fn
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();

    node->flags = GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeGenerated {};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 8;
}

void GeoLayoutProcessor::cmdNodeBackground() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();

    int16_t background_id {readInt<int16_t>(command_data, 2)};
    uint32_t func {readInt<uint32_t>(command_data, 4)};

    node->flags = GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeBackGround {(background_id << 16) + background_id};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 8;
}

void GeoLayoutProcessor::cmdNOP() {
    geo_layout_command.offset += 8;
}

void GeoLayoutProcessor::cmdCopyView() {
    // TODO (or not)
    geo_layout_command.offset += 4;
}

void GeoLayoutProcessor::cmdNodeHeldObject() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();

    Vec3<int16_t> offset = readVec3s(command_data, 2);
    uint32_t func = readInt<uint32_t>(command_data, 8);

    node->flags = GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeHeldObject {offset};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 12;
}

void GeoLayoutProcessor::cmdNodeScale() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
    int16_t drawing_layer {0};
    SegmentedAddress display_list;

    int16_t params = readInt<uint8_t>(command_data, 1);
    float scale = static_cast<float>(readInt<uint32_t>(command_data, 4)) / 65536.0f;

    if (params & 0x80) {
        display_list.setAddress(readInt<uint32_t>(command_data, 8));
        drawing_layer = params & 0x0F;
        geo_layout_command.offset += 4;
    }

    node->flags = (drawing_layer << 8) | GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeScale {display_list, scale};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 8;
}

void GeoLayoutProcessor::cmdNOP2() {
    geo_layout_command.offset += 8;
}

void GeoLayoutProcessor::cmdNOP3() {
    geo_layout_command.offset += 16;
}

void GeoLayoutProcessor::cmdNodeCullingRadius() {
    std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();

    int16_t radius = readInt<uint32_t>(command_data, 8);

    node->flags = GRAPH_RENDER_ACTIVE;
    node->data = GraphNodeCullingRadius {radius};

    registerSceneGraphNode(std::move(node));
    geo_layout_command.offset += 4;
}

std::unique_ptr<GraphNode> GeoLayoutProcessor::processGeoLayout(SegmentedAddress seg_ptr) {
    root_graph_node = nullptr;
    address_stack_index = 1;
    frame_stack_index = 1;
    graph_node_index = 0;
    address_stack[0] = SegmentedAddress {};   // null sentinel (top-level End returns to it)
    frame_stack[0] = Frame {1, 0};            // sentinel frame: return to the null address
    graph_node_list[0] = nullptr;
    geo_layout_command = seg_ptr;

    while (!geo_layout_command.isNull()) {
        uint8_t command = seg_table.read(geo_layout_command);
        command_data = seg_table.data(geo_layout_command);

        switch (command) {
            case 0x00:
                cmdBranchAndLink();
                break;
            case 0x01:
                cmdEnd();
                break;
            case 0x02:
                cmdBranch();
                break;
            case 0x03:
                cmdReturn();
                break;
            case 0x04:
                cmdOpenNode();
                break;
            case 0x05:
                cmdCloseNode();
                break;
            case 0x06:
                cmdAssignAsView();
                break;
            case 0x07:
                cmdUpdateNodeFlags();
                break;
            case 0x08:
                cmdNodeRoot();
                break;
            case 0x09:
                cmdNodeOrthoProjection();
                break;
            case 0x0A:
                cmdNodePerspective();
                break;
            case 0x0B:
                cmdNodeStart();
                break;
            case 0x0C:
                cmdNodeMasterList();
                break;
            case 0x0D:
                cmdNodeLevelOfDetail();
                break;
            case 0x0E:
                cmdNodeSwitchCase();
                break;
            case 0x0F:
                cmdNodeCamera();
                break;
            case 0x10:
                cmdNodeTranslationRotation();
                break;
            case 0x11:
                cmdNodeTranslation();
                break;
            case 0x12:
                cmdNodeRotation();
                break;
            case 0x13:
                cmdNodeAnimatedPart();
                break;
            case 0x14:
                cmdNodeBillboard();
                break;
            case 0x15:
                cmdNodeDisplayList();
                break;
            case 0x16:
                cmdNodeShadow();
                break;
            case 0x17:
                cmdNodeObjectParent();
                break;
            case 0x18:
                cmdNodeGenerated();
                break;
            case 0x19:
                cmdNodeBackground();
                break;
            case 0x1A:
                cmdNOP();
                break;
            case 0x1B:
                cmdCopyView();
                break;
            case 0x1C:
                cmdNodeHeldObject();
                break;
            case 0x1D:
                cmdNodeScale();
                break;
            case 0x1E:
                cmdNOP2();
                break;
            case 0x1F:
                cmdNOP3();
                break;
            case 0x20:
                cmdNodeCullingRadius();
                break;
        }
    }

    return std::move(root_graph_node);
}
