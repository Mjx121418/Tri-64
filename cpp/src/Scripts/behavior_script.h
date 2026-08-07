#ifndef BEHAVIOR_SCRIPT_H
#define BEHAVIOR_SCRIPT_H

#include "Memory/segment.h"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
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
    bool hidden {false};          // HIDE / DISABLE_RENDERING（出生即不可见）
    bool billboard {false};       // BILLBOARD
    bool scale_seen {false};      // 出现过 SCALE
    int16_t scale_percent {100};  // SCALE 的百分比
    SegmentedAddress animations {}; // LOAD_ANIMATIONS 的动画数组地址
    int16_t animate_index {-1};   // 第一个 ANIMATE 的动画索引（出生即播放的动画）
    SegmentedAddress collision_data {}; // LOAD_COLLISION_DATA 的碰撞数据地址
    int16_t hitbox_radius {0};    // SET_HITBOX(_WITH_OFFSET) 半径
    int16_t hitbox_height {0};    // SET_HITBOX(_WITH_OFFSET) 高度
    int16_t hitbox_down_offset {0}; // SET_HITBOX_WITH_OFFSET 下移量
    std::vector<SegmentedAddress> spawned_behaviors; // SPAWN_CHILD/SPAWN_OBJ/... 目标行为
};

// 行为脚本的静态解释器类（镜像 LevelScriptVM 的结构）：构造时绑定段表与输出
// Info，run(entry) 重置全部运行状态后走查脚本并填入 info_。
class BehaviorScriptVM {
    const SegmentTable &seg_table_;
    Info &info_;

    // entry 所在段的整段数据（run 时取，未加载的段是空 span，见 SegmentTable）。
    std::span<const uint8_t> seg_;
    uint32_t pc_ {0};             // 当前命令在 seg_ 内的字节偏移
    std::vector<uint32_t> stack_; // 返回地址 + BEGIN_REPEAT 计数共用栈
    uint64_t executed_ {0};       // 指令预算（防死循环）
    uint64_t loop_passes_ {0};    // END_LOOP 已经回跳的次数
    bool animate_seen_ {false};   // 出生即播放的动画 = 程序顺序上第一个 ANIMATE
    bool finished_ {false};       // 脚本终止（正常或异常）

    // 每个操作码的命令长度（u32 命令数）。镜像 decomp 的 behavior_data.h 宏。
    static constexpr std::array<uint8_t, 0x38> kCommandLengths {
        /* 00 */ 1, /* 01 */ 1, /* 02 */ 2, /* 03 */ 1,
        /* 04 */ 2, /* 05 */ 1, /* 06 */ 1, /* 07 */ 1,
        /* 08 */ 1, /* 09 */ 1, /* 0A */ 1, /* 0B */ 1,
        /* 0C */ 2, /* 0D */ 1, /* 0E */ 1, /* 0F */ 1,
        /* 10 */ 1, /* 11 */ 1, /* 12 */ 1, /* 13 */ 2,
        /* 14 */ 2, /* 15 */ 2, /* 16 */ 2, /* 17 */ 2,
        /* 18 */ 1, /* 19 */ 1, /* 1A */ 1, /* 1B */ 1,
        /* 1C */ 3, /* 1D */ 1, /* 1E */ 1, /* 1F */ 1,
        /* 20 */ 1, /* 21 */ 1, /* 22 */ 1, /* 23 */ 2,
        /* 24 */ 1, /* 25 */ 1, /* 26 */ 1, /* 27 */ 2,
        /* 28 */ 1, /* 29 */ 3, /* 2A */ 2, /* 2B */ 3,
        /* 2C */ 3, /* 2D */ 1, /* 2E */ 2, /* 2F */ 2,
        /* 30 */ 5, /* 31 */ 2, /* 32 */ 1, /* 33 */ 2,
        /* 34 */ 1, /* 35 */ 1, /* 36 */ 2, /* 37 */ 2,
    };

    // 读一个 BE32 命令。越界返回 nullopt（脚本走查中止而非抛异常）。
    std::optional<uint32_t> readWord(uint32_t offset) const;
    // 分段地址参数（CALL/GOTO/LOAD_*/SPAWN_* 的目标），word_index = 相对当前命令
    // 的第几个 u32。
    std::optional<SegmentedAddress> targetOf(uint32_t word_index) const;
    // 按 opcode 分发到 cmd*。每条 cmd* 负责推进 pc_（或跳转/终止）。
    void dispatch(uint32_t word);

    // 0x00 BEGIN(objList): objList 在字节 1
    void cmdBegin(uint32_t word);
    // 0x01 DELAY / 0x25 DELAY_VAR: 出生帧脚本停在第一个 DELAY（返回
    // BHV_PROC_BREAK），之后的命令不是出生状态，终止走查。
    void cmdDelay();
    // 0x02 CALL(addr): 压入返回地址后跳转
    void cmdCall();
    // 0x03 RETURN()
    void cmdReturn();
    // 0x04 GOTO(addr)
    void cmdGoto();
    // 0x05 BEGIN_REPEAT(count)
    void cmdBeginRepeat(uint32_t word);
    // 0x06 END_REPEAT() / 0x07 END_REPEAT_CONTINUE()
    void cmdEndRepeat();
    // 0x08 BEGIN_LOOP()
    void cmdBeginLoop();
    // 0x09 END_LOOP(): 循环体只走一遍（静态导出不需要无限展开）。
    void cmdEndLoop();
    // 0x0A BREAK / 0x0B BREAK_UNUSED / 0x1D DEACTIVATE: 脚本终止
    void cmdBreak();
    // 0x0C CALL_NATIVE(func): 原生函数无法执行，跳过
    void cmdCallNative();
    // 0x1B SET_MODEL(modelID)
    void cmdSetModel(uint32_t word);
    // 0x21 BILLBOARD / 0x22 HIDE / 0x35 DISABLE_RENDERING
    void cmdBillboard();
    void cmdHide();
    // 0x23 SET_HITBOX(radius, height)
    void cmdSetHitbox();
    // 0x2B SET_HITBOX_WITH_OFFSET(radius, height, downOffset)
    void cmdSetHitboxWithOffset();
    // 0x27 LOAD_ANIMATIONS(field, anims)
    void cmdLoadAnimations();
    // 0x28 ANIMATE(animIndex): 只记录第一个（出生即播放的动画）
    void cmdAnimate(uint32_t word);
    // 0x2A LOAD_COLLISION_DATA(collisionData)
    void cmdLoadCollisionData();
    // 0x1C SPAWN_CHILD / 0x29 SPAWN_CHILD_WITH_PARAM / 0x2C SPAWN_OBJ
    void cmdSpawn();
    // 0x32 SCALE(percent)
    void cmdScale(uint32_t word);
    // 其余命令：不改变静态导出所需状态，只推进
    void cmdAdvance(uint32_t length);

public:
    BehaviorScriptVM(const SegmentTable &seg_table, Info &info) :
        seg_table_(seg_table), info_(info) {}

    // 走查 entry 处的行为脚本并填 info_。entry 的段必须已加载（如 0x13），
    // 否则 info_.ok 保持 false。
    void run(SegmentedAddress entry);
};

} // namespace BehaviorScript

#endif /* BEHAVIOR_SCRIPT_H */
