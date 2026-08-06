#include "Scripts/behavior_script.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>

namespace BehaviorScript {

namespace {

// Safety net: behavior scripts can loop forever in the game (BEGIN_LOOP /
// END_LOOP never terminate). A static walk must not, so any script running past
// this many commands is aborted.
constexpr int64_t MAX_INSTRUCTIONS = 100'000;

// 每个操作码的命令长度（u32 命令数）。镜像 decomp 的 behavior_data.h 宏。
// 未列出的操作码视为未知（长度 0），解释器会在 dispatch 时中止。
constexpr std::array<uint8_t, 0x38> kCommandLengths {
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

// 值栈：与 decomp 一致，返回地址和 BEGIN_REPEAT 的计数共用同一个栈。
// 地址存完整的分段地址（seg << 24 | offset），跳转目标可能指向其他段。
using BhvStack = std::vector<uint32_t>;

// 读一个 BE32 命令。越界返回 nullopt（脚本走查中止而非抛异常）。
std::optional<uint32_t> readWord(const std::span<uint8_t> &seg, uint32_t offset) {
    if (static_cast<uint64_t>(offset) + 4 > seg.size()) {
        return std::nullopt;
    }
    const uint8_t *p = seg.data() + offset;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

} // namespace

Info analyze(const SegmentTable &seg_table, SegmentedAddress entry) {
    Info info;
    if (entry.seg < 0 || entry.seg > 31) {
        return info;
    }

    // 取整段数据（seg 在 [0,31] 时 data({seg,0}) 不会抛异常；未加载的段是空
    // span），之后全部用显式越界检查读写，避免 SegmentTable::data 的 subspan
    // 在越界时抛异常。
    const std::span<uint8_t> seg = seg_table.data(SegmentedAddress { entry.seg, 0 });

    BhvStack stack;
    uint32_t offset = entry.offset;
    int64_t executed = 0;
    int64_t loop_passes = 0;
    bool animate_seen = false; // 出生即播放的动画 = 程序顺序上第一个 ANIMATE

    while (true) {
        if (++executed > MAX_INSTRUCTIONS) {
            fprintf(stderr, "BehaviorScript: script at seg %#04x off %#06x did not "
                            "terminate after %lld commands, aborting.\n",
                    entry.seg, entry.offset, (long long)executed);
            return info;
        }

        const auto word_opt = readWord(seg, offset);
        if (!word_opt) {
            fprintf(stderr, "BehaviorScript: out of bounds at seg %#04x off %#06x\n",
                    entry.seg, offset);
            return info;
        }
        const uint32_t word = *word_opt;
        const uint8_t op = static_cast<uint8_t>(word >> 24);
        const uint32_t length = (op < kCommandLengths.size()) ? kCommandLengths[op] : 0;
        if (length == 0) {
            fprintf(stderr, "BehaviorScript: unknown opcode %#04x at seg %#04x off %#06x\n",
                    op, entry.seg, offset);
            return info;
        }

        // 分段地址参数（CALL/GOTO/LOAD_*/SPAWN_* 的目标）。
        const auto targetOf = [&](uint32_t word_index) -> std::optional<SegmentedAddress> {
            const auto raw = readWord(seg, offset + word_index * 4);
            if (!raw) {
                return std::nullopt;
            }
            SegmentedAddress addr;
            addr.setAddress(*raw);
            return addr;
        };

        switch (op) {
            // 0x00 BEGIN(objList): BC_BB(0x00, objList)，objList 在字节 1
            case 0x00:
                info.obj_list = static_cast<int16_t>((word >> 16) & 0xFF);
                offset += 4;
                break;

            // 0x01 DELAY / 0x25 DELAY_VAR: 出生帧脚本停在第一个 DELAY（返回
            // BHV_PROC_BREAK），之后的命令不是出生状态，终止走查。
            case 0x01:
            case 0x25:
                info.ok = true;
                return info;

            // 0x02 CALL(addr): 压入返回地址后跳转
            case 0x02: {
                const auto target = targetOf(1);
                if (!target) {
                    return info;
                }
                stack.push_back((uint32_t(entry.seg) << 24) | (offset + 8));
                offset = target->offset;
                break;
            }

            // 0x03 RETURN()
            case 0x03:
                if (stack.empty()) {
                    return info;
                }
                offset = stack.back() & 0xFFFFFF;
                stack.pop_back();
                break;

            // 0x04 GOTO(addr)
            case 0x04: {
                const auto target = targetOf(1);
                if (!target) {
                    return info;
                }
                offset = target->offset;
                break;
            }

            // 0x05 BEGIN_REPEAT(count)
            case 0x05: {
                const uint32_t count = word & 0xFFFF;
                stack.push_back(offset + 4);
                stack.push_back(count);
                offset += 4;
                break;
            }

            // 0x06 END_REPEAT() / 0x07 END_REPEAT_CONTINUE()
            case 0x06:
            case 0x07: {
                if (stack.size() < 2) {
                    return info;
                }
                uint32_t count = stack.back();
                stack.pop_back();
                const uint32_t body = stack.back();
                stack.pop_back();
                if (count > 1) {
                    stack.push_back(body);
                    stack.push_back(count - 1);
                    offset = body;
                } else {
                    offset += 4;
                }
                break;
            }

            // 0x08 BEGIN_LOOP()
            case 0x08:
                stack.push_back(offset + 4);
                offset += 4;
                break;

            // 0x09 END_LOOP(): 循环体只走一遍（静态导出不需要无限展开）。
            // 游戏里 END_LOOP 永远跳回循环体（每帧中断于此），循环体之后的
            // 字节是相邻行为/填充，不是本脚本可达的代码，因此退出循环即终止。
            case 0x09: {
                if (stack.empty()) {
                    return info;
                }
                const uint32_t body = stack.back();
                if (loop_passes++ < 1) {
                    offset = body;
                } else {
                    info.ok = true;
                    return info;
                }
                break;
            }

            // 0x0A BREAK / 0x0B BREAK_UNUSED / 0x1D DEACTIVATE: 脚本终止
            case 0x0A:
            case 0x0B:
            case 0x1D:
                info.ok = true;
                return info;

            // 0x0C CALL_NATIVE(func): 原生函数无法执行，跳过
            case 0x0C:
                offset += 8;
                break;

            // 0x1B SET_MODEL(modelID)
            case 0x1B:
                info.model_id = static_cast<int16_t>(word & 0xFFFF);
                offset += 4;
                break;

            // 0x21 BILLBOARD / 0x22 HIDE / 0x35 DISABLE_RENDERING
            case 0x21:
                info.billboard = true;
                offset += 4;
                break;
            case 0x22:
            case 0x35:
                info.hidden = true;
                offset += 4;
                break;

            // 0x23 SET_HITBOX(radius, height)
            case 0x23: {
                const uint32_t rh = readWord(seg, offset + 4).value_or(0);
                info.hitbox_radius = static_cast<int16_t>(rh >> 16);
                info.hitbox_height = static_cast<int16_t>(rh & 0xFFFF);
                offset += 8;
                break;
            }

            // 0x2B SET_HITBOX_WITH_OFFSET(radius, height, downOffset)
            case 0x2B: {
                const uint32_t rh = readWord(seg, offset + 4).value_or(0);
                const uint32_t down = readWord(seg, offset + 8).value_or(0);
                info.hitbox_radius = static_cast<int16_t>(rh >> 16);
                info.hitbox_height = static_cast<int16_t>(rh & 0xFFFF);
                info.hitbox_down_offset = static_cast<int16_t>(down >> 16);
                offset += 12;
                break;
            }

            // 0x27 LOAD_ANIMATIONS(field, anims)
            case 0x27:
                info.animations = targetOf(1).value_or(SegmentedAddress {});
                offset += 8;
                break;

            // 0x28 ANIMATE(animIndex): 只记录第一个（出生即播放的动画）
            case 0x28:
                if (!animate_seen) {
                    info.animate_index = static_cast<int16_t>((word >> 16) & 0xFF);
                    animate_seen = true;
                }
                offset += 4;
                break;

            // 0x2A LOAD_COLLISION_DATA(collisionData)
            case 0x2A:
                info.collision_data = targetOf(1).value_or(SegmentedAddress {});
                offset += 8;
                break;

            // 0x1C SPAWN_CHILD / 0x29 SPAWN_CHILD_WITH_PARAM / 0x2C SPAWN_OBJ
            case 0x1C:
            case 0x29:
            case 0x2C:
                if (const auto spawned = targetOf(2)) {
                    info.spawned_behaviors.push_back(*spawned);
                }
                offset += 12;
                break;

            // 0x32 SCALE(percent)
            case 0x32:
                info.scale_seen = true;
                info.scale_percent = static_cast<int16_t>(word & 0xFFFF);
                offset += 4;
                break;

            // 其余命令：不改变静态导出所需状态，只推进
            default:
                offset += length * 4;
                break;
        }
    }
}

} // namespace BehaviorScript
