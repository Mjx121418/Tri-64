#include "segment.h"
#include "Memory/mio0.h"

void SegmentedAddress::setAddress(uint32_t seg_addr) {
    seg = seg_addr >> 24;
    offset = seg_addr & 0x00FFFFFF;
}

SegmentedAddress segAddress(uint32_t seg_addr) {
    SegmentedAddress addr;
    addr.setAddress(seg_addr);
    return addr;
}

bool SegmentedAddress::isNull() const {
    return (seg < 0) || (seg > 31);
}

void SegmentedAddress::operator+=(uint32_t step) {
    offset += step;
}

uint8_t SegmentTable::read(SegmentedAddress seg_addr) const {
    return segments.at(seg_addr.seg).data.at(seg_addr.offset);
}

uint8_t SegmentTable::read(SegmentedAddress seg_addr, uint32_t offset) const {
    return segments.at(seg_addr.seg).data.at(seg_addr.offset+offset);
}

std::span<uint8_t> SegmentTable::data(SegmentedAddress seg_addr) const {
    const auto &seg = segments.at(seg_addr.seg).data;
    if (seg_addr.offset > seg.size()) {
        throw std::out_of_range("SegmentTable::data offset out of range");
    }
    return seg.subspan(seg_addr.offset);
}

std::span<uint8_t> SegmentTable::data(SegmentedAddress seg_addr, uint32_t length) const {
    const auto &seg = segments.at(seg_addr.seg).data;
    if (seg_addr.offset > seg.size() || length > seg.size() - seg_addr.offset) {
        throw std::out_of_range("SegmentTable::data range out of bounds");
    }
    return seg.subspan(seg_addr.offset, length);
}

void SegmentTable::loadSegment(int16_t seg, uint32_t rom_start, uint32_t rom_end) {
    if (seg < 0 || seg > 31) {
        printf("Invalid segmentation %d\n", seg);
        return;
    }
    
    segments[seg].is_compressed = false;
    segments[seg].decompressed_data.clear();
    // 防御性边界检查：hack 的 LOAD_RAW 命令可能越界（否则 span 会指向
    // 越界内存，后续读取导致未定义行为/崩溃）。
    if (rom_start > rom_span.size() || rom_end > rom_span.size() || rom_start > rom_end) {
        printf("loadSegment: invalid range seg=%d rom=0x%x-0x%x (rom size 0x%zx)\n", seg,
               rom_start, rom_end, rom_span.size());
        segments[seg].data = {};
        return;
    }
    segments[seg].data = std::span<uint8_t>(rom_span.begin() + rom_start, rom_span.begin() + rom_end);
}

std::expected<void, SegmentError> SegmentTable::loadMIO0Segment(int16_t seg, uint32_t rom_start, uint32_t rom_end) {
    if (seg < 0 || seg > 31) {
        printf("Invalid segmentation %d\n", seg);
        return std::unexpected(SegmentError::InvalidSegmentID);
    }

    if (rom_start > rom_span.size() || rom_end > rom_span.size() || rom_start > rom_end) {
        return std::unexpected(SegmentError::InvalidRomOffset);
    }

    std::span<uint8_t> mio0_data {rom_span.begin()+rom_start, rom_span.begin()+rom_end};
    MIO0 decoder;

    if (!decoder.load(mio0_data)) {
        return std::unexpected(SegmentError::MIO0Error);
    }

    auto decompressed_data {decoder.decompressMIO0()};

    if (!decompressed_data) {
        return std::unexpected(SegmentError::MIO0Error);
    }

    segments[seg].is_compressed = true;
    segments[seg].decompressed_data = decompressed_data.value();
    // 注意：span 必须指向成员 vector（局部 decompressed_data 在函数返回后销毁）
    segments[seg].data = std::span(segments[seg].decompressed_data);

    return {};
}
