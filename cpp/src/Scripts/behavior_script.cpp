#include "Scripts/behavior_script.h"

#include <cstdio>

namespace BehaviorScript {

namespace {

// Safety net: behavior scripts can loop forever in the game (BEGIN_LOOP /
// END_LOOP never terminate). A static walk must not, so any script running past
// this many commands is aborted.
constexpr int64_t MAX_INSTRUCTIONS = 100'000;

} // namespace

void BehaviorScriptVM::run(SegmentedAddress entry) {
    info_ = {};
    seg_ = {};
    pc_ = entry.offset;
    stack_.clear();
    executed_ = 0;
    loop_passes_ = 0;
    animate_seen_ = false;
    finished_ = false;

    if (entry.seg < 0 || entry.seg > 31) {
        return;
    }

    // 取整段数据（seg 在 [0,31] 时 data({seg,0}) 不会抛异常；未加载的段是空
    // span），之后全部用显式越界检查读写，避免 SegmentTable::data 的 subspan
    // 在越界时抛异常。
    seg_ = seg_table_.data(SegmentedAddress { entry.seg, 0 });

    while (!finished_) {
        if (++executed_ > MAX_INSTRUCTIONS) {
            fprintf(stderr, "BehaviorScript: script at seg %#04x off %#06x did not "
                            "terminate after %lld commands, aborting.\n",
                    entry.seg, entry.offset, (long long)executed_);
            return;
        }

        const auto word_opt = readWord(pc_);
        if (!word_opt) {
            fprintf(stderr, "BehaviorScript: out of bounds at seg %#04x off %#06x\n",
                    entry.seg, pc_);
            return;
        }
        const uint32_t word = *word_opt;
        const uint8_t op = static_cast<uint8_t>(word >> 24);
        const uint32_t length = (op < kCommandLengths.size()) ? kCommandLengths[op] : 0;
        if (length == 0) {
            fprintf(stderr, "BehaviorScript: unknown opcode %#04x at seg %#04x off %#06x\n",
                    op, entry.seg, pc_);
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
        case 0x05: cmdBeginRepeat(word); break;
        case 0x06:
        case 0x07: cmdEndRepeat(); break;
        case 0x08: cmdBeginLoop(); break;
        case 0x09: cmdEndLoop(); break;
        case 0x0A:
        case 0x0B:
        case 0x1D: cmdBreak(); break;
        case 0x0C: cmdCallNative(); break;
        case 0x0E: cmdSetFloat(word); break;
        case 0x10: cmdSetInt(word, false); break;
        case 0x11: cmdSetInt(word, true); break;
        case 0x1B: cmdSetModel(word); break;
        case 0x21: cmdBillboard(); break;
        case 0x22:
        case 0x35: cmdHide(); break;
        case 0x23: cmdSetHitbox(); break;
        case 0x2B: cmdSetHitboxWithOffset(); break;
        case 0x27: cmdLoadAnimations(); break;
        case 0x28: cmdAnimate(word); break;
        case 0x2A: cmdLoadCollisionData(); break;
        case 0x2E: cmdSetHurtbox(); break;
        case 0x2F: cmdSetInteractType(); break;
        case 0x30: cmdSetObjPhysics(); break;
        case 0x31: cmdSetInteractSubtype(); break;
        case 0x34: cmdAnimateTexture(word); break;
        case 0x37: cmdSpawnWaterDroplet(); break;
        case 0x1C:
        case 0x29:
        case 0x2C: cmdSpawn(); break;
        case 0x32: cmdScale(word); break;
        default: cmdAdvance(kCommandLengths[op]); break;
    }
}

void BehaviorScriptVM::cmdBegin(uint32_t word) {
    info_.obj_list = static_cast<int16_t>((word >> 16) & 0xFF);
    pc_ += 4;
}

void BehaviorScriptVM::cmdDelay() {
    info_.ok = true;
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

void BehaviorScriptVM::cmdBeginRepeat(uint32_t word) {
    const uint32_t count = word & 0xFFFF;
    stack_.push_back(pc_ + 4);
    stack_.push_back(count);
    pc_ += 4;
}

void BehaviorScriptVM::cmdEndRepeat() {
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
    if (stack_.empty()) {
        finished_ = true;
        return;
    }
    const uint32_t body = stack_.back();
    if (loop_passes_++ < 1) {
        pc_ = body;
    } else {
        info_.ok = true;
        finished_ = true;
    }
}

void BehaviorScriptVM::cmdBreak() {
    info_.ok = true;
    finished_ = true;
}

void BehaviorScriptVM::cmdCallNative() {
    pc_ += 8;
}

void BehaviorScriptVM::cmdSetModel(uint32_t word) {
    info_.model_id = static_cast<int16_t>(word & 0xFFFF);
    pc_ += 4;
}

void BehaviorScriptVM::cmdBillboard() {
    info_.billboard = true;
    pc_ += 4;
}

void BehaviorScriptVM::cmdHide() {
    info_.hidden = true;
    pc_ += 4;
}

void BehaviorScriptVM::cmdSetHitbox() {
    const uint32_t rh = readWord(pc_ + 4).value_or(0);
    info_.hitbox_radius = static_cast<int16_t>(rh >> 16);
    info_.hitbox_height = static_cast<int16_t>(rh & 0xFFFF);
    pc_ += 8;
}

void BehaviorScriptVM::cmdSetHitboxWithOffset() {
    const uint32_t rh = readWord(pc_ + 4).value_or(0);
    const uint32_t down = readWord(pc_ + 8).value_or(0);
    info_.hitbox_radius = static_cast<int16_t>(rh >> 16);
    info_.hitbox_height = static_cast<int16_t>(rh & 0xFFFF);
    info_.hitbox_down_offset = static_cast<int16_t>(down >> 16);
    pc_ += 12;
}

void BehaviorScriptVM::cmdLoadAnimations() {
    info_.animations = targetOf(1).value_or(SegmentedAddress {});
    pc_ += 8;
}

void BehaviorScriptVM::cmdAnimate(uint32_t word) {
    if (!animate_seen_) {
        info_.animate_index = static_cast<int16_t>((word >> 16) & 0xFF);
        animate_seen_ = true;
    }
    pc_ += 4;
}

void BehaviorScriptVM::cmdLoadCollisionData() {
    info_.collision_data = targetOf(1).value_or(SegmentedAddress {});
    pc_ += 8;
}

void BehaviorScriptVM::cmdSpawn() {
    // SPAWN_OBJ(modelID, behavior) / SPAWN_CHILD(modelID, behavior) /
    // SPAWN_CHILD_WITH_PARAM(bhvParam, modelID, behavior)：模型 id 在 word1，
    // 行为在 word2。
    if (const auto model = readWord(pc_ + 4)) {
        info_.spawned_models.push_back(static_cast<int16_t>(*model & 0xFFFF));
    }
    if (const auto spawned = targetOf(2)) {
        info_.spawned_behaviors.push_back(*spawned);
    }
    pc_ += 12;
}

void BehaviorScriptVM::cmdSetHurtbox() {
    const uint32_t rh = readWord(pc_ + 4).value_or(0);
    info_.hurtbox_radius = static_cast<int16_t>(rh >> 16);
    info_.hurtbox_height = static_cast<int16_t>(rh & 0xFFFF);
    pc_ += 8;
}

void BehaviorScriptVM::cmdSetInteractType() {
    info_.interact_type = readWord(pc_ + 4).value_or(0);
    pc_ += 8;
}

void BehaviorScriptVM::cmdSetInteractSubtype() {
    info_.interact_subtype = readWord(pc_ + 4).value_or(0);
    pc_ += 8;
}

void BehaviorScriptVM::cmdSetObjPhysics() {
    info_.physics_seen = true;
    for (int i = 0; i < 4; i++) {
        const uint32_t v = readWord(pc_ + 4 + static_cast<uint32_t>(i) * 4).value_or(0);
        info_.physics[i * 2] = static_cast<int16_t>(v >> 16);
        info_.physics[i * 2 + 1] = static_cast<int16_t>(v & 0xFFFF);
    }
    pc_ += 20;
}

void BehaviorScriptVM::cmdAnimateTexture(uint32_t word) {
    info_.animate_texture_rate = static_cast<int16_t>(word & 0xFFFF);
    pc_ += 4;
}

void BehaviorScriptVM::cmdSpawnWaterDroplet() {
    if (const auto p = readWord(pc_ + 4)) {
        info_.water_droplet_params.setAddress(*p);
    }
    pc_ += 8;
}

void BehaviorScriptVM::cmdSetFloat(uint32_t word) {
    // SET_FLOAT(field, value)：field 是对象字段索引（object_fields.h）。
    const uint32_t field = (word >> 16) & 0xFF;
    const int32_t value = static_cast<int16_t>(word & 0xFFFF);
    info_.set_float_fields[static_cast<uint8_t>(field)] = value;
    pc_ += 4;
}

void BehaviorScriptVM::cmdSetInt(uint32_t word, bool or_op) {
    // SET_INT/OR_INT(field, value)：记录到字段写入全集（OR_INT 按字段累积）。
    const uint32_t field = (word >> 16) & 0xFF;
    const uint32_t value = word & 0xFFFF;
    auto &v = info_.set_int_fields[static_cast<uint8_t>(field)];
    v = or_op ? (v | value) : static_cast<int32_t>(value);
    pc_ += 4;
}

void BehaviorScriptVM::cmdScale(uint32_t word) {
    info_.scale_seen = true;
    info_.scale_percent = static_cast<int16_t>(word & 0xFFFF);
    pc_ += 4;
}

void BehaviorScriptVM::cmdAdvance(uint32_t length) {
    pc_ += length * 4;
}

} // namespace BehaviorScript
