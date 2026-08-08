#ifndef BEHAVIOR_SCRIPT_H
#define BEHAVIOR_SCRIPT_H

#include "Level/Object.h"
#include "Memory/segment.h"
#include "Scripts/Collision.h"
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
// 随机数/原生函数）解释它；这里做静态走查：跟随控制流（CALL/RETURN/GOTO/循环，
// 带有限预算），并在命令出现的位置直接作用于 ObjectExtract::Object（像游戏里
// 作用于 gCurrentObject），因此 SET/ADD/OR/SUM 的先后顺序天然正确。
//
// 出生路径在第一个"帧断点"命令处停止（DELAY/DELAY_VAR/END_REPEAT/END_LOOP/
// BREAK/DEACTIVATE 返回 BHV_PROC_BREAK），之后的命令不是出生状态。
// CALL_NATIVE 与随机命令无法执行，只推进指令指针。
namespace BehaviorScript {

class BehaviorScriptVM {
    const SegmentTable &seg_table_;
    ObjectExtract::Object *obj_ {nullptr};
    const Collision::Data *floor_ {nullptr}; // DROP_TO_FLOOR 用的地形碰撞（可空）

    // entry 所在段的整段数据（run 时取，未加载的段是空 span，见 SegmentTable）。
    std::span<const uint8_t> seg_;
    uint32_t pc_ {0};             // 当前命令在 seg_ 内的字节偏移
    std::vector<uint32_t> stack_; // 返回地址 + BEGIN_REPEAT 计数共用栈
    uint64_t executed_ {0};       // 指令预算（防死循环）
    bool animate_seen_ {false};   // 出生即播放的动画 = 程序顺序上第一个 ANIMATE
    bool finished_ {false};       // 脚本终止（正常或异常）
    bool ok_ {false};             // 正常终止（帧断点命令，非预算/越界中止）

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
    // 读当前命令往后 rel 字节处的 s16（sub 0 = 高 16 位，1 = 低 16 位）。
    int16_t readS16(uint32_t rel, uint8_t sub) const;
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
    // 0x05 BEGIN_REPEAT(count) / 0x26 BEGIN_REPEAT_UNUSED(u8 count)
    void cmdBeginRepeat(uint32_t word, bool unused);
    // 0x06 END_REPEAT(): 游戏返回 BHV_PROC_BREAK（这一帧到此为止）——循环体只
    // 走一遍（出生帧状态），之后的命令不是出生状态。
    void cmdEndRepeat();
    // 0x07 END_REPEAT_CONTINUE(): 同帧继续执行循环后命令（把循环跑完再继续）。
    void cmdEndRepeatContinue();
    // 0x08 BEGIN_LOOP()
    void cmdBeginLoop();
    // 0x09 END_LOOP(): 循环体只走一遍（静态导出不需要无限展开）。
    void cmdEndLoop();
    // 0x0A BREAK / 0x0B BREAK_UNUSED: 脚本终止
    void cmdBreak();
    // 0x1D DEACTIVATE(): 脚本终止且对象被反生成（不渲染）
    void cmdDeactivate();
    // 0x0C CALL_NATIVE(func): 原生函数无法执行，跳过
    void cmdCallNative();
    // 0x0D ADD_FLOAT / 0x0E SET_FLOAT
    void cmdAddFloat(uint32_t word);
    void cmdSetFloat(uint32_t word);
    // 0x0F ADD_INT / 0x10 SET_INT / 0x11 OR_INT / 0x12 BIT_CLEAR
    void cmdAddInt(uint32_t word);
    void cmdSetInt(uint32_t word);
    void cmdOrInt(uint32_t word);
    void cmdBitClear(uint32_t word);
    // 0x1F SUM_FLOAT / 0x20 SUM_INT
    void cmdSumFloat(uint32_t word);
    void cmdSumInt(uint32_t word);
    // 0x1B SET_MODEL(modelID)
    void cmdSetModel(uint32_t word);
    // 0x1C SPAWN_CHILD / 0x29 SPAWN_CHILD_WITH_PARAM / 0x2C SPAWN_OBJ
    void cmdSpawn(uint32_t word);
    // 0x1E DROP_TO_FLOOR(): 出生时把对象落到脚下地面（需要 floor_ 地形碰撞）
    void cmdDropToFloor();
    // 0x2D SET_HOME()
    void cmdSetHome();
    // 0x21 BILLBOARD / 0x22 HIDE / 0x35 DISABLE_RENDERING
    void cmdBillboard();
    void cmdHide();
    void cmdDisableRendering();
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
    // 0x2E SET_HURTBOX(radius, height)
    void cmdSetHurtbox();
    // 0x2F SET_INTERACT_TYPE / 0x31 SET_INTERACT_SUBTYPE
    void cmdSetInteractType();
    void cmdSetInteractSubtype();
    // 0x30 SET_OBJ_PHYSICS(8×s16)
    void cmdSetObjPhysics();
    // 0x32 SCALE(percent)
    void cmdScale(uint32_t word);
    // 0x36 SET_INT_UNUSED(field, value)（2 命令长）
    void cmdSetIntUnused(uint32_t word);
    // 其余命令：不改变静态导出所需状态，只推进
    void cmdAdvance(uint32_t length);

public:
    explicit BehaviorScriptVM(const SegmentTable &seg_table) : seg_table_(seg_table) {}

    // 走查 entry 处的行为脚本并作用于 obj（对象已由变换函数初始化）。floor 为
    // 当前区域的地形碰撞数据（DROP_TO_FLOOR 用；空则 DROP 无效）。对象按命令
    // 顺序被修改：位置/角度增量、DROP_TO_FLOOR、隐藏/缩放/模型覆盖、动画与
    // 碰撞数据地址、出生子对象列表等。
    void run(ObjectExtract::Object &obj, SegmentedAddress entry,
             const Collision::Data *floor = nullptr);

    // 脚本是否正常走查到底（在 DELAY/END_REPEAT/END_LOOP/BREAK/DEACTIVATE 等
    // 帧断点命令处结束；预算/越界/未知操作码中止时为 false）。
    bool ok() const { return ok_; }
};

} // namespace BehaviorScript

#endif /* BEHAVIOR_SCRIPT_H */
