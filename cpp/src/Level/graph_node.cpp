#include "graph_node.h"

GraphNode &GraphNode::addChild(std::unique_ptr<GraphNode> node) {
    children.push_back(std::move(node));
    return *children.back();
}

std::string_view GraphNode::variantName(const NodeData& data) noexcept {
    static constexpr std::array names {
        std::string_view { "GraphNodeRoot" },
        std::string_view { "GraphNodeOrthoProjection" },
        std::string_view { "GraphNodePerspective" },
        std::string_view { "GraphNodeMasterList" },
        std::string_view { "GraphNodeStart" },
        std::string_view { "GraphNodeLevelOfDetail" },
        std::string_view { "GraphNodeSwitchCase" },
        std::string_view { "GraphNodeCamera" },
        std::string_view { "GraphNodeTranslationRotation" },
        std::string_view { "GraphNodeTranslation" },
        std::string_view { "GraphNodeRotation" },
        std::string_view { "GraphNodeAnimatedPart" },
        std::string_view { "GraphNodeBillboard" },
        std::string_view { "GraphNodeDisplayList" },
        std::string_view { "GraphNodeScale" },
        std::string_view { "GraphNodeShadow" },
        std::string_view { "GraphNodeObjectParent" },
        std::string_view { "GraphNodeGenerated" },
        std::string_view { "GraphNodeBackGround" },
        std::string_view { "GraphNodeHeldObject" },
        std::string_view { "GraphNodeCullingRadius" },
    };

    static_assert(
        names.size() == std::variant_size_v<NodeData>,
        "Every NodeData alternative must have a name"
    );

    if (data.valueless_by_exception()) {
        return "ValuelessNodeData";
    }

    return names[data.index()];
}

std::string_view GraphNode::variantName() const noexcept {
    return variantName(data);
}
