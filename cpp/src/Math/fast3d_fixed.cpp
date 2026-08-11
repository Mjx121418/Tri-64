#include "Math/fast3d_fixed.h"

#include <algorithm>
#include <bit>

namespace Fast3D {

namespace {

uint16_t readBigEndian16(std::span<const uint8_t> data, size_t offset) noexcept {
    return static_cast<uint16_t>((uint16_t {data[offset]} << 8) | data[offset + 1]);
}

// Upper half of the RSP's 1024-entry reciprocal ROM. The reciprocal-square
// root entries are parity-interleaved in indices 0x200..0x3ff. Values are
// reproduced from the CEN64/paraLLEl RSP implementation, which matches the
// hardware ROM used by Fast3D.
constexpr uint16_t kReciprocalSqrtRom[] {
    0x6A09, 0xFFFF, 0x6955, 0xFF00, 0x68A1, 0xFE02, 0x67EF, 0xFD06,
    0x673E, 0xFC0B, 0x668D, 0xFB12, 0x65DE, 0xFA1A, 0x6530, 0xF923,
    0x6482, 0xF82E, 0x63D6, 0xF73B, 0x632B, 0xF648, 0x6280, 0xF557,
    0x61D7, 0xF467, 0x612E, 0xF379, 0x6087, 0xF28C, 0x5FE0, 0xF1A0,
    0x5F3A, 0xF0B6, 0x5E95, 0xEFCD, 0x5DF1, 0xEEE5, 0x5D4E, 0xEDFF,
    0x5CAC, 0xED19, 0x5C0B, 0xEC35, 0x5B6B, 0xEB52, 0x5ACB, 0xEA71,
    0x5A2C, 0xE990, 0x598F, 0xE8B1, 0x58F2, 0xE7D3, 0x5855, 0xE6F6,
    0x57BA, 0xE61B, 0x5720, 0xE540, 0x5686, 0xE467, 0x55ED, 0xE38E,
    0x5555, 0xE2B7, 0x54BE, 0xE1E1, 0x5427, 0xE10D, 0x5391, 0xE039,
    0x52FC, 0xDF66, 0x5268, 0xDE94, 0x51D5, 0xDDC4, 0x5142, 0xDCF4,
    0x50B0, 0xDC26, 0x501F, 0xDB59, 0x4F8E, 0xDA8C, 0x4EFE, 0xD9C1,
    0x4E6F, 0xD8F7, 0x4DE1, 0xD82D, 0x4D53, 0xD765, 0x4CC6, 0xD69E,
    0x4C3A, 0xD5D7, 0x4BAF, 0xD512, 0x4B24, 0xD44E, 0x4A9A, 0xD38A,
    0x4A10, 0xD2C8, 0x4987, 0xD206, 0x48FF, 0xD146, 0x4878, 0xD086,
    0x47F1, 0xCFC7, 0x476B, 0xCF0A, 0x46E5, 0xCE4D, 0x4660, 0xCD91,
    0x45DC, 0xCCD6, 0x4558, 0xCC1B, 0x44D5, 0xCB62, 0x4453, 0xCAA9,
    0x43D1, 0xC9F2, 0x434F, 0xC93B, 0x42CF, 0xC885, 0x424F, 0xC7D0,
    0x41CF, 0xC71C, 0x4151, 0xC669, 0x40D2, 0xC5B6, 0x4055, 0xC504,
    0x3FD8, 0xC453, 0x3F5B, 0xC3A3, 0x3EDF, 0xC2F4, 0x3E64, 0xC245,
    0x3DE9, 0xC198, 0x3D6E, 0xC0EB, 0x3CF5, 0xC03F, 0x3C7C, 0xBF93,
    0x3C03, 0xBEE9, 0x3B8B, 0xBE3F, 0x3B13, 0xBD96, 0x3A9C, 0xBCED,
    0x3A26, 0xBC46, 0x39B0, 0xBB9F, 0x393A, 0xBAF8, 0x38C5, 0xBA53,
    0x3851, 0xB9AE, 0x37DD, 0xB90A, 0x3769, 0xB867, 0x36F6, 0xB7C5,
    0x3684, 0xB723, 0x3612, 0xB681, 0x35A0, 0xB5E1, 0x352F, 0xB541,
    0x34BF, 0xB4A2, 0x344F, 0xB404, 0x33DF, 0xB366, 0x3370, 0xB2C9,
    0x3302, 0xB22C, 0x3293, 0xB191, 0x3226, 0xB0F5, 0x31B9, 0xB05B,
    0x314C, 0xAFC1, 0x30DF, 0xAF28, 0x3074, 0xAE8F, 0x3008, 0xADF7,
    0x2F9D, 0xAD60, 0x2F33, 0xACC9, 0x2EC8, 0xAC33, 0x2E5F, 0xAB9E,
    0x2DF6, 0xAB09, 0x2D8D, 0xAA75, 0x2D24, 0xA9E1, 0x2CBC, 0xA94E,
    0x2C55, 0xA8BC, 0x2BEE, 0xA82A, 0x2B87, 0xA799, 0x2B21, 0xA708,
    0x2ABB, 0xA678, 0x2A55, 0xA5E8, 0x29F0, 0xA559, 0x298B, 0xA4CB,
    0x2927, 0xA43D, 0x28C3, 0xA3B0, 0x2860, 0xA323, 0x27FD, 0xA297,
    0x279A, 0xA20B, 0x2738, 0xA180, 0x26D6, 0xA0F6, 0x2674, 0xA06C,
    0x2613, 0x9FE2, 0x25B2, 0x9F59, 0x2552, 0x9ED1, 0x24F2, 0x9E49,
    0x2492, 0x9DC2, 0x2432, 0x9D3B, 0x23D3, 0x9CB4, 0x2375, 0x9C2F,
    0x2317, 0x9BA9, 0x22B9, 0x9B25, 0x225B, 0x9AA0, 0x21FE, 0x9A1C,
    0x21A1, 0x9999, 0x2145, 0x9916, 0x20E8, 0x9894, 0x208D, 0x9812,
    0x2031, 0x9791, 0x1FD6, 0x9710, 0x1F7B, 0x968F, 0x1F21, 0x960F,
    0x1EC7, 0x9590, 0x1E6D, 0x9511, 0x1E13, 0x9492, 0x1DBA, 0x9414,
    0x1D61, 0x9397, 0x1D09, 0x931A, 0x1CB1, 0x929D, 0x1C59, 0x9221,
    0x1C01, 0x91A5, 0x1BAA, 0x9129, 0x1B53, 0x90AF, 0x1AFC, 0x9034,
    0x1AA6, 0x8FBA, 0x1A50, 0x8F40, 0x19FA, 0x8EC7, 0x19A5, 0x8E4F,
    0x1950, 0x8DD6, 0x18FB, 0x8D5E, 0x18A7, 0x8CE7, 0x1853, 0x8C70,
    0x17FF, 0x8BF9, 0x17AB, 0x8B83, 0x1758, 0x8B0D, 0x1705, 0x8A98,
    0x16B2, 0x8A23, 0x1660, 0x89AE, 0x160D, 0x893A, 0x15BC, 0x88C6,
    0x156A, 0x8853, 0x1519, 0x87E0, 0x14C8, 0x876D, 0x1477, 0x86FB,
    0x1426, 0x8689, 0x13D6, 0x8618, 0x1386, 0x85A7, 0x1337, 0x8536,
    0x12E7, 0x84C6, 0x1298, 0x8456, 0x1249, 0x83E7, 0x11FB, 0x8377,
    0x11AC, 0x8309, 0x115E, 0x829A, 0x1111, 0x822C, 0x10C3, 0x81BF,
    0x1076, 0x8151, 0x1029, 0x80E4, 0x0FDC, 0x8078, 0x0F8F, 0x800C,
    0x0F43, 0x7FA0, 0x0EF7, 0x7F34, 0x0EAB, 0x7EC9, 0x0E60, 0x7E5E,
    0x0E15, 0x7DF4, 0x0DCA, 0x7D8A, 0x0D7F, 0x7D20, 0x0D34, 0x7CB6,
    0x0CEA, 0x7C4D, 0x0CA0, 0x7BE5, 0x0C56, 0x7B7C, 0x0C0C, 0x7B14,
    0x0BC3, 0x7AAC, 0x0B7A, 0x7A45, 0x0B31, 0x79DE, 0x0AE8, 0x7977,
    0x0AA0, 0x7911, 0x0A58, 0x78AB, 0x0A10, 0x7845, 0x09C8, 0x77DF,
    0x0981, 0x777A, 0x0939, 0x7715, 0x08F2, 0x76B1, 0x08AB, 0x764D,
    0x0865, 0x75E9, 0x081E, 0x7585, 0x07D8, 0x7522, 0x0792, 0x74BF,
    0x074D, 0x745D, 0x0707, 0x73FA, 0x06C2, 0x7398, 0x067D, 0x7337,
    0x0638, 0x72D5, 0x05F3, 0x7274, 0x05AF, 0x7213, 0x056A, 0x71B3,
    0x0526, 0x7152, 0x04E2, 0x70F2, 0x049F, 0x7093, 0x045B, 0x7033,
    0x0418, 0x6FD4, 0x03D5, 0x6F76, 0x0392, 0x6F17, 0x0350, 0x6EB9,
    0x030D, 0x6E5B, 0x02CB, 0x6DFD, 0x0289, 0x6DA0, 0x0247, 0x6D43,
    0x0206, 0x6CE6, 0x01C4, 0x6C8A, 0x0183, 0x6C2D, 0x0142, 0x6BD1,
    0x0101, 0x6B76, 0x00C0, 0x6B1A, 0x0080, 0x6ABF, 0x0040, 0x6A64,
};

static_assert(sizeof(kReciprocalSqrtRom) / sizeof(kReciprocalSqrtRom[0]) == 512);

uint32_t reciprocalSqrtCore(uint32_t bits) noexcept {
    const int32_t input = static_cast<int32_t>(bits);
    const uint32_t sign = input < 0 ? 0xFFFFFFFFu : 0u;
    uint32_t data = bits ^ sign;

    // The RSP uses one's-complement magnitude handling for negative values,
    // except for the signed 16-bit boundary represented by -32768.
    if (input > -32768) {
        data -= sign;
    }
    if (data == 0) {
        return 0x7FFFFFFFu;
    }
    if (input == -32768) {
        return 0xFFFF0000u;
    }

    const unsigned shift = std::countl_zero(data);
    const unsigned mantissa = static_cast<unsigned>(
        (static_cast<uint64_t>(data) << shift) & 0x7FC00000u) >> 22;
    const unsigned index = ((mantissa | 0x200u) & 0x3FEu) | (shift & 1u);
    uint32_t result = ((0x10000u | kReciprocalSqrtRom[index - 0x200]) << 14)
        >> ((31u - shift) >> 1);
    return result ^ sign;
}

uint8_t shadeComponent(int64_t value) noexcept {
    return saturateUnsigned8(value);
}

} // namespace

FixedMatrix identityMatrix() noexcept {
    FixedMatrix matrix {};
    for (size_t i {0}; i < matrix.size(); i++) {
        matrix[i][i] = kFixedOne;
    }
    return matrix;
}

FixedMatrix fromFloatMatrix(const Mtxf &matrix) noexcept {
    FixedMatrix result {};
    for (size_t row = 0; row < result.size(); row++) {
        for (size_t column = 0; column < result[row].size(); column++) {
            const double scaled = static_cast<double>(matrix[row][column])
                * static_cast<double>(kFixedOne);
            result[row][column] = saturateSigned32(static_cast<int64_t>(scaled));
        }
    }
    return result;
}

Fixed fixedDivide(Fixed numerator, Fixed denominator) noexcept {
    if (denominator == 0) {
        return numerator < 0 ? std::numeric_limits<Fixed>::min()
                             : std::numeric_limits<Fixed>::max();
    }
    const int64_t quotient = (static_cast<int64_t>(numerator) << kFractionBits)
        / denominator;
    return saturateSigned32(quotient);
}

FixedMatrix matrixMultiply(const FixedMatrix &left, const FixedMatrix &right) noexcept {
    FixedMatrix result {};
    for (size_t row {0}; row < result.size(); row++) {
        for (size_t column {0}; column < result[row].size(); column++) {
            Accumulator48 accumulator;
            for (size_t term {0}; term < result.size(); term++) {
                accumulator.addFixedProduct(left[row][term], right[term][column]);
            }
            result[row][column] = accumulator.toFixed();
        }
    }
    return result;
}

FixedVector4 vectorMatrixMultiply(const FixedVector4 &vector,
                                  const FixedMatrix &matrix) noexcept {
    FixedVector4 result {};
    for (size_t column {0}; column < result.size(); column++) {
        Accumulator48 accumulator;
        for (size_t term {0}; term < vector.size(); term++) {
            accumulator.addFixedProduct(vector[term], matrix[term][column]);
        }
        result[column] = accumulator.toFixed();
    }
    return result;
}

FixedVector3 transformPoint(const FixedMatrix &matrix, const FixedVector3 &point) noexcept {
    const FixedVector4 transformed = vectorMatrixMultiply(
        makeVector4(point[0], point[1], point[2]), matrix);
    return makeVector3(transformed[0], transformed[1], transformed[2]);
}

FixedVector3 transformDirection(const FixedMatrix &matrix,
                                const FixedVector3 &direction) noexcept {
    const FixedVector4 transformed = vectorMatrixMultiply(
        makeVector4(direction[0], direction[1], direction[2], 0), matrix);
    return makeVector3(transformed[0], transformed[1], transformed[2]);
}

uint32_t reciprocalSqrt(uint32_t input) noexcept {
    return reciprocalSqrtCore(input);
}

FixedVector3 normalizeVector(const FixedVector3 &vector) noexcept {
    int64_t squared = 0;
    for (const Fixed value : vector) {
        squared += (static_cast<int64_t>(value) * value) >> kFractionBits;
    }
    if (squared <= 0) {
        return makeVector3(0, 0, 0);
    }

    // VRSQ returns 2^31/sqrt(input). The input here is Q16.16, so shifting
    // the result down by seven converts it to the Q16.16 reciprocal length.
    const uint32_t squared_input = static_cast<uint32_t>(std::min<int64_t>(
        squared, std::numeric_limits<uint32_t>::max()));
    const Fixed inverse_length = static_cast<Fixed>(
        reciprocalSqrt(squared_input) >> 7);

    FixedVector3 result {};
    for (size_t i = 0; i < result.size(); i++) {
        result[i] = saturateSigned32(
            (static_cast<int64_t>(vector[i]) * inverse_length) >> kFractionBits);
    }
    return result;
}

Fixed fixedDot(const FixedVector3 &left, const FixedVector3 &right) noexcept {
    Accumulator48 accumulator;
    for (size_t i {0}; i < left.size(); i++) {
        accumulator.addFixedProduct(left[i], right[i]);
    }
    return accumulator.toFixed();
}

FixedVector3 lightDirectionQ16(const Light &light) noexcept {
    // Light_t directions are signed Q0.7 bytes. -128 represents -1.0 and
    // 127 represents the largest positive value, so one byte step is 1/128.
    constexpr int32_t kQ07ToQ16 {1 << (kFractionBits - 7)};
    return makeVector3(
        static_cast<Fixed>(static_cast<int32_t>(light.direction[0]) * kQ07ToQ16),
        static_cast<Fixed>(static_cast<int32_t>(light.direction[1]) * kQ07ToQ16),
        static_cast<Fixed>(static_cast<int32_t>(light.direction[2]) * kQ07ToQ16));
}

std::array<uint8_t, 4> shadeVertex(
    const FixedVector3 &normal,
    const std::array<FixedVector3, 8> &transformed_lights,
    const std::array<Light, 8> &lights,
    uint8_t num_lights,
    bool lights_loaded,
    uint8_t alpha) noexcept {
    std::array<uint8_t, 4> shade {255, 255, 255, alpha};
    if (!lights_loaded) {
        return shade;
    }

    const uint8_t directional_count = std::min<uint8_t>(num_lights, 7);
    int64_t rgb[3] {
        lights[directional_count].color[0],
        lights[directional_count].color[1],
        lights[directional_count].color[2],
    };
    for (uint8_t i = 0; i < directional_count; i++) {
        if (lights[i].color[0] == 0 && lights[i].color[1] == 0 && lights[i].color[2] == 0) {
            continue;
        }
        const Fixed dot = fixedDot(normal, transformed_lights[i]);
        if (dot <= 0) {
            continue;
        }
        for (int channel = 0; channel < 3; channel++) {
            rgb[channel] += (static_cast<int64_t>(dot) * lights[i].color[channel])
                >> kFractionBits;
        }
    }

    shade[0] = shadeComponent(rgb[0]);
    shade[1] = shadeComponent(rgb[1]);
    shade[2] = shadeComponent(rgb[2]);
    return shade;
}

std::optional<FixedMatrix> decodeMatrix(std::span<const uint8_t> data) noexcept {
    if (data.size() < kMatrixBytes) {
        return std::nullopt;
    }

    FixedMatrix matrix {};
    for (size_t element {0}; element < kMatrixElementCount; element++) {
        const uint16_t integer = readBigEndian16(data, element * sizeof(uint16_t));
        const uint16_t fraction = readBigEndian16(
            data, (kMatrixBytes / 2) + element * sizeof(uint16_t));
        matrix[element / 4][element % 4] = composeFixed(signExtend16(integer), fraction);
    }
    return matrix;
}

std::optional<Light> decodeLight(std::span<const uint8_t> data) noexcept {
    if (data.size() < kLightBytes) {
        return std::nullopt;
    }

    Light light;
    light.color = {data[0], data[1], data[2]};
    light.direction = {signExtend8(data[8]), signExtend8(data[9]), signExtend8(data[10])};
    return light;
}

} // namespace Fast3D
