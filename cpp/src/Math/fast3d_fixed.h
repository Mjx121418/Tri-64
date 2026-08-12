#ifndef FAST3D_FIXED_H
#define FAST3D_FIXED_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include "Math/math.h"

namespace Fast3D {

// Fast3D matrices use a signed 32-bit Q16.16 value for each element. The
// serialized matrix stores the signed integer half first and the unsigned
// fractional half second.
using Fixed = int32_t;

inline constexpr unsigned kFractionBits {16};
inline constexpr Fixed kFixedOne {static_cast<Fixed>(1 << kFractionBits)};
inline constexpr size_t kMatrixElementCount {16};
inline constexpr size_t kMatrixBytes {kMatrixElementCount * sizeof(uint32_t)};
inline constexpr size_t kLightBytes {16};
inline constexpr size_t kRspVectorLanes {8};

using FixedMatrix = std::array<std::array<Fixed, 4>, 4>;
using FixedVector3 = std::array<Fixed, 3>;
using FixedVector4 = std::array<Fixed, 4>;
using RspVector = std::array<int16_t, kRspVectorLanes>;

// This is the byte layout used by Fast3D's Light_t. Lighting itself is
// intentionally outside this module; keeping the raw light representation
// here prevents later code from treating signed directions as colors.
struct Light {
    std::array<uint8_t, 3> color {};
    std::array<int8_t, 3> direction {};

    constexpr bool operator==(const Light &) const = default;
};

// Sign-extend a value containing between 1 and 63 significant bits. Invalid
// widths return zero instead of evaluating an undefined shift.
constexpr int64_t signExtend(uint64_t value, unsigned bit_count) noexcept {
    if (bit_count == 0 || bit_count > 63) {
        return 0;
    }

    const uint64_t sign_bit = uint64_t {1} << (bit_count - 1);
    const uint64_t magnitude = value & (sign_bit - 1);
    return static_cast<int64_t>(magnitude)
        - ((value & sign_bit) ? static_cast<int64_t>(sign_bit) : int64_t {0});
}

constexpr int16_t signExtend16(uint16_t value) noexcept {
    return static_cast<int16_t>(signExtend(value, 16));
}

constexpr int32_t signExtend32(uint32_t value) noexcept {
    return static_cast<int32_t>(signExtend(value, 32));
}

constexpr int8_t signExtend8(uint8_t value) noexcept {
    return static_cast<int8_t>(signExtend(value, 8));
}

constexpr int8_t saturateSigned8(int64_t value) noexcept {
    if (value < std::numeric_limits<int8_t>::min()) {
        return std::numeric_limits<int8_t>::min();
    }
    if (value > std::numeric_limits<int8_t>::max()) {
        return std::numeric_limits<int8_t>::max();
    }
    return static_cast<int8_t>(value);
}

constexpr int16_t saturateSigned16(int64_t value) noexcept {
    if (value < std::numeric_limits<int16_t>::min()) {
        return std::numeric_limits<int16_t>::min();
    }
    if (value > std::numeric_limits<int16_t>::max()) {
        return std::numeric_limits<int16_t>::max();
    }
    return static_cast<int16_t>(value);
}

constexpr int32_t saturateSigned32(int64_t value) noexcept {
    if (value < std::numeric_limits<int32_t>::min()) {
        return std::numeric_limits<int32_t>::min();
    }
    if (value > std::numeric_limits<int32_t>::max()) {
        return std::numeric_limits<int32_t>::max();
    }
    return static_cast<int32_t>(value);
}

constexpr uint16_t saturateUnsigned16(int64_t value) noexcept {
    if (value <= 0) {
        return 0;
    }
    if (value >= std::numeric_limits<uint16_t>::max()) {
        return std::numeric_limits<uint16_t>::max();
    }
    return static_cast<uint16_t>(value);
}

constexpr uint8_t saturateUnsigned8(int64_t value) noexcept {
    if (value <= 0) {
        return 0;
    }
    if (value >= std::numeric_limits<uint8_t>::max()) {
        return std::numeric_limits<uint8_t>::max();
    }
    return static_cast<uint8_t>(value);
}

struct FixedParts {
    int16_t integer;
    uint16_t fraction;
};

constexpr FixedParts splitFixed(Fixed value) noexcept {
    const uint32_t bits = static_cast<uint32_t>(value);
    return FixedParts {
        signExtend16(static_cast<uint16_t>(bits >> 16)),
        static_cast<uint16_t>(bits),
    };
}

constexpr Fixed composeFixed(int16_t integer, uint16_t fraction) noexcept {
    const uint32_t bits = (static_cast<uint32_t>(static_cast<uint16_t>(integer)) << 16)
        | fraction;
    return signExtend32(bits);
}

constexpr Fixed fixedFromInteger(int32_t value) noexcept {
    return saturateSigned32(static_cast<int64_t>(value) * (int64_t {1} << kFractionBits));
}

// The RSP accumulator is three signed 16-bit words, interpreted as one
// two's-complement 48-bit value. Each add wraps at 48 bits, as the vector unit
// does; conversion to a matrix/vector Fixed happens only at the end.
class Accumulator48 {
public:
    static constexpr unsigned kBits {48};
    static constexpr uint64_t kMask {(uint64_t {1} << kBits) - 1};

    constexpr Accumulator48() noexcept = default;
    constexpr explicit Accumulator48(int64_t value) noexcept : raw_(static_cast<uint64_t>(value) & kMask) {}

    constexpr void clear() noexcept { raw_ = 0; }

    // Add a signed value to the low end of the 48-bit accumulator.
    constexpr void addSigned(int64_t value) noexcept {
        raw_ = (raw_ + (static_cast<uint64_t>(value) & kMask)) & kMask;
    }

    // These four operations are the four pieces used by Fast3D's matrix
    // multiply: VMADL, VMADM, VMADN, and VMADH respectively.
    constexpr void addUnsignedProductHigh(uint16_t lhs, uint16_t rhs) noexcept {
        addSigned(static_cast<int64_t>((uint32_t {lhs} * uint32_t {rhs}) >> kFractionBits));
    }

    constexpr void addSignedUnsignedProduct(int16_t lhs, uint16_t rhs) noexcept {
        addSigned(static_cast<int64_t>(lhs) * static_cast<int64_t>(rhs));
    }

    constexpr void addUnsignedSignedProduct(uint16_t lhs, int16_t rhs) noexcept {
        addSigned(static_cast<int64_t>(lhs) * static_cast<int64_t>(rhs));
    }

    constexpr void addSignedProductShifted(int16_t lhs, int16_t rhs) noexcept {
        const int64_t product = static_cast<int64_t>(lhs) * static_cast<int64_t>(rhs);
        addSigned(product * (int64_t {1} << kFractionBits));
    }

    constexpr void addFixedProduct(Fixed lhs, Fixed rhs) noexcept {
        const FixedParts left = splitFixed(lhs);
        const FixedParts right = splitFixed(rhs);
        addUnsignedProductHigh(left.fraction, right.fraction);
        addSignedUnsignedProduct(left.integer, right.fraction);
        addUnsignedSignedProduct(left.fraction, right.integer);
        addSignedProductShifted(left.integer, right.integer);
    }

    constexpr uint64_t raw() const noexcept { return raw_; }

    constexpr int64_t signedValue() const noexcept {
        return signExtend(raw_, kBits);
    }

    constexpr uint16_t lowWord() const noexcept {
        return static_cast<uint16_t>(raw_);
    }

    constexpr int16_t middleWord() const noexcept {
        return signExtend16(static_cast<uint16_t>(raw_ >> 16));
    }

    constexpr int16_t highWord() const noexcept {
        return signExtend16(static_cast<uint16_t>(raw_ >> 32));
    }

    // Fast3D's VMADH result is a signed clamp of accumulator bits 47..16.
    constexpr int16_t saturatedMiddle() const noexcept {
        const int32_t clamped = saturateSigned32(signedValue());
        return signExtend16(static_cast<uint16_t>(static_cast<uint32_t>(clamped) >> 16));
    }

    // Fast3D's VMADN result is the low word, except that an accumulator which
    // overflowed the signed 32-bit fixed range gets 0x0000/0xFFFF.
    constexpr uint16_t saturatedLow() const noexcept {
        if (signedValue() < std::numeric_limits<int32_t>::min()) {
            return 0;
        }
        if (signedValue() > std::numeric_limits<int32_t>::max()) {
            return std::numeric_limits<uint16_t>::max();
        }
        return lowWord();
    }

    constexpr Fixed toFixed() const noexcept {
        return composeFixed(saturatedMiddle(), saturatedLow());
    }

private:
    uint64_t raw_ {0};
};

constexpr Fixed fixedMultiply(Fixed lhs, Fixed rhs) noexcept {
    Accumulator48 accumulator;
    accumulator.addFixedProduct(lhs, rhs);
    return accumulator.toFixed();
}

// Multiply a Q16.16 value by a raw unsigned 16-bit RSP lane. G_MW_PERSPNORM
// supplies this kind of lane, not another Q16.16 value; 0xFFFF is the
// maximum normalization scale used by orthographic/uncalibrated paths.
constexpr Fixed fixedMultiplyScalar(Fixed value, uint16_t scalar) noexcept {
    Accumulator48 accumulator;
    accumulator.addSigned(static_cast<int64_t>(value) * scalar);
    return accumulator.toFixed();
}

// Divide two Q16.16 values while keeping the result in Q16.16. This remains
// available for non-RSP callers; projected vertices use rspReciprocal below.
Fixed fixedDivide(Fixed numerator, Fixed denominator) noexcept;

FixedMatrix identityMatrix() noexcept;

// Quantize a float scene-graph matrix into the fixed representation used by
// the RSP. Graph-node matrices are authored as floats before the game emits
// its fixed matrix command.
FixedMatrix fromFloatMatrix(const Mtxf &matrix) noexcept;

// Matrix product in the project's row-vector convention. The result is
// left * right, so a point is transformed by left first and right second.
FixedMatrix matrixMultiply(const FixedMatrix &left, const FixedMatrix &right) noexcept;

// Row-vector multiplication. The fourth component is retained so callers can
// use this for both points (w=1) and directions (w=0).
FixedVector4 vectorMatrixMultiply(const FixedVector4 &vector,
                                  const FixedMatrix &matrix) noexcept;

FixedVector3 transformPoint(const FixedMatrix &matrix, const FixedVector3 &point) noexcept;
FixedVector3 transformDirection(const FixedMatrix &matrix,
                                 const FixedVector3 &direction) noexcept;

// RSP reciprocal-square-root result. This is the raw 32-bit VRSQ result; the
// caller supplies the binary point implied by the vector accumulator.
uint32_t reciprocalSqrt(uint32_t input) noexcept;

// RSP reciprocal result for a signed S15.16 input. This is the raw VRCP
// result; Fast3D's perspective path doubles it because the instruction's
// result uses the reciprocal's normalized half-range.
Fixed rspReciprocal(Fixed input) noexcept;

// Deterministic fixed-point normalization used by the Fast3D light overlay.
// Uses the RSP reciprocal-square-root ROM rather than floating-point math.
FixedVector3 normalizeVector(const FixedVector3 &vector) noexcept;
Fixed fixedDot(const FixedVector3 &left, const FixedVector3 &right) noexcept;

// Fast3D's VMULF: signed Q1.15 multiplication with the RSP's 0x4000
// fractional rounding and signed 16-bit result clamp.
int16_t rspMultiplyFraction(int16_t left, int16_t right) noexcept;

constexpr FixedVector3 makeVector3(Fixed x, Fixed y, Fixed z) noexcept {
    return FixedVector3 {x, y, z};
}

constexpr FixedVector4 makeVector4(Fixed x, Fixed y, Fixed z,
                                   Fixed w = kFixedOne) noexcept {
    return FixedVector4 {x, y, z, w};
}

FixedVector3 lightDirectionQ16(const Light &light) noexcept;

std::array<uint8_t, 4> shadeVertex(
    const FixedVector3 &normal,
    const std::array<FixedVector3, 8> &transformed_lights,
    const std::array<Light, 8> &lights,
    uint8_t num_lights,
    bool lights_loaded,
    uint8_t alpha) noexcept;

// The input is a complete 64-byte SM64 Mtx or a larger span beginning with
// one. No bytes are read when the span is too short.
std::optional<FixedMatrix> decodeMatrix(std::span<const uint8_t> data) noexcept;

// Light_t has byte fields at offsets 0..2 and 8..10; the padding and colc
// fields are intentionally ignored. No bytes are read when the span is short.
std::optional<Light> decodeLight(std::span<const uint8_t> data) noexcept;

} // namespace Fast3D

#endif /* FAST3D_FIXED_H */
