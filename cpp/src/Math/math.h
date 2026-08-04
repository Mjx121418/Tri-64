#ifndef MATH_H
#define MATH_H

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

template<Number T>
using Mat4 = T[4][4];

#endif /* MATH_H */
