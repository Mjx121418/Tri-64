#ifndef SEGMENT_H
#define SEGMENT_H

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

enum class SegmentError {
    InvalidRomOffset,
    InvalidSegmentID,
    MIO0Error
};

struct Segment {
    std::span<uint8_t> data;
    bool is_compressed {false};
    std::vector<uint8_t> decompressed_data {};
};

// A raw ROM range mapped to an absolute RDRAM address by FIXED_LOAD. Segment 0
// has a fixed base at 0x80000000, so the range is addressable through seg-0
// offsets even though it is not part of the ordinary segment span.
struct FixedMemory {
    uint32_t segment0_offset {0};
    std::span<uint8_t> data;
};

struct SegmentedAddress {
    int16_t seg {-1};
    uint32_t offset {0};

    void setAddress(uint32_t seg_addr);
    bool isNull() const;
    void operator+=(const uint32_t step);
};

class SegmentTable {
    std::array<Segment, 32> segments {};
    std::vector<FixedMemory> fixed_memory_;

public:
    std::span<uint8_t> rom_span;
    uint8_t read(SegmentedAddress seg_addr) const;
    uint8_t read(SegmentedAddress seg_addr, uint32_t offset) const;
    std::span<uint8_t> data(SegmentedAddress seg_addr) const;
    std::span<uint8_t> data(SegmentedAddress seg_addr, uint32_t length) const;
    void loadSegment(int16_t seg, uint32_t rom_start, uint32_t rom_end);
    std::expected<void, SegmentError> loadMIO0Segment(int16_t seg, uint32_t rom_start, uint32_t rom_end);
    // Map a raw ROM range at an absolute RDRAM address. The data is kept as a
    // ROM-backed span; the ROM must outlive this segment table.
    bool loadFixedAddress(uint32_t dest_addr, uint32_t rom_start, uint32_t rom_end);
};

SegmentedAddress segAddress(uint32_t seg_addr);

#endif /* SEGMENT_H */
