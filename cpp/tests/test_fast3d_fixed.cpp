#include "test_fast3d_fixed.h"

#include "Level/dl_interpreter.h"
#include "Math/fast3d_fixed.h"
#include "Math/math.h"
#include "Memory/segment.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>

namespace {

bool check(bool condition, const char *description) {
    if (!condition) {
        printf("test_fast3d_fixed: FAIL %s\n", description);
    }
    return condition;
}

bool checkMatrix(const Fast3D::FixedMatrix &actual,
                 const Fast3D::FixedMatrix &expected,
                 const char *description) {
    bool ok = true;
    for (size_t row {0}; row < actual.size(); row++) {
        for (size_t column {0}; column < actual[row].size(); column++) {
            if (actual[row][column] != expected[row][column]) {
                printf("test_fast3d_fixed: FAIL %s [%zu][%zu] got 0x%08X expect 0x%08X\n",
                       description, row, column,
                       static_cast<uint32_t>(actual[row][column]),
                       static_cast<uint32_t>(expected[row][column]));
                ok = false;
            }
        }
    }
    return ok;
}

bool checkVector3(const Fast3D::FixedVector3 &actual,
                  const Fast3D::FixedVector3 &expected,
                  const char *description) {
    const bool ok = actual == expected;
    if (!ok) {
        printf("test_fast3d_fixed: FAIL %s got=(0x%08X,0x%08X,0x%08X) "
               "expect=(0x%08X,0x%08X,0x%08X)\n",
               description,
               static_cast<uint32_t>(actual[0]), static_cast<uint32_t>(actual[1]),
               static_cast<uint32_t>(actual[2]),
               static_cast<uint32_t>(expected[0]), static_cast<uint32_t>(expected[1]),
               static_cast<uint32_t>(expected[2]));
    }
    return ok;
}

void putMatrixElement(std::array<uint8_t, Fast3D::kMatrixBytes> &data,
                      size_t element, uint32_t bits) {
    const size_t integer_offset = element * sizeof(uint16_t);
    const size_t fraction_offset = Fast3D::kMatrixBytes / 2 + integer_offset;
    data[integer_offset] = static_cast<uint8_t>(bits >> 24);
    data[integer_offset + 1] = static_cast<uint8_t>(bits >> 16);
    data[fraction_offset] = static_cast<uint8_t>(bits >> 8);
    data[fraction_offset + 1] = static_cast<uint8_t>(bits);
}

void putCommand(std::vector<uint8_t> &data, size_t offset, uint32_t w0, uint32_t w1) {
    for (int i = 0; i < 4; i++) {
        data[offset + i] = static_cast<uint8_t>(w0 >> (24 - i * 8));
        data[offset + 4 + i] = static_cast<uint8_t>(w1 >> (24 - i * 8));
    }
}

void putMatrix(std::vector<uint8_t> &data, size_t offset, const Fast3D::FixedMatrix &matrix) {
    for (size_t element = 0; element < Fast3D::kMatrixElementCount; element++) {
        const uint32_t bits = static_cast<uint32_t>(matrix[element / 4][element % 4]);
        const size_t integer_offset = offset + element * sizeof(uint16_t);
        const size_t fraction_offset = offset + Fast3D::kMatrixBytes / 2
            + element * sizeof(uint16_t);
        data[integer_offset] = static_cast<uint8_t>(bits >> 24);
        data[integer_offset + 1] = static_cast<uint8_t>(bits >> 16);
        data[fraction_offset] = static_cast<uint8_t>(bits >> 8);
        data[fraction_offset + 1] = static_cast<uint8_t>(bits);
    }
}

void putVertex(std::vector<uint8_t> &data, size_t offset, int16_t x, int16_t y, int16_t z,
               int16_t s, int16_t t) {
    const uint16_t values[] {
        static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(z), 0,
        static_cast<uint16_t>(s), static_cast<uint16_t>(t),
    };
    for (size_t i = 0; i < std::size(values); i++) {
        data[offset + i * 2] = static_cast<uint8_t>(values[i] >> 8);
        data[offset + i * 2 + 1] = static_cast<uint8_t>(values[i]);
    }
    data[offset + 12] = 0x7F;
    data[offset + 13] = 0;
    data[offset + 14] = 0;
    data[offset + 15] = 0x5A;
}

void putLight(std::vector<uint8_t> &data, size_t offset,
              uint8_t r, uint8_t g, uint8_t b,
              int8_t x, int8_t y, int8_t z) {
    data[offset] = r;
    data[offset + 1] = g;
    data[offset + 2] = b;
    data[offset + 8] = static_cast<uint8_t>(x);
    data[offset + 9] = static_cast<uint8_t>(y);
    data[offset + 10] = static_cast<uint8_t>(z);
}

} // namespace

void testFast3DFixed() {
    using namespace Fast3D;

    std::array<uint8_t, kMatrixBytes> serialized {};
    putMatrixElement(serialized, 0, 0x00018000);  // +1.5
    putMatrixElement(serialized, 1, 0xFFFFC000);  // -0.25
    putMatrixElement(serialized, 6, 0xFFFE8000);  // -1.5
    putMatrixElement(serialized, 12, 0x00001234); // positive fraction
    putMatrixElement(serialized, 15, static_cast<uint32_t>(kFixedOne));

    const auto decoded = decodeMatrix(std::span<const uint8_t>(serialized));
    if (!check(decoded.has_value(), "64-byte matrix decode")) {
        return;
    }
    check((*decoded)[0][0] == 0x00018000, "positive matrix decode");
    check((*decoded)[0][1] == signExtend32(0xFFFFC000), "negative matrix decode");
    check((*decoded)[1][2] == signExtend32(0xFFFE8000), "negative fractional decode");
    check((*decoded)[3][0] == 0x00001234, "positive fractional decode");
    check(!decodeMatrix(std::span<const uint8_t>(serialized.data(), kMatrixBytes - 1)),
          "short matrix is rejected");

    check(signExtend16(0x7FFF) == 32767, "positive sign extension");
    check(signExtend16(0x8000) == -32768, "negative sign extension");
    check(signExtend16(0xFFFF) == -1, "negative one sign extension");
    check(signExtend32(0x80000000) == std::numeric_limits<int32_t>::min(),
          "32-bit sign extension boundary");
    check(signExtend(0x3F, 6) == -1, "arbitrary-width sign extension");
    check(signExtend(0xFF, 0) == 0 && signExtend(0xFF, 64) == 0,
          "invalid sign-extension widths are deterministic");

    const FixedMatrix identity = identityMatrix();
    FixedMatrix translation = identity;
    translation[3][0] = fixedFromInteger(100);
    translation[3][1] = fixedFromInteger(200);
    translation[3][2] = fixedFromInteger(300);
    checkMatrix(matrixMultiply(identity, translation), translation,
                "identity times translation");
    checkMatrix(matrixMultiply(translation, identity), translation,
                "translation times identity");

    const FixedVector3 point = makeVector3(fixedFromInteger(1), fixedFromInteger(2),
                                           fixedFromInteger(3));
    checkVector3(transformPoint(translation, point),
                 makeVector3(fixedFromInteger(101), fixedFromInteger(202),
                             fixedFromInteger(303)),
                 "row-vector translation");

    FixedMatrix local = identity;
    local[3][0] = fixedFromInteger(10);
    local[3][1] = fixedFromInteger(20);
    local[3][2] = fixedFromInteger(30);
    FixedMatrix parentScale = identity;
    parentScale[0][0] = fixedFromInteger(2);
    parentScale[1][1] = fixedFromInteger(2);
    parentScale[2][2] = fixedFromInteger(2);
    checkVector3(transformPoint(matrixMultiply(local, parentScale), makeVector3(0, 0, 0)),
                 makeVector3(fixedFromInteger(20), fixedFromInteger(40),
                             fixedFromInteger(60)),
                 "row-vector local-parent composition");

    FixedMatrix fractional = identity;
    fractional[0][0] = 0x00018000; // 1.5
    fractional[3][0] = 0x00004000; // 0.25
    FixedMatrix halfScale = identity;
    halfScale[0][0] = 0x00008000; // 0.5
    const FixedMatrix fractionalProduct = matrixMultiply(fractional, halfScale);
    check(fractionalProduct[0][0] == 0x0000C000, "fractional matrix product 1.5 times 0.5");
    check(fractionalProduct[3][0] == 0x00002000,
          "fractional translation follows row-vector composition");
    check(fixedMultiply(0x00010001, 0x00010001) == 0x00010002,
          "fractional product truncates like VMAD");
    check(fixedMultiply(signExtend32(0xFFFFC000), 0x00008000) == signExtend32(0xFFFFE000),
          "negative fractional product keeps its sign");
    check(fixedMultiply(std::numeric_limits<Fixed>::max(), fixedFromInteger(2))
              == std::numeric_limits<Fixed>::max(),
          "positive fixed product saturates at the VMAD boundary");
    check(fixedMultiply(std::numeric_limits<Fixed>::min(), fixedFromInteger(-1))
              == std::numeric_limits<Fixed>::max(),
          "negative times negative fixed product saturates at the VMAD boundary");
    check(fixedMultiply(std::numeric_limits<Fixed>::min(), fixedFromInteger(1))
              == std::numeric_limits<Fixed>::min(),
          "negative fixed product preserves the lower boundary");
    FixedMatrix overflow_scale = identityMatrix();
    overflow_scale[0][0] = fixedFromInteger(2);
    checkVector3(transformPoint(
                     overflow_scale,
                     makeVector3(std::numeric_limits<Fixed>::max(), 0, 0)),
                 makeVector3(std::numeric_limits<Fixed>::max(), 0, 0),
                 "vector matrix positive overflow saturates");
    overflow_scale[0][0] = fixedFromInteger(-1);
    checkVector3(transformPoint(
                     overflow_scale,
                     makeVector3(std::numeric_limits<Fixed>::min(), 0, 0)),
                 makeVector3(std::numeric_limits<Fixed>::max(), 0, 0),
                 "vector matrix negative overflow saturates");

    // Matrix/vector products use the RSP's wrapped 48-bit accumulator before
    // the VMAD result is saturated. Four maximum positive products wrap just
    // below zero; the signed counterpart wraps just above zero. Keep these
    // values as goldens so a widened host accumulator cannot silently change
    // Fast3D's overflow behavior.
    const Fixed fixed_max = std::numeric_limits<Fixed>::max();
    const Fixed fixed_min = std::numeric_limits<Fixed>::min();
    FixedMatrix maximums {};
    for (auto &row : maximums) {
        row.fill(fixed_max);
    }
    FixedMatrix minimum_row {};
    minimum_row[0].fill(fixed_min);
    const FixedMatrix positive_wrap = matrixMultiply(maximums, maximums);
    const FixedMatrix negative_wrap = matrixMultiply(minimum_row, maximums);
    const Fixed wrap_positive = -262144;
    const Fixed wrap_negative = 131072;
    FixedMatrix positive_expected {};
    for (auto &row : positive_expected) {
        row.fill(wrap_positive);
    }
    FixedMatrix negative_expected {};
    negative_expected[0].fill(wrap_negative);
    checkMatrix(positive_wrap,
                positive_expected,
                "four-term positive matrix accumulator wrap");
    checkMatrix(negative_wrap,
                negative_expected,
                "four-term negative matrix accumulator wrap");
    const FixedVector4 positive_vector = vectorMatrixMultiply(
        FixedVector4 {fixed_max, fixed_max, fixed_max, fixed_max}, maximums);
    check(positive_vector == FixedVector4 {wrap_positive, wrap_positive,
                                           wrap_positive, wrap_positive},
          "four-term positive vector accumulator wrap");
    const FixedVector4 negative_vector = vectorMatrixMultiply(
        FixedVector4 {fixed_min, fixed_min, fixed_min, fixed_min}, maximums);
    check(negative_vector == FixedVector4 {wrap_negative, wrap_negative,
                                           wrap_negative, wrap_negative},
          "four-term negative vector accumulator wrap");

    const FixedVector3 fractionalPoint = makeVector3(0x00010000, 0x00020000, 0x00030000);
    checkVector3(transformPoint(fractionalProduct, fractionalPoint),
                 makeVector3(0x0000E000, 0x00020000, 0x00030000),
                 "fractional transformed vector");
    check(transformDirection(translation, makeVector3(kFixedOne, 0, 0))
              == makeVector3(kFixedOne, 0, 0),
          "direction ignores translation");
    check(fixedDot(makeVector3(0x00018000, 0, 0), makeVector3(0x00008000, 0, 0))
              == 0x0000C000,
          "fixed vector dot product");
    check(rspMultiplyFraction(0x7FFF, 0x7FFF) == 0x7FFE,
          "VMULF fractional rounding");
    check(rspMultiplyFraction(std::numeric_limits<int16_t>::min(),
                              std::numeric_limits<int16_t>::min())
              == std::numeric_limits<int16_t>::max(),
          "VMULF positive signed saturation");
    check(rspMultiplyFraction(std::numeric_limits<int16_t>::min(), 0x7FFF) == -32767,
          "VMULF negative signed rounding");

    std::array<Light, 8> shade_lights {};
    std::array<FixedVector3, 8> shade_directions {};
    shade_lights[0].color = {255, 255, 255};
    shade_lights[0].direction = {127, 0, 0};
    shade_lights[1].color = {10, 20, 30};
    shade_directions[0] = makeVector3(-kFixedOne, 0, 0);
    const auto negative_shade = shadeVertex(
        makeVector3(kFixedOne, 0, 0), shade_directions, shade_lights, 1, true, 0x5A);
    check(negative_shade == std::array<uint8_t, 4> {10, 20, 30, 0x5A},
          "negative light dot keeps ambient only");
    shade_lights[0].color = {255, 255, 255};
    shade_directions[0] = makeVector3(kFixedOne, 0, 0);
    const auto overbright_shade = shadeVertex(
        makeVector3(kFixedOne, 0, 0), shade_directions, shade_lights, 1, true, 0xFF);
    check(overbright_shade == std::array<uint8_t, 4> {255, 255, 255, 0xFF},
          "overbright light accumulation saturates to white");

    // A half-length normal and half-length light produce an exact Q1.15 dot
    // of 0.25. Two directional lights plus ambient then saturate red while
    // retaining the unsaturated green/blue channel values.
    std::array<Light, 8> channel_lights {};
    std::array<FixedVector3, 8> channel_directions {};
    channel_lights[0].color = {255, 100, 200};
    channel_lights[1].color = {100, 255, 100};
    channel_lights[2].color = {200, 20, 30};
    channel_directions[0] = makeVector3(0x00008000, 0x00008000, 0);
    channel_directions[1] = makeVector3(0x00008000, 0x00008000, 0);
    const auto channel_shade = shadeVertex(
        makeVector3(0x00008000, 0x00008000, 0), channel_directions,
        channel_lights, 2, true, 0xA5);
    check(channel_shade == std::array<uint8_t, 4> {255, 197, 180, 0xA5},
          "multi-light shade saturates channels independently");

    const Mtxf object_transform = mtxfMul(
        mtxfRotationZXY({0x1200, 0x2300, 0x0800}), mtxfTranslation(17, -9, 31));
    const Vec3<float> transformed = transformPoint(object_transform, {4, 5, 6});
    const Vec3<float> restored = transformPoint(mtxfInverse(object_transform), transformed);
    check(std::abs(restored.x - 4.0f) < 0.001f && std::abs(restored.y - 5.0f) < 0.001f &&
              std::abs(restored.z - 6.0f) < 0.001f,
          "row-vector affine inverse");

    check(saturateSigned8(-129) == std::numeric_limits<int8_t>::min(),
          "signed 8-bit lower saturation");
    check(saturateSigned8(128) == std::numeric_limits<int8_t>::max(),
          "signed 8-bit upper saturation");
    check(saturateSigned16(-32769) == std::numeric_limits<int16_t>::min(),
          "signed 16-bit lower saturation");
    check(saturateSigned16(32768) == std::numeric_limits<int16_t>::max(),
          "signed 16-bit upper saturation");
    check(saturateSigned32(static_cast<int64_t>(std::numeric_limits<int32_t>::min()) - 1)
              == std::numeric_limits<int32_t>::min(),
          "signed 32-bit lower saturation");
    check(saturateSigned32(static_cast<int64_t>(std::numeric_limits<int32_t>::max()) + 1)
              == std::numeric_limits<int32_t>::max(),
          "signed 32-bit upper saturation");
    check(saturateUnsigned16(-1) == 0 && saturateUnsigned16(65536) == 65535,
          "unsigned 16-bit saturation");
    check(saturateUnsigned8(-1) == 0 && saturateUnsigned8(256) == 255,
          "unsigned 8-bit saturation");

    check(reciprocalSqrt(0x00000000u) == 0x7FFFFFFFu,
          "RSP reciprocal square-root zero case");
    check(reciprocalSqrt(0x00000001u) == 0x7FFFC000u,
          "RSP reciprocal square-root one case");
    check(reciprocalSqrt(0x00000002u) == 0x5A824000u,
          "RSP reciprocal square-root two case");
    check(reciprocalSqrt(0x00000004u) == 0x3FFFE000u,
          "RSP reciprocal square-root four case");
    check(reciprocalSqrt(0x00010000u) == 0x007FFFC0u,
          "RSP reciprocal square-root Q16 one case");
    check(reciprocalSqrt(0xFFFFFFFFu) == 0x80003FFFu,
          "RSP reciprocal square-root negative one case");
    check(reciprocalSqrt(0xFFFF8000u) == 0xFFFF0000u,
          "RSP reciprocal square-root negative boundary case");
    check(rspReciprocal(0) == static_cast<Fixed>(0x7FFFFFFFu),
          "RSP reciprocal zero case");
    check(rspReciprocal(kFixedOne) == 0x00007FFF,
          "RSP reciprocal Q16 one case");
    check(rspReciprocal(fixedFromInteger(2)) == 0x00003FFF,
          "RSP reciprocal Q16 two case");
    check(rspReciprocal(fixedFromInteger(3)) == 0x00002AAA,
          "RSP reciprocal Q16 three case");
    check(rspReciprocal(0x00017880) == 0x00005708,
           "RSP reciprocal ROM quantization entry 241");
    check(rspReciprocal(0x00018880) == 0x0000537C,
           "RSP reciprocal ROM quantization entry 273");
    check(fixedMultiplyScalar(kFixedOne, 4) == 4 * kFixedOne,
          "perspNorm scales Q16 values");
    check(fixedMultiplyScalar(kFixedOne, 0xFFFF) == std::numeric_limits<Fixed>::max(),
          "maximum perspNorm saturates Q16 values");

    Accumulator48 wrapped((int64_t {1} << 47) - 1);
    wrapped.addSigned(1);
    check(wrapped.signedValue() == -(int64_t {1} << 47),
          "48-bit accumulator wraps deterministically");

    const Accumulator48 positiveOverflow(static_cast<int64_t>(std::numeric_limits<int32_t>::max()) + 1);
    check(positiveOverflow.saturatedMiddle() == std::numeric_limits<int16_t>::max()
              && positiveOverflow.saturatedLow() == std::numeric_limits<uint16_t>::max()
              && positiveOverflow.toFixed() == std::numeric_limits<int32_t>::max(),
          "positive VMAD accumulator saturation");
    const Accumulator48 negativeOverflow(static_cast<int64_t>(std::numeric_limits<int32_t>::min()) - 1);
    check(negativeOverflow.saturatedMiddle() == std::numeric_limits<int16_t>::min()
              && negativeOverflow.saturatedLow() == 0
              && negativeOverflow.toFixed() == std::numeric_limits<int32_t>::min(),
          "negative VMAD accumulator saturation");

    std::array<uint8_t, kLightBytes> lightData {};
    lightData[0] = 0x10;
    lightData[1] = 0x20;
    lightData[2] = 0x30;
    lightData[8] = 0x80;
    lightData[9] = 0xFF;
    lightData[10] = 0x7F;
    const auto light = decodeLight(std::span<const uint8_t>(lightData));
    check(light.has_value() && light->color == std::array<uint8_t, 3> {0x10, 0x20, 0x30}
              && light->direction == std::array<int8_t, 3> {-128, -1, 127},
          "Light_t byte decode");
    if (light) {
        check(lightDirectionQ16(*light) == makeVector3(-0x00010000, -0x00000200, 0x0000FE00),
              "Light_t direction Q16 conversion");
    }

    // Fast3D processes vertices at G_VTX. A later matrix and texture-scale
    // change must not alter the cached vertex used by G_TRI1.
    {
        std::vector<uint8_t> segment(0x300, 0);
        FixedMatrix first = identityMatrix();
        first[3][0] = fixedFromInteger(10);
        FixedMatrix second = identityMatrix();
        second[3][0] = fixedFromInteger(100);
        putMatrix(segment, 0x100, first);
        putMatrix(segment, 0x140, second);
        putVertex(segment, 0x180, 1, 2, 3, 32, 0);
        putVertex(segment, 0x190, 0, 0, 0, 0, 0);
        putVertex(segment, 0x1A0, 0, 1, 0, 0, 0);

        size_t command = 0;
        putCommand(segment, command, (0x01u << 24) | (GBI::MTX_LOAD << 16) | 0x40,
                   0x0E000100u);
        command += 8;
        putCommand(segment, command, (0xBBu << 24) | 1u, (0xFFFFu << 16) | 0xFFFFu);
        command += 8;
        putCommand(segment, command, (0x04u << 24) | (0x20u << 16) | 48u,
                   0x0E000180u);
        command += 8;
        putCommand(segment, command, (0xBBu << 24) | 1u, (0x8000u << 16) | 0x8000u);
        command += 8;
        putCommand(segment, command, (0x01u << 24) | (GBI::MTX_LOAD << 16) | 0x40,
                   0x0E000140u);
        command += 8;
        putCommand(segment, command, 0xBFu << 24, (1u * 10u << 8) | (2u * 10u));
        command += 8;
        putCommand(segment, command, 0xB8u << 24, 0);

        SegmentTable table;
        table.rom_span = std::span(segment);
        table.loadSegment(0x0E, 0, static_cast<uint32_t>(segment.size()));
        WarningLog warnings;
        GBI::DLInterpreter interpreter(table, warnings);
        const GBI::Mesh &mesh = interpreter.run(SegmentedAddress {0x0E, 0}, true);
        check(mesh.vertices.size() == 3, "G_VTX timing triangle vertices");
        if (mesh.vertices.size() == 3) {
            check(mesh.vertices[0].position[0] == 11.0f,
                  "G_VTX caches position before later matrix");
            check(mesh.vertices[0].uv[0] == 31.0f / 32.0f,
                  "G_VTX caches texture scale before later change");
        }
    }

    // Projection matrices use the same load/multiply semantics as the RSP
    // model-view stack. The second matrix must affect vertices already loaded
    // after both commands, rather than silently replacing the first matrix.
    {
        std::vector<uint8_t> segment(0x300, 0);
        FixedMatrix projection_load = identityMatrix();
        projection_load[0][0] = fixedFromInteger(2);
        FixedMatrix projection_multiply = identityMatrix();
        projection_multiply[1][1] = fixedFromInteger(3);
        putMatrix(segment, 0x100, projection_load);
        putMatrix(segment, 0x140, projection_multiply);
        putVertex(segment, 0x180, 1, 2, 3, 0, 0);
        putVertex(segment, 0x190, 0, 1, 0, 0, 0);
        putVertex(segment, 0x1A0, 0, 0, 1, 0, 0);

        size_t command = 0;
        putCommand(segment, command, (0x01u << 24)
                       | ((GBI::MTX_PROJECTION | GBI::MTX_LOAD) << 16) | 0x40,
                   0x0E000100u);
        command += 8;
        putCommand(segment, command, (0x01u << 24)
                       | (GBI::MTX_PROJECTION << 16) | 0x40,
                   0x0E000140u);
        command += 8;
        putCommand(segment, command, (0x04u << 24) | (0x20u << 16) | 48u,
                   0x0E000180u);
        command += 8;
        putCommand(segment, command, 0xBFu << 24, (1u * 10u << 8) | (2u * 10u));
        command += 8;
        putCommand(segment, command, 0xB8u << 24, 0);

        SegmentTable table;
        table.rom_span = std::span(segment);
        table.loadSegment(0x0E, 0, static_cast<uint32_t>(segment.size()));
        WarningLog warnings;
        GBI::DLInterpreter interpreter(table, warnings);
        const GBI::Mesh &mesh = interpreter.run(SegmentedAddress {0x0E, 0}, true);
        check(mesh.vertices.size() == 3, "projection composition triangle vertices");
        if (mesh.vertices.size() == 3) {
            check(mesh.vertices[0].projected, "projection marks vertex as projected");
            check(mesh.vertices[0].clip_position[0] == 2.0f
                      && mesh.vertices[0].clip_position[1] == 6.0f
                      && mesh.vertices[0].clip_position[2] == 3.0f
                      && mesh.vertices[0].clip_position[3] == 1.0f,
                  "projection load and multiply compose in fixed point");
        }
    }

    // A graph projection context supplies the camera projection and the RSP
    // viewport outside the user display list. G_MOVEMEM viewport data must
    // still override the context when a DL provides it explicitly.
    {
        std::vector<uint8_t> segment(0x280, 0);
        const int16_t viewport_values[] {640, 480, 511, 0, 640, 480, 511, 0};
        for (size_t i = 0; i < std::size(viewport_values); i++) {
            const uint16_t value = static_cast<uint16_t>(viewport_values[i]);
            segment[0x80 + i * 2] = static_cast<uint8_t>(value >> 8);
            segment[0x80 + i * 2 + 1] = static_cast<uint8_t>(value);
        }
        putVertex(segment, 0x180, 2, 4, 2, 0, 0);
        putVertex(segment, 0x190, 0, 1, 0, 0, 0);
        putVertex(segment, 0x1A0, 0, 0, 1, 0, 0);

        size_t command = 0;
        putCommand(segment, command, (0x03u << 24) | (0x80u << 16) | 16u,
                   0x0E000080u);
        command += 8;
        putCommand(segment, command, (0xBCu << 24) | GBI::G_MW_PERSPNORM, 4u);
        command += 8;
        putCommand(segment, command, (0x04u << 24) | (0x20u << 16) | 48u,
                   0x0E000180u);
        command += 8;
        putCommand(segment, command, 0xBFu << 24, (1u * 10u << 8) | (2u * 10u));
        command += 8;
        putCommand(segment, command, 0xB8u << 24, 0);

        GBI::ProjectionContext context;
        context.valid = true;
        context.viewport.valid = true;
        context.fixed_projection_matrix = identityMatrix();
        context.fixed_projection_matrix[0][0] = 0x00008000; // x / 2
        context.fixed_projection_matrix[1][1] = 0x00004000; // y / 4
        context.fixed_projection_matrix[2][2] = 0x00008000; // z / 2
        context.persp_norm = 4;

        SegmentTable table;
        table.rom_span = std::span(segment);
        table.loadSegment(0x0E, 0, static_cast<uint32_t>(segment.size()));
        WarningLog warnings;
        GBI::DLInterpreter interpreter(table, warnings);
        const GBI::Mesh &mesh = interpreter.run(
            SegmentedAddress {0x0E, 0}, true, 0, mtxfIdentity(), identityMatrix(), context);
        check(mesh.vertices.size() == 3, "projection context triangle vertices");
        if (mesh.vertices.size() == 3) {
            check(mesh.vertices[0].projected, "projection context marks vertex projected");
            check(std::abs(mesh.vertices[0].ndc_position[0] - 0.999878f) < 0.0001f
                      && std::abs(mesh.vertices[0].ndc_position[1] - 0.999878f) < 0.0001f
                      && std::abs(mesh.vertices[0].ndc_position[2] - 0.999878f) < 0.0001f,
                  "projection context fixed NDC");
            check(std::abs(mesh.vertices[0].viewport_position[0] - 319.980469f) < 0.01f
                      && std::abs(mesh.vertices[0].viewport_position[1] - 239.985352f) < 0.01f
                      && std::abs(mesh.vertices[0].viewport_position[2] - 255.484406f) < 0.01f,
                  "RSP viewport scale and translation");
        }
    }

    // The extracted camera view is applied before the fixed projection, and
    // viewport Z maps NDC [-1, 1] to the RSP's [0, 255.5] range.
    {
        std::vector<uint8_t> segment(0x280, 0);
        putVertex(segment, 0x180, 10, 0, -1, 0, 0);
        putVertex(segment, 0x190, 10, 0, 0, 0, 0);
        putVertex(segment, 0x1A0, 10, 0, 1, 0, 0);

        size_t command = 0;
        putCommand(segment, command, (0x04u << 24) | (0x20u << 16) | 48u,
                   0x0E000180u);
        command += 8;
        putCommand(segment, command, 0xBFu << 24, (1u * 10u << 8) | (2u * 10u));
        command += 8;
        putCommand(segment, command, 0xB8u << 24, 0);

        GBI::ProjectionContext context;
        context.valid = true;
        context.viewport.valid = true;
        const Fast3D::Fixed half_max_z = fixedFromInteger(511) / 4;
        context.viewport.scale[0] = kFixedOne;
        context.viewport.scale[1] = kFixedOne;
        context.viewport.scale[2] = half_max_z;
        context.viewport.translate[0] = 0;
        context.viewport.translate[1] = 0;
        context.viewport.translate[2] = half_max_z;
        context.fixed_view_matrix = identityMatrix();
        context.fixed_view_matrix[3][0] = fixedFromInteger(-10);
        context.fixed_projection_matrix = identityMatrix();
        context.persp_norm = 4;

        SegmentTable table;
        table.rom_span = std::span(segment);
        table.loadSegment(0x0E, 0, static_cast<uint32_t>(segment.size()));
        WarningLog warnings;
        GBI::DLInterpreter interpreter(table, warnings);
        const GBI::Mesh &mesh = interpreter.run(
            SegmentedAddress {0x0E, 0}, true, 0, mtxfIdentity(), identityMatrix(), context);
        check(mesh.vertices.size() == 3, "camera/depth triangle vertices");
        if (mesh.vertices.size() == 3) {
            check(mesh.vertices[0].clip_position[0] == 0.0f
                      && mesh.vertices[1].clip_position[0] == 0.0f
                      && mesh.vertices[2].clip_position[0] == 0.0f,
                  "camera view translation precedes projection");
            check(std::abs(mesh.vertices[0].viewport_position[2] - 0.015594f) < 0.01f
                      && std::abs(mesh.vertices[1].viewport_position[2] - 127.75f) < 0.01f
                      && std::abs(mesh.vertices[2].viewport_position[2] - 255.484406f) < 0.01f,
                  "RSP viewport depth maps NDC in order");
        }
    }

    // The transformed-light cache must be rebuilt after G_MTX. A negative X
    // matrix turns the directional light away from the X-facing vertex, so
    // only the ambient color should remain.
    {
        std::vector<uint8_t> segment(0x280, 0);
        FixedMatrix mirror = identityMatrix();
        mirror[0][0] = -kFixedOne;
        putMatrix(segment, 0x140, identityMatrix());
        putMatrix(segment, 0x100, mirror);
        putLight(segment, 0x80, 100, 80, 60, 127, 0, 0);
        putLight(segment, 0x90, 10, 20, 30, 0, 0, 0);
        putVertex(segment, 0x180, 1, 2, 3, 0, 0);
        putVertex(segment, 0x1B0, 1, 2, 3, 0, 0);

        size_t command = 0;
        putCommand(segment, command, (0x01u << 24) | (GBI::MTX_LOAD << 16) | 0x40,
                   0x0E000100u);
        command += 8;
        putCommand(segment, command, (0x01u << 24)
                       | ((GBI::MTX_LOAD | GBI::MTX_PUSH) << 16) | 0x40,
                   0x0E000140u);
        command += 8;
        putCommand(segment, command, (0x03u << 24) | (0x86u << 16) | 16u,
                   0x0E000080u);
        command += 8;
        putCommand(segment, command, (0x03u << 24) | (0x88u << 16) | 16u,
                   0x0E000090u);
        command += 8;
        putCommand(segment, command, (0xBCu << 24) | 0x02u, 64u);
        command += 8;
        putCommand(segment, command, (0x04u << 24) | (0x01u << 16) | 16u,
                   0x0E0001B0u);
        command += 8;
        putCommand(segment, command, 0xBDu << 24, 0);
        command += 8;
        putCommand(segment, command, (0x04u << 24) | (0x20u << 16) | 48u,
                   0x0E000180u);
        command += 8;
        putCommand(segment, command, 0xBFu << 24, (1u * 10u << 8) | (2u * 10u));
        command += 8;
        putCommand(segment, command, 0xB8u << 24, 0);

        SegmentTable table;
        table.rom_span = std::span(segment);
        table.loadSegment(0x0E, 0, static_cast<uint32_t>(segment.size()));
        WarningLog warnings;
        GBI::DLInterpreter interpreter(table, warnings);
        const GBI::Mesh &mesh = interpreter.run(SegmentedAddress {0x0E, 0}, true);
        check(mesh.vertices.size() == 3, "fixed lighting triangle vertices");
        if (mesh.vertices.size() == 3) {
            const auto &shade = mesh.vertices[0].shade;
            check(shade[0] == 10 && shade[1] == 20 && shade[2] == 30 && shade[3] == 0x5A,
                  "transformed light invalidation and alpha preservation");
            check(mesh.vertices[0].shade_valid, "exact shade is marked valid");
        }
    }

    printf("test_fast3d_fixed: decode, row-vector multiply, VMAD accumulator, "
           "saturation, vector, light, and G_VTX timing helpers checked\n");
}
