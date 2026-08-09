#include "ASM/mop_loader.h"

#include <cstddef>

namespace {

// The main segment begins at this runtime address in the US SM64 layout. See
// decomp/sm64/asm/entry.s and the main segment linker placement.
constexpr uint32_t kMainCodeBase = 0x80246000;
constexpr size_t kMOPLoaderSize = 0x2C;

uint32_t readInstruction(std::span<const uint8_t> code, size_t offset) {
    return (static_cast<uint32_t>(code[offset]) << 24)
         | (static_cast<uint32_t>(code[offset + 1]) << 16)
         | (static_cast<uint32_t>(code[offset + 2]) << 8)
         | static_cast<uint32_t>(code[offset + 3]);
}

bool isLui(uint32_t instruction, uint8_t reg) {
    return (instruction & 0xFFFF0000) == (0x3C000000 | (static_cast<uint32_t>(reg) << 16));
}

uint32_t luiValue(uint32_t instruction) {
    return (instruction & 0xFFFF) << 16;
}

} // namespace

namespace ASM {

std::optional<MOP123Load> detectMOP123Loader(const SegmentTable &seg_table,
                                              uint32_t function_address) {
    if (function_address < kMainCodeBase) {
        return std::nullopt;
    }

    const uint32_t main_offset = function_address - kMainCodeBase;
    std::span<const uint8_t> code;
    try {
        code = seg_table.data(SegmentedAddress {0, main_offset}, kMOPLoaderSize);
    } catch (const std::out_of_range &) {
        return std::nullopt;
    }

    // MOP1-3's native loader (MIPS, big-endian in the ROM):
    //   addiu sp, sp, -0x18
    //   sw    ra, 0x14(sp)
    //   lui   a0, 0x805f       // fixed RDRAM destination
    //   lui   a1, 0x007d       // ROM start
    //   jal   dma/load helper
    //   lui   a2, 0x0080       // ROM end
    //   addiu v0, zero, 0x10
    //   ...
    //   lw    ra, 0x14(sp)
    //   jr    ra
    //   addiu sp, sp, 0x18
    if (readInstruction(code, 0x00) != 0x27BDFFE8
        || readInstruction(code, 0x04) != 0xAFBF0014
        || !isLui(readInstruction(code, 0x08), 4)
        || !isLui(readInstruction(code, 0x0C), 5)
        || (readInstruction(code, 0x10) & 0xFC000000) != 0x0C000000
        || !isLui(readInstruction(code, 0x14), 6)
        || readInstruction(code, 0x18) != 0x24020010
        || readInstruction(code, 0x20) != 0x8FBF0014
        || readInstruction(code, 0x24) != 0x03E00008
        || readInstruction(code, 0x28) != 0x27BD0018) {
        return std::nullopt;
    }

    const uint32_t destination = luiValue(readInstruction(code, 0x08));
    const uint32_t rom_start = luiValue(readInstruction(code, 0x0C));
    const uint32_t rom_end = luiValue(readInstruction(code, 0x14));
    if (destination != 0x805F0000 || rom_start != 0x007D0000 || rom_end != 0x00800000) {
        return std::nullopt;
    }

    return MOP123Load {destination, rom_start, rom_end, 0x10};
}

bool applyMOP123Load(SegmentTable &seg_table, const MOP123Load &load) {
    return seg_table.loadFixedAddress(load.destination, load.rom_start, load.rom_end);
}

} // namespace ASM
