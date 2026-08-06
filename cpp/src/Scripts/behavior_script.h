#ifndef BEHAVIOR_SCRIPT_H
#define BEHAVIOR_SCRIPT_H

#include "Memory/segment.h"
#include <cstdint>
#include <vector>

// 对象行为脚本的静态解释器（静态资产提取用）。
//
// 行为脚本是固定长度的 u32 数组（游戏里位于段 0x13，由 LevelScript 的公共段
// 加载 setup 载入；见 docs/engine-notes.md）。每个命令 4 字节对齐，高字节是
// 操作码，参数打包在低字节与后续命令里。游戏在每帧带对象上下文（字段/计时/
// 随机数/原生函数）解释它，这里无法执行那一层；因此解释器只静态走查脚本、
// 跟随控制流（CALL/RETURN/GOTO/循环，带有限预算），并记录静态导出需要的数据
// （模型、动画数组 + 选中的动画、碰撞数据、hitbox、渲染标志、缩放）。
//
// 不透明/未知命令（CALL_NATIVE、对象字段上的 SET_*/ADD_*、粒子生成等）只推进
// 指令指针，不影响结果。LOOP 体只走一遍；DELAY 视作直接推进。
namespace BehaviorScript {

// analyze 提取出的静态信息。
struct Info {
    bool ok {false};              // 脚本能被解释到底（进入过 BREAK/DEACTIVATE）
    int16_t obj_list {-1};        // BEGIN(objList) 的对象列表编号
    int16_t model_id {-1};        // 最后一次 SET_MODEL（行为覆盖模型 id）
    SegmentedAddress animations {}; // LOAD_ANIMATIONS 的动画数组地址
    int16_t animate_index {-1};   // 第一个 ANIMATE 的动画索引（出生即播放的动画）
    SegmentedAddress collision_data {}; // LOAD_COLLISION_DATA 的碰撞数据地址
    int16_t hitbox_radius {0};    // SET_HITBOX(_WITH_OFFSET) 半径
    int16_t hitbox_height {0};    // SET_HITBOX(_WITH_OFFSET) 高度
    int16_t hitbox_down_offset {0}; // SET_HITBOX_WITH_OFFSET 下移量
    bool billboard {false};       // BILLBOARD
    bool hide {false};            // HIDE
    bool disable_rendering {false}; // DISABLE_RENDERING
    bool scale_seen {false};      // 出现过 SCALE
    int16_t scale_percent {100};  // SCALE 的百分比
    std::vector<SegmentedAddress> spawned_behaviors; // SPAWN_CHILD/SPAWN_OBJ/... 目标行为
};

// 走查 `entry` 处的行为脚本并返回静态信息。entry 的段必须已加载（如 0x13），
// 否则返回 !ok 的空 Info。
Info analyze(const SegmentTable &seg_table, SegmentedAddress entry);

} // namespace BehaviorScript

#endif /* BEHAVIOR_SCRIPT_H */
