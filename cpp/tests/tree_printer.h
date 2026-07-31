#ifndef TREE_PRINTER_H
#define TREE_PRINTER_H

#include "Level/graph_node.h"
#include <print>

// Depth-first pretty print of a graph node tree, used by the tests to verify
// that a processed geo layout (or a whole level) has the expected structure.
inline void printNodeTree(GraphNode &root, int depth) {
    for (int i {0}; i < depth; i++) {
        std::print("|");
    }
    std::println("-{}", root.variantName());
    for (int i {0}; i < root.children.size(); i++) {
        printNodeTree(*root.children[i], depth + 1);
    }
}

#endif /* TREE_PRINTER_H */
