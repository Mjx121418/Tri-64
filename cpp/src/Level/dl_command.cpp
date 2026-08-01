#include "Level/dl_command.h"

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

} // namespace GBI
