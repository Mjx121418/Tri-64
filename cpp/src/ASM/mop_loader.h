#ifndef ASM_MOP_LOADER_H
#define ASM_MOP_LOADER_H

#include "Memory/segment.h"

#include <cstdint>
#include <optional>

namespace ASM {

// The small native loader used by the MOP1-3 ROM patch. Its function body
// contains the source/destination constants for the extra model bank.
struct MOP123Load {
    uint32_t destination {0};
    uint32_t rom_start {0};
    uint32_t rom_end {0};
    int32_t return_value {0};
};

// Inspect a native function referenced by a level-script CALL. The main code
// is loaded into segment 0 at its main-segment offset, while native addresses
// use the runtime main-code base from asm/entry.s.
std::optional<MOP123Load> detectMOP123Loader(const SegmentTable &seg_table,
                                              uint32_t function_address);

// Apply a previously recognized MOP fixed-memory load.
bool applyMOP123Load(SegmentTable &seg_table, const MOP123Load &load);

} // namespace ASM

#endif /* ASM_MOP_LOADER_H */
