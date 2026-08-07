#include "Scripts/movtex.h"

#include "Math/math.h"
#include <cstdio>
#include <set>
#include <span>

namespace Movtex {

namespace {

// 每个 MovtexQuad 的 s16 数（moving_texture.c struct MovtexQuad）。
constexpr int32_t kQuadS16 = 14;
// 单次集合扫描的最大条目数（防误报）。
constexpr int32_t kMaxCollectionEntries = 32;
// 单个 quadArray 的最大四边形数。
constexpr int32_t kMaxQuads = 8;

int16_t readS16(const std::span<const uint8_t> &seg, size_t off) {
    return readInt<int16_t>(seg, off);
}

uint32_t readU32(const std::span<const uint8_t> &seg, size_t off) {
    return readInt<uint32_t>(seg, off);
}

// 解析 offset 处的一个 quadArray（numLists + numLists×Quad），内容校验通过才
// 接受（texture_id ∈ [0,7]、alpha ∈ [0,255]），避免随机数据误报。
bool parseQuadArray(const std::span<const uint8_t> &seg, size_t offset, int16_t id,
                    std::vector<Quad> &out) {
    if (offset + 2 > seg.size()) {
        return false;
    }
    const int32_t num = readS16(seg, offset);
    if (num <= 0 || num > kMaxQuads) {
        return false;
    }
    const size_t base = offset + 2;
    for (int32_t q = 0; q < num; q++) {
        const size_t qoff = base + static_cast<size_t>(q) * kQuadS16 * 2;
        if (qoff + kQuadS16 * 2 > seg.size()) {
            return false;
        }
        Quad quad;
        quad.id = id;
        quad.rot = readS16(seg, qoff + 0);
        quad.rot_speed = readS16(seg, qoff + 2);
        quad.scale = readS16(seg, qoff + 4);
        quad.x1 = readS16(seg, qoff + 6);
        quad.z1 = readS16(seg, qoff + 8);
        quad.x2 = readS16(seg, qoff + 10);
        quad.z2 = readS16(seg, qoff + 12);
        quad.x3 = readS16(seg, qoff + 14);
        quad.z3 = readS16(seg, qoff + 16);
        quad.x4 = readS16(seg, qoff + 18);
        quad.z4 = readS16(seg, qoff + 20);
        quad.rot_dir = readS16(seg, qoff + 22);
        quad.alpha = readS16(seg, qoff + 24);
        quad.texture_id = readS16(seg, qoff + 26);
        // 内容校验：纹理 id 与透明度必须是合理值（随机数据几乎不可能全通过）。
        if (quad.texture_id < 0 || quad.texture_id > 7 || quad.alpha < 0 || quad.alpha > 255) {
            return false;
        }
        out.push_back(quad);
    }
    return true;
}

} // namespace

void MovtexDecoder::run(const std::vector<int16_t> &segments_to_scan) {
    data_ = {};

    for (const int16_t seg_id : segments_to_scan) {
        std::span<const uint8_t> seg;
        try {
            seg = seg_table_.data(SegmentedAddress { seg_id, 0 });
        } catch (const std::out_of_range &) {
            continue;
        }
        if (seg.empty()) {
            continue;
        }

        // 扫描 {s16 id, s32 ptr}：id ∈ [0,255]，ptr 指向本段，quadArray 内容
        // 校验通过。命中后沿集合读后续条目（id 连续、ptr 有效）直到 id=-1。
        for (size_t off = 0; off + 6 <= seg.size(); off += 2) {
            const int16_t id0 = readS16(seg, off);
            if (id0 < 0 || id0 > 255) {
                continue;
            }
            const uint32_t ptr0 = readU32(seg, off + 2);
            const SegmentedAddress qa0 = segAddress(ptr0);
            if (qa0.seg != seg_id) {
                continue;
            }
            std::vector<Quad> quads;
            if (!parseQuadArray(seg, qa0.offset, id0, quads)) {
                continue;
            }
            // 收集后续条目（6 字节/条，id 连续）直到 -1 哨兵。
            std::vector<Quad> collection = quads;
            int16_t expected_id = static_cast<int16_t>(id0 + 1);
            bool terminated = false;
            for (int32_t e = 1; e < kMaxCollectionEntries; e++) {
                const size_t entry_off = off + static_cast<size_t>(e) * 6;
                if (entry_off + 6 > seg.size()) {
                    break;
                }
                const int16_t eid = readS16(seg, entry_off);
                if (eid == -1) { // -1 哨兵（ptr 应为 NULL）
                    const uint32_t eptr = readU32(seg, entry_off + 2);
                    if (eptr == 0) {
                        terminated = true;
                    }
                    break;
                }
                if (eid != expected_id) {
                    break;
                }
                const uint32_t eptr = readU32(seg, entry_off + 2);
                const SegmentedAddress eqa = segAddress(eptr);
                if (eqa.seg != seg_id) {
                    break;
                }
                std::vector<Quad> eq;
                if (!parseQuadArray(seg, eqa.offset, eid, eq)) {
                    break;
                }
                collection.insert(collection.end(), eq.begin(), eq.end());
                expected_id++;
            }
            // 只接受以 -1 哨兵结束、且至少含一个四边形的集合。
            if (!terminated || collection.empty()) {
                continue;
            }
            data_.quads.insert(data_.quads.end(), collection.begin(), collection.end());
            // 跳过已识别的集合（避免在条目内部重复命中）
            off += static_cast<size_t>(expected_id - id0) * 6 - 2;
        }
    }

    data_.ok = true;
}

} // namespace Movtex
