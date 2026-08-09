#include "Scripts/behavior_script.h"

#include <cmath>
#include <cstdio>

namespace BehaviorScript {

namespace {

// Safety net: behavior scripts can loop forever in the game (BEGIN_LOOP /
// END_LOOP never terminate). A static walk must not, so any script running past
// this many commands is aborted.
constexpr int64_t MAX_INSTRUCTIONS = 100'000;

std::string formatAddress(int16_t seg, uint32_t offset) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%#04x:%#06x", static_cast<int>(seg), offset);
    return buf;
}

std::string formatOpcode(uint8_t opcode) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%#04x", opcode);
    return buf;
}

} // namespace

void BehaviorScriptVM::run(ObjectExtract::Object &obj, SegmentedAddress entry,
                           const Collision::Data *floor) {
    obj_ = &obj;
    floor_ = floor;
    seg_ = {};
    pc_ = entry.offset;
    stack_.clear();
    executed_ = 0;
    animate_seen_ = false;
    finished_ = false;
    ok_ = false;

    if (entry.seg < 0 || entry.seg > 31) {
        return;
    }

    // 取整段数据（seg 在 [0,31] 时 data({seg,0}) 不会抛异常；未加载的段是空
    // span），之后全部用显式越界检查读写，避免 SegmentTable::data 的 subspan
    // 在越界时抛异常。
    seg_ = seg_table_.data(SegmentedAddress { entry.seg, 0 });

    while (!finished_) {
        if (++executed_ > MAX_INSTRUCTIONS) {
            warnings_.addUnique(
                "behavior",
                "script at " + formatAddress(entry.seg, entry.offset) +
                    " did not terminate after " + std::to_string(executed_) +
                    " commands; script walk aborted");
            return;
        }

        const auto word_opt = readWord(pc_);
        if (!word_opt) {
            warnings_.addUnique(
                "behavior",
                "script at " + formatAddress(entry.seg, pc_) +
                    " is outside the loaded segment (size 0x" +
                    [&]() {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "%zX", seg_.size());
                        return std::string(buf);
                    }() + "); script walk aborted");
            return;
        }
        const uint32_t word = *word_opt;
        const uint8_t op = static_cast<uint8_t>(word >> 24);
        const uint32_t length = (op < kCommandLengths.size()) ? kCommandLengths[op] : 0;
        if (length == 0) {
            warnings_.addUnique(
                "behavior",
                "unknown opcode " + formatOpcode(op) + " at " +
                    formatAddress(entry.seg, pc_) + "; script walk aborted");
            return;
        }

        dispatch(word);
    }
}

std::optional<uint32_t> BehaviorScriptVM::readWord(uint32_t offset) const {
    if (static_cast<uint64_t>(offset) + 4 > seg_.size()) {
        return std::nullopt;
    }
    const uint8_t *p = seg_.data() + offset;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

int16_t BehaviorScriptVM::readS16(uint32_t rel, uint8_t sub) const {
    const uint32_t v = readWord(pc_ + rel).value_or(0);
    return sub == 0 ? static_cast<int16_t>(v >> 16) : static_cast<int16_t>(v & 0xFFFF);
}

std::optional<SegmentedAddress> BehaviorScriptVM::targetOf(uint32_t word_index) const {
    const auto raw = readWord(pc_ + word_index * 4);
    if (!raw) {
        return std::nullopt;
    }
    SegmentedAddress addr;
    addr.setAddress(*raw);
    return addr;
}

void BehaviorScriptVM::dispatch(uint32_t word) {
    const uint8_t op = static_cast<uint8_t>(word >> 24);
    switch (op) {
        case 0x00: cmdBegin(word); break;
        case 0x01:
        case 0x25: cmdDelay(); break;
        case 0x02: cmdCall(); break;
        case 0x03: cmdReturn(); break;
        case 0x04: cmdGoto(); break;
        case 0x05: cmdBeginRepeat(word, false); break;
        case 0x06: cmdEndRepeat(); break;
        case 0x07: cmdEndRepeatContinue(); break;
        case 0x08: cmdBeginLoop(); break;
        case 0x09: cmdEndLoop(); break;
        case 0x0A:
        case 0x0B: cmdBreak(); break;
        case 0x0C: cmdCallNative(); break;
        case 0x0D: cmdAddFloat(word); break;
        case 0x0E: cmdSetFloat(word); break;
        case 0x0F: cmdAddInt(word); break;
        case 0x10: cmdSetInt(word); break;
        case 0x11: cmdOrInt(word); break;
        case 0x12: cmdBitClear(word); break;
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17: cmdAdvance(kCommandLengths[op]); break; // 随机命令无法重现
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x24: cmdAdvance(1); break; // NOP
        case 0x1B: cmdSetModel(word); break;
        case 0x1C:
        case 0x29:
        case 0x2C: cmdSpawn(word); break;
        case 0x1D: cmdDeactivate(); break;
        case 0x1E: cmdDropToFloor(); break;
        case 0x1F: cmdSumFloat(word); break;
        case 0x20: cmdSumInt(word); break;
        case 0x21: cmdBillboard(); break;
        case 0x22: cmdHide(); break;
        case 0x23: cmdSetHitbox(); break;
        case 0x26: cmdBeginRepeat(word, true); break; // BEGIN_REPEAT_UNUSED（u8 计数）
        case 0x27: cmdLoadAnimations(); break;
        case 0x28: cmdAnimate(word); break;
        case 0x2A: cmdLoadCollisionData(); break;
        case 0x2B: cmdSetHitboxWithOffset(); break;
        case 0x2D: cmdSetHome(); break;
        case 0x2E: cmdSetHurtbox(); break;
        case 0x2F: cmdSetInteractType(); break;
        case 0x30: cmdSetObjPhysics(); break;
        case 0x31: cmdSetInteractSubtype(); break;
        case 0x32: cmdScale(word); break;
        case 0x33: cmdAdvance(2); break; // PARENT_BIT_CLEAR（父对象/Mario 粒子）
        case 0x34: cmdAdvance(1); break; // ANIMATE_TEXTURE（每帧生效，无 frame-0 效果）
        case 0x35: cmdDisableRendering(); break;
        case 0x36: cmdSetIntUnused(word); break;
        case 0x37: cmdAdvance(2); break; // SPAWN_WATER_DROPLET（运行时粒子）
        default: cmdAdvance(kCommandLengths[op]); break;
    }
}

void BehaviorScriptVM::cmdBegin(uint32_t word) {
    obj_->obj_list = static_cast<int8_t>((word >> 16) & 0xFF);
    pc_ += 4;
}

void BehaviorScriptVM::cmdDelay() {
    ok_ = true;
    finished_ = true;
}

void BehaviorScriptVM::cmdCall() {
    const auto target = targetOf(1);
    if (!target) {
        finished_ = true;
        return;
    }
    stack_.push_back(pc_ + 8);
    pc_ = target->offset;
}

void BehaviorScriptVM::cmdReturn() {
    if (stack_.empty()) {
        finished_ = true;
        return;
    }
    pc_ = stack_.back() & 0xFFFFFF;
    stack_.pop_back();
}

void BehaviorScriptVM::cmdGoto() {
    const auto target = targetOf(1);
    if (!target) {
        finished_ = true;
        return;
    }
    pc_ = target->offset;
}

void BehaviorScriptVM::cmdBeginRepeat(uint32_t word, bool unused) {
    const uint32_t count = unused ? ((word >> 16) & 0xFF) : (word & 0xFFFF);
    stack_.push_back(pc_ + 4);
    stack_.push_back(count);
    pc_ += 4;
}

void BehaviorScriptVM::cmdEndRepeat() {
    // 游戏里 0x06 END_REPEAT 返回 BHV_PROC_BREAK：循环体只在出生帧走一遍就停
    // 帧（后续命令是之后帧的），与 END_LOOP 的"出生状态"语义一致。
    ok_ = true;
    finished_ = true;
}

void BehaviorScriptVM::cmdEndRepeatContinue() {
    // 0x07 END_REPEAT_CONTINUE 返回 BHV_PROC_CONTINUE：同一帧把循环跑完，然后
    // 继续执行循环后的命令。
    if (stack_.size() < 2) {
        finished_ = true;
        return;
    }
    uint32_t count = stack_.back();
    stack_.pop_back();
    const uint32_t body = stack_.back();
    stack_.pop_back();
    if (count > 1) {
        stack_.push_back(body);
        stack_.push_back(count - 1);
        pc_ = body;
    } else {
        pc_ += 4;
    }
}

void BehaviorScriptVM::cmdBeginLoop() {
    stack_.push_back(pc_ + 4);
    pc_ += 4;
}

void BehaviorScriptVM::cmdEndLoop() {
    // 游戏里 0x09 END_LOOP 返回 BHV_PROC_BREAK：出生帧循环体只走一遍就停帧，
    // 之后每帧再走一遍。静态导出取出生帧状态，因此在第一个 END_LOOP 停止（循环
    // 体在前往这里的路上已经执行过一次）。
    if (stack_.empty()) {
        finished_ = true;
        return;
    }
    ok_ = true;
    finished_ = true;
}

void BehaviorScriptVM::cmdBreak() {
    ok_ = true;
    finished_ = true;
}

void BehaviorScriptVM::cmdDeactivate() {
    obj_->deactivated = true;
    ok_ = true;
    finished_ = true;
}

void BehaviorScriptVM::cmdCallNative() {
    pc_ += 8;
}

void BehaviorScriptVM::cmdAddFloat(uint32_t word) {
    obj_->f32((word >> 16) & 0xFF) += static_cast<float>(static_cast<int16_t>(word & 0xFFFF));
    pc_ += 4;
}

void BehaviorScriptVM::cmdSetFloat(uint32_t word) {
    obj_->f32((word >> 16) & 0xFF) = static_cast<float>(static_cast<int16_t>(word & 0xFFFF));
    pc_ += 4;
}

void BehaviorScriptVM::cmdAddInt(uint32_t word) {
    obj_->s32((word >> 16) & 0xFF) += static_cast<int16_t>(word & 0xFFFF);
    pc_ += 4;
}

void BehaviorScriptVM::cmdSetInt(uint32_t word) {
    obj_->s32((word >> 16) & 0xFF) = static_cast<int16_t>(word & 0xFFFF);
    pc_ += 4;
}

void BehaviorScriptVM::cmdOrInt(uint32_t word) {
    obj_->u32((word >> 16) & 0xFF) |= (word & 0xFFFF);
    pc_ += 4;
}

void BehaviorScriptVM::cmdBitClear(uint32_t word) {
    obj_->u32((word >> 16) & 0xFF) &= ~(word & 0xFFFF);
    pc_ += 4;
}

void BehaviorScriptVM::cmdSumFloat(uint32_t word) {
    const uint8_t dst = (word >> 16) & 0xFF;
    const uint8_t s1 = (word >> 8) & 0xFF;
    const uint8_t s2 = word & 0xFF;
    obj_->f32(dst) = obj_->f32(s1) + obj_->f32(s2);
    pc_ += 4;
}

void BehaviorScriptVM::cmdSumInt(uint32_t word) {
    const uint8_t dst = (word >> 16) & 0xFF;
    const uint8_t s1 = (word >> 8) & 0xFF;
    const uint8_t s2 = word & 0xFF;
    obj_->s32(dst) = obj_->s32(s1) + obj_->s32(s2);
    pc_ += 4;
}

void BehaviorScriptVM::cmdSetModel(uint32_t word) {
    obj_->model_id = static_cast<int16_t>(word & 0xFFFF);
    pc_ += 4;
}

void BehaviorScriptVM::cmdSpawn(uint32_t word) {
    // 0x1C SPAWN_CHILD / 0x29 SPAWN_CHILD_WITH_PARAM / 0x2C SPAWN_OBJ：
    // model 在 word1，behavior 在 word2；0x29 的 bhvParam 在 word0 低 s16。
    ObjectExtract::Object::ChildSpawn child;
    const uint8_t op = static_cast<uint8_t>(word >> 24);
    child.param = (op == 0x29) ? static_cast<int16_t>(word & 0xFFFF) : 0;
    if (const auto model = readWord(pc_ + 4)) {
        child.model = static_cast<int16_t>(*model & 0xFFFF);
    }
    if (const auto spawned = targetOf(2)) {
        child.behavior = *spawned;
    }
    obj_->spawned_children.push_back(child);
    pc_ += 12;
}

void BehaviorScriptVM::cmdDropToFloor() {
    // 镜像 bhv_cmd_drop_to_floor：find_floor_height(x, oPosY + 200, z) 落到脚下。
    if (floor_ != nullptr) {
        const float x = obj_->f32(ObjectExtract::F::PosX);
        const float y = obj_->f32(ObjectExtract::F::PosY);
        const float z = obj_->f32(ObjectExtract::F::PosZ);
        if (const auto floor = Collision::findFloorHeight(*floor_, x, y + 200.0f, z)) {
            obj_->f32(ObjectExtract::F::PosY) = *floor;
        }
    }
    pc_ += 4;
}

void BehaviorScriptVM::cmdSetHome() {
    obj_->f32(ObjectExtract::F::HomeX) = obj_->f32(ObjectExtract::F::PosX);
    obj_->f32(ObjectExtract::F::HomeY) = obj_->f32(ObjectExtract::F::PosY);
    obj_->f32(ObjectExtract::F::HomeZ) = obj_->f32(ObjectExtract::F::PosZ);
    pc_ += 4;
}

void BehaviorScriptVM::cmdBillboard() {
    obj_->billboard = true;
    pc_ += 4;
}

void BehaviorScriptVM::cmdHide() {
    obj_->invisible = true;
    pc_ += 4;
}

void BehaviorScriptVM::cmdDisableRendering() {
    obj_->active = false;
    pc_ += 4;
}

void BehaviorScriptVM::cmdSetHitbox() {
    obj_->hitbox_radius = readS16(4, 0);
    obj_->hitbox_height = readS16(4, 1);
    pc_ += 8;
}

void BehaviorScriptVM::cmdSetHitboxWithOffset() {
    obj_->hitbox_radius = readS16(4, 0);
    obj_->hitbox_height = readS16(4, 1);
    obj_->hitbox_down_offset = readS16(8, 0);
    pc_ += 12;
}

void BehaviorScriptVM::cmdLoadAnimations() {
    obj_->setAddr(ObjectExtract::F::Animations, targetOf(1).value_or(SegmentedAddress {}));
    pc_ += 8;
}

void BehaviorScriptVM::cmdAnimate(uint32_t word) {
    if (!animate_seen_) {
        obj_->animate_index = static_cast<int16_t>((word >> 16) & 0xFF);
        animate_seen_ = true;
    }
    pc_ += 4;
}

void BehaviorScriptVM::cmdLoadCollisionData() {
    obj_->collision_data = targetOf(1).value_or(SegmentedAddress {});
    pc_ += 8;
}

void BehaviorScriptVM::cmdSetHurtbox() {
    obj_->hurtbox_radius = static_cast<float>(readS16(4, 0));
    obj_->hurtbox_height = static_cast<float>(readS16(4, 1));
    pc_ += 8;
}

void BehaviorScriptVM::cmdSetInteractType() {
    obj_->u32(ObjectExtract::F::InteractType) = readWord(pc_ + 4).value_or(0);
    pc_ += 8;
}

void BehaviorScriptVM::cmdSetInteractSubtype() {
    obj_->u32(ObjectExtract::F::InteractionSubtype) = readWord(pc_ + 4).value_or(0);
    pc_ += 8;
}

void BehaviorScriptVM::cmdSetObjPhysics() {
    obj_->f32(ObjectExtract::F::WallHitboxRadius) = static_cast<float>(readS16(4, 0));
    obj_->f32(ObjectExtract::F::Gravity) = readS16(4, 1) / 100.0f;
    obj_->f32(ObjectExtract::F::Bounciness) = readS16(8, 0) / 100.0f;
    obj_->f32(ObjectExtract::F::DragStrength) = readS16(8, 1) / 100.0f;
    obj_->f32(ObjectExtract::F::Friction) = readS16(12, 0) / 100.0f;
    obj_->f32(ObjectExtract::F::Buoyancy) = readS16(12, 1) / 100.0f;
    pc_ += 20;
}

void BehaviorScriptVM::cmdScale(uint32_t word) {
    const int16_t percent = static_cast<int16_t>(word & 0xFFFF);
    const float s = percent / 100.0f;
    obj_->scale = {s, s, s};
    pc_ += 4;
}

void BehaviorScriptVM::cmdSetIntUnused(uint32_t word) {
    obj_->s32((word >> 16) & 0xFF) = readS16(4, 1);
    pc_ += 8;
}

void BehaviorScriptVM::cmdAdvance(uint32_t length) {
    pc_ += length * 4;
}

} // namespace BehaviorScript
