#include "Level/dl_command.h"
#include "Math/math.h"

namespace GBI {

DecodedCommand decodeDLCommand(SegmentedAddress addr, const SegmentTable &seg_table) {
    DecodedCommand cmd;
    cmd.addr = addr;
    cmd.opcode = seg_table.read(addr);
    cmd.w0 = uint32_t(cmd.opcode) << 24;
    cmd.w1 = 0;
    for (int i = 0; i < 3; i++) {
        cmd.w0 |= uint32_t(seg_table.read(addr, 1 + i)) << (16 - 8 * i);
    }
    for (int i = 0; i < 4; i++) {
        cmd.w1 = (cmd.w1 << 8) | seg_table.read(addr, 4 + i);
    }
    return cmd;
}

Mtxf mtxfIdentity() {
    Mtxf m {};
    for (int i = 0; i < 4; i++) {
        m[i][i] = 1.0f;
    }
    return m;
}

Mtxf mtxfMul(const Mtxf &a, const Mtxf &b) {
    Mtxf out {};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a[i][k] * b[k][j];
            }
            out[i][j] = sum;
        }
    }
    return out;
}

Mtxf decodeMtx(SegmentedAddress addr, const SegmentTable &seg_table) {
    std::span<const uint8_t> d = seg_table.data(addr, 64);
    Mtxf m {};
    for (int k = 0; k < 16; k++) {
        // 布局：字节 0-31 = 各元素高 16 位，字节 32-63 = 低 16 位
        int16_t hi = readInt<int16_t>(d, 2 * k);
        int16_t lo = readInt<int16_t>(d, 32 + 2 * k);
        int32_t fixed = (int32_t(hi) << 16) | uint16_t(lo);
        m[k / 4][k % 4] = fixed / 65536.0f;
    }
    return m;
}

} // namespace GBI
