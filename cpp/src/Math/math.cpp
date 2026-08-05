#include "math.h"

#include <cmath>

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

Mtxf mtxfIdentity() {
    Mtxf m {};
    for (int i = 0; i < 4; i++) {
        m[i][i] = 1.0f;
    }
    return m;
}

Mtxf mtxfMul(const Mtxf &a, const Mtxf &b) {
    Mtxf out {};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a[i][k] * b[k][j];
            }
            out[i][j] = sum;
        }
    }
    return out;
}

float sm64AngleToRadians(int16_t angle) {
    // SM64 角度单位：0x8000 = 180°，全圈 = 65536
    return angle * (2.0f * 3.14159265358979323846f) / 65536.0f;
}

int16_t convertRotation(int16_t inRotation) {
    // 与 decomp 的 convert_rotation 一致（macro_special_objects.c:15）
    uint16_t rotation = static_cast<uint16_t>(static_cast<uint16_t>(inRotation) & 0xFF) << 8;
    if (rotation == 0x3F00) {
        rotation = 0x4000;
    }
    if (rotation == 0x7F00) {
        rotation = 0x8000;
    }
    if (rotation == 0xBF00) {
        rotation = 0xC000;
    }
    if (rotation == 0xFF00) {
        rotation = 0x0000;
    }
    return static_cast<int16_t>(rotation);
}

Mtxf mtxfTranslation(float x, float y, float z) {
    Mtxf m = mtxfIdentity();
    m[3][0] = x;
    m[3][1] = y;
    m[3][2] = z;
    return m;
}

Mtxf mtxfScale(float s) {
    Mtxf m = mtxfIdentity();
    m[0][0] = m[1][1] = m[2][2] = s;
    return m;
}

Mtxf mtxfRotationZXY(Vec3<int16_t> rotation) {
    const float sx = std::sin(sm64AngleToRadians(rotation.x));
    const float cx = std::cos(sm64AngleToRadians(rotation.x));
    const float sy = std::sin(sm64AngleToRadians(rotation.y));
    const float cy = std::cos(sm64AngleToRadians(rotation.y));
    const float sz = std::sin(sm64AngleToRadians(rotation.z));
    const float cz = std::cos(sm64AngleToRadians(rotation.z));

    Mtxf m = mtxfIdentity();
    m[0][0] = cy * cz + sx * sy * sz;
    m[1][0] = -cy * sz + sx * sy * cz;
    m[2][0] = cx * sy;
    m[3][0] = 0.0f;
    m[0][1] = cx * sz;
    m[1][1] = cx * cz;
    m[2][1] = -sx;
    m[3][1] = 0.0f;
    m[0][2] = -sy * cz + sx * cy * sz;
    m[1][2] = sy * sz + sx * cy * cz;
    m[2][2] = cx * cy;
    m[3][2] = 0.0f;
    return m;
}

Vec3<float> transformPoint(const Mtxf &m, const Vec3<float> &p) {
    Vec3<float> out;
    out.x = m[0][0] * p.x + m[1][0] * p.y + m[2][0] * p.z + m[3][0];
    out.y = m[0][1] * p.x + m[1][1] * p.y + m[2][1] * p.z + m[3][1];
    out.z = m[0][2] * p.x + m[1][2] * p.y + m[2][2] * p.z + m[3][2];
    return out;
}

Vec3<float> transformNormal(const Mtxf &m, const Vec3<float> &n) {
    Vec3<float> out;
    out.x = m[0][0] * n.x + m[1][0] * n.y + m[2][0] * n.z;
    out.y = m[0][1] * n.x + m[1][1] * n.y + m[2][1] * n.z;
    out.z = m[0][2] * n.x + m[1][2] * n.y + m[2][2] * n.z;
    const float len = std::sqrt(out.x * out.x + out.y * out.y + out.z * out.z);
    if (len > 1e-6f) {
        out.x /= len;
        out.y /= len;
        out.z /= len;
    }
    return out;
}
