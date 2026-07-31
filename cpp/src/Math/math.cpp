#include "math.h"

Vec3<int16_t> readVec3s(std::span<const uint8_t> data, size_t offset) {
    Vec3<int16_t> res;
    res.x = readInt<int16_t>(data, offset);
    res.y = readInt<int16_t>(data, offset+2);
    res.z = readInt<int16_t>(data, offset+4);
    return res;
}

Vec3<int16_t> readVec3sAngle(std::span<const uint8_t> data, size_t offset) {
    Vec3<int16_t> res;
    res.x = (readInt<int16_t>(data, offset) << 15) / 180;
    res.y = (readInt<int16_t>(data, offset+2) << 15) / 180;
    res.z = (readInt<int16_t>(data, offset+4) << 15) / 180;
    return res;
}

Vec3<float> readVec3sToVec3f(std::span<const uint8_t> data, size_t offset) {
    Vec3<float> res;
    res.x = static_cast<float>(readInt<int16_t>(data, offset));
    res.y = static_cast<float>(readInt<int16_t>(data, offset+2));
    res.z = static_cast<float>(readInt<int16_t>(data, offset+4));
    return res;
}
