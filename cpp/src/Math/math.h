#ifndef MATH_H
#define MATH_H

#include <array>
#include <cstdint>
#include <concepts>
#include <cstdio>
#include <span>

template <std::integral T>
T readInt(std::span<const uint8_t> data, size_t offset) {
    using U = std::make_unsigned_t<T>;

    if (offset + sizeof(T) > data.size()) {
        printf("readBigEndian out of range");
        return 0;
    }

    U res = 0;
    for (size_t i {0}; i < sizeof(T); i++) {
        res = (res << 8) | data.at(offset + i);
    }

    return static_cast<T>(res);
}

template<typename T>
concept Number = std::integral<T> || std::floating_point<T>;

template<Number T>
struct Vec3 {
    T x;
    T y;
    T z;
};

Vec3<int16_t> readVec3s(std::span<const uint8_t> data, size_t offset);
Vec3<int16_t> readVec3sAngle(std::span<const uint8_t> data, size_t offset);
Vec3<float> readVec3sToVec3f(std::span<const uint8_t> data, size_t offset);

// 4x4 浮点矩阵（行主序，平移在 m[3][0..2]）。与 GBI::Mtxf 同一布局，
// 供 DL 解释器与对象模型变换烘焙共用。统一用这一个矩阵类型。
using Mtxf = std::array<std::array<float, 4>, 4>;

// 单位阵
Mtxf mtxfIdentity();

// 矩阵乘法 a × b（结果先应用 b 再应用 a）
Mtxf mtxfMul(const Mtxf &a, const Mtxf &b);

// SM64 角度单位 → 弧度（全圈 = 65536，0x8000 = 180°）
float sm64AngleToRadians(int16_t angle);

// 宏/特殊对象出生数据的 yaw（256 一全圈）→ SM64 角度单位。
// 与 decomp 的 convert_rotation（macro_special_objects.c）一致。
int16_t convertRotation(int16_t inRotation);

// 平移 / 缩放矩阵
Mtxf mtxfTranslation(float x, float y, float z);
Mtxf mtxfScale(float s);

// 与 decomp 的 mtxf_rotate_zxy_and_translate 一致（平移为 0），
// rotation 为 SM64 角度单位。
Mtxf mtxfRotationZXY(Vec3<int16_t> rotation);

// 与 decomp 的 mtxf_rotate_xyz_and_translate 一致（旋转部分，平移为 0），
// rotation 为 SM64 角度单位；用于 GEO_ANIMATED_PART 节点。
Mtxf mtxfRotationXYZ(Vec3<int16_t> rotation);

// 用矩阵 m 变换点（含平移，m[3][0..2] 为平移列）
Vec3<float> transformPoint(const Mtxf &m, const Vec3<float> &p);

// 用矩阵 m 的 3x3 线性部分变换方向向量（法线）并归一化；
// 均匀缩放不改变方向，仅改变长度。
Vec3<float> transformNormal(const Mtxf &m, const Vec3<float> &n);

#endif /* MATH_H */
