#ifndef DISPLAY_LIST_H
#define DISPLAY_LIST_H

#include "Math/math.h"
#include "Memory/segment.h"
#include <cstdint>

// 场景图节点持有的显示列表引用（GraphNodeMasterList 的 display_lists 使用）。
// GBI 命令层与解释器见 dl_command.h / dl_interpreter.h。
struct DisplayListNode {
    Mat4<int32_t> transform;
    SegmentedAddress display_list;
};

#endif /* DISPLAY_LIST_H */
