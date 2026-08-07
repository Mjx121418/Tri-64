#include "Level/dl_command.h"
#include "Math/math.h"

namespace GBI {

DecodedCommand CommandDecoder::decode(SegmentedAddress addr) const {
    DecodedCommand cmd;
    std::span<const uint8_t> d = seg_table_.data(addr, 8);
    cmd.addr = addr;
    cmd.opcode = readInt<uint8_t>(d, 0);
    cmd.w0 = readInt<uint32_t>(d, 0);
    cmd.w1 = readInt<uint32_t>(d, 4);
    return cmd;
}

Mtxf CommandDecoder::decodeMtx(SegmentedAddress addr) const {
    std::span<const uint8_t> d = seg_table_.data(addr, 64);
    Mtxf m {};
    for (int k = 0; k < 16; k++) {
        // 布局：字节 0-31 = 各元素高 16 位，字节 32-63 = 低 16 位
        uint16_t hi = readInt<uint16_t>(d, 2 * k);
        uint16_t lo = readInt<uint16_t>(d, 32 + 2 * k);
        int32_t fixed = static_cast<int32_t>((hi << 16) | lo);
        m[k / 4][k % 4] = fixed / 65536.0f;
    }
    return m;
}

} // namespace GBI
