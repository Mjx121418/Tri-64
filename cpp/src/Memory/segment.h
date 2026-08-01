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

struct SegmentedAddress {
    int16_t seg {-1};
    uint32_t offset {0};

    void setAddress(uint32_t seg_addr);
    bool isNull();
    void operator+=(const uint32_t step);
};

class SegmentTable {
    std::array<Segment, 32> segments {};

public:
    std::span<uint8_t> rom_span;
    uint8_t read(SegmentedAddress seg_addr) const;
    uint8_t read(SegmentedAddress seg_addr, uint32_t offset) const;
    std::span<uint8_t> data(SegmentedAddress seg_addr) const;
    std::span<uint8_t> data(SegmentedAddress seg_addr, uint32_t length) const;
    void loadSegment(int16_t seg, uint32_t rom_start, uint32_t rom_end);
    std::expected<void, SegmentError> loadMIO0Segment(int16_t seg, uint32_t rom_start, uint32_t rom_end);
};

SegmentedAddress segAddress(uint32_t seg_addr);

#endif /* SEGMENT_H */
