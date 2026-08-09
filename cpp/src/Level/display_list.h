#ifndef DISPLAY_LIST_H
#define DISPLAY_LIST_H

#include "Math/math.h"
#include "Memory/segment.h"

// 场景图节点持有的显示列表引用（GraphNodeMasterList 的 display_lists 使用）。
// SM64 每帧渲染时给每个可渲染节点附一个变换矩阵 + 显示列表并依次执行；
// 静态导出只遍历场景图收集 DL 地址（collectDisplayLists），transform 未参与计算。
// 游戏里是定点 Mtx，这里用浮点 Mtxf 记录以保持结构一致。
struct DisplayListNode {
    Mtxf transform;
    SegmentedAddress display_list;
};

#endif /* DISPLAY_LIST_H */
