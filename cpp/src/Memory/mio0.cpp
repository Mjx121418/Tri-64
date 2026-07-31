#include "mio0.h"
#include "Math/math.h"

std::expected<void, MIO0Error> MIO0::load(std::span<uint8_t> data) {
    if (data.size() < 16) {
        return std::unexpected(MIO0Error::TooSmall);
    }

    header.signature = readInt<uint32_t>(data, 0);

    if (header.signature != mio0_signature) {
        return std::unexpected(MIO0Error::IncorrectSignature);
    }

    header.decompressed_length = readInt<uint32_t>(data, 4);
    header.compressed_offset = readInt<uint32_t>(data, 8);
    header.uncompressed_offset = readInt<uint32_t>(data, 12);

    if (header.compressed_offset < 0x10
        || header.uncompressed_offset < header.compressed_offset
        || data.size() < header.uncompressed_offset
        // Each layout bit produces at most 18 bytes (a copy run of length
        // 0xF+3), so this is a valid upper bound on the decompressed size.
        || header.decompressed_length > static_cast<uint64_t>(header.compressed_offset - 0x10) * 8 * 18) {
        return std::unexpected(MIO0Error::CorruptedData);
    }

    layout_bits = data.subspan(0x10, header.compressed_offset - 0x10);
    compressed_data = data.subspan(header.compressed_offset, header.uncompressed_offset - header.compressed_offset);
    uncompressed_data = data.subspan(header.uncompressed_offset, data.size() - header.uncompressed_offset);
    return {};
}

std::expected<std::vector<uint8_t>, MIO0Error> MIO0::decompressMIO0() {
    std::vector<uint8_t> result(header.decompressed_length);

    int bytes_written { 0 };
    int comp_index { 0 };
    int uncomp_index { 0 };

    for (const uint32_t layout_byte : layout_bits) {
        for (int i { 0 }; i < 8; i++) {
            if (layout_byte & (0x80 >> i)) {
                if (uncomp_index >= uncompressed_data.size()) {
                    return std::unexpected(MIO0Error::CorruptedData);
                }

                result[bytes_written] = uncompressed_data[uncomp_index];
                bytes_written++;
                uncomp_index++;
            } else {
                if (comp_index+1 >= compressed_data.size()) {
                    return std::unexpected(MIO0Error::CorruptedData);
                }

                int length = (compressed_data[comp_index] >> 4) + 3;
                int offset = ((compressed_data[comp_index] & 0x0F) << 8) + compressed_data[comp_index+1];
                comp_index += 2;

                // The copy source is `bytes_written - offset - 1` (the game's
                // decoder reads dest - offset - 1); offset == 0 repeats the
                // last written byte and is valid.
                if (bytes_written+length > header.decompressed_length
                    || bytes_written <= offset) {
                    return std::unexpected(MIO0Error::CorruptedData);
                }

                for (int j { 0 }; j < length; j++) {
                    result[bytes_written] = result[bytes_written-offset-1];
                    bytes_written++;
                }
            }

            if (bytes_written == header.decompressed_length) {
                return result;
            }
        }
    }

    return std::unexpected(MIO0Error::CorruptedData);
}
