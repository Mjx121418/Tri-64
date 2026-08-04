#ifndef MIO0_H
#define MIO0_H

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

const uint32_t mio0_signature = ('M' << 24) + ('I' << 16) + ('O' << 8) + '0';

enum class MIO0Error {
    IncorrectSignature,
    TooSmall,
    CorruptedData
};

struct MIO0Header {
    uint32_t signature;
    uint32_t decompressed_length;
    uint32_t compressed_offset;
    uint32_t uncompressed_offset;
};

class MIO0 {
    MIO0Header header;
    std::span<uint8_t> layout_bits;
    std::span<uint8_t> compressed_data;
    std::span<uint8_t> uncompressed_data;

public:
    std::expected<void, MIO0Error> load(std::span<uint8_t> data);
    std::expected<std::vector<uint8_t>, MIO0Error> decompressMIO0();
};

#endif /* MIO0_H */
