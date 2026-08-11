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

Fast3D::FixedMatrix CommandDecoder::decodeFixedMtx(SegmentedAddress addr) const {
    return *Fast3D::decodeMatrix(seg_table_.data(addr, Fast3D::kMatrixBytes));
}

Mtxf CommandDecoder::decodeMtx(SegmentedAddress addr) const {
    const Fast3D::FixedMatrix fixed = decodeFixedMtx(addr);
    Mtxf m {};
    for (size_t row = 0; row < m.size(); row++) {
        for (size_t column = 0; column < m[row].size(); column++) {
            m[row][column] = static_cast<float>(fixed[row][column])
                / static_cast<float>(Fast3D::kFixedOne);
        }
    }
    return m;
}

} // namespace GBI
