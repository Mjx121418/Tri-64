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

Mtxf mtxfLookAt(const Vec3<float> &from, const Vec3<float> &to, int16_t roll) {
    // Mirrors decomp math_util.c:194 (mtxf_lookat). The matrix is built in
    // the same row-vector convention as mtxf_mul_vec3s.
    const float dx = to.x - from.x;
    const float dz = to.z - from.z;
    const float horizontal_length = std::sqrt(dx * dx + dz * dz);
    if (horizontal_length < 1e-6f) {
        return mtxfIdentity();
    }

    const float horizontal_inverse = -1.0f / horizontal_length;
    const float normalized_dx = dx * horizontal_inverse;
    const float normalized_dz = dz * horizontal_inverse;
    const float roll_radians = sm64AngleToRadians(roll);
    const float sin_roll = std::sin(roll_radians);
    const float cos_roll = std::cos(roll_radians);

    float x_col_y = sin_roll * normalized_dz;
    float y_col_y = cos_roll;
    float z_col_y = -sin_roll * normalized_dx;

    float x_col_z = to.x - from.x;
    float y_col_z = to.y - from.y;
    float z_col_z = to.z - from.z;
    const float view_length = std::sqrt(
        x_col_z * x_col_z + y_col_z * y_col_z + z_col_z * z_col_z);
    if (view_length < 1e-6f) {
        return mtxfIdentity();
    }
    const float view_inverse = -1.0f / view_length;
    x_col_z *= view_inverse;
    y_col_z *= view_inverse;
    z_col_z *= view_inverse;

    float x_col_x = y_col_y * z_col_z - z_col_y * y_col_z;
    float y_col_x = z_col_y * x_col_z - x_col_y * z_col_z;
    float z_col_x = x_col_y * y_col_z - y_col_y * x_col_z;
    const float x_length = std::sqrt(
        x_col_x * x_col_x + y_col_x * y_col_x + z_col_x * z_col_x);
    if (x_length < 1e-6f) {
        return mtxfIdentity();
    }
    const float x_inverse = 1.0f / x_length;
    x_col_x *= x_inverse;
    y_col_x *= x_inverse;
    z_col_x *= x_inverse;

    x_col_y = y_col_z * z_col_x - z_col_z * y_col_x;
    y_col_y = z_col_z * x_col_x - x_col_z * z_col_x;
    z_col_y = x_col_z * y_col_x - y_col_z * x_col_x;
    const float y_length = std::sqrt(
        x_col_y * x_col_y + y_col_y * y_col_y + z_col_y * z_col_y);
    if (y_length < 1e-6f) {
        return mtxfIdentity();
    }
    const float y_inverse = 1.0f / y_length;
    x_col_y *= y_inverse;
    y_col_y *= y_inverse;
    z_col_y *= y_inverse;

    Mtxf matrix {};
    matrix[0][0] = x_col_x;
    matrix[1][0] = y_col_x;
    matrix[2][0] = z_col_x;
    matrix[3][0] = -(from.x * x_col_x + from.y * y_col_x + from.z * z_col_x);
    matrix[0][1] = x_col_y;
    matrix[1][1] = y_col_y;
    matrix[2][1] = z_col_y;
    matrix[3][1] = -(from.x * x_col_y + from.y * y_col_y + from.z * z_col_y);
    matrix[0][2] = x_col_z;
    matrix[1][2] = y_col_z;
    matrix[2][2] = z_col_z;
    matrix[3][2] = -(from.x * x_col_z + from.y * y_col_z + from.z * z_col_z);
    matrix[3][3] = 1.0f;
    return matrix;
}

Mtxf mtxfPerspective(float fov_degrees, float aspect, float near_plane,
                     float far_plane) {
    Mtxf matrix {};
    if (aspect <= 0.0f || near_plane <= 0.0f || far_plane <= near_plane) {
        return mtxfIdentity();
    }
    constexpr float pi = 3.14159265358979323846f;
    const float half_fov = fov_degrees * pi / 360.0f;
    const float sine = std::sin(half_fov);
    if (std::abs(sine) < 1e-6f) {
        return mtxfIdentity();
    }
    const float cotangent = std::cos(half_fov) / sine;
    const float range = far_plane - near_plane;
    matrix[0][0] = cotangent / aspect;
    matrix[1][1] = cotangent;
    matrix[2][2] = -(far_plane + near_plane) / range;
    matrix[2][3] = -1.0f;
    matrix[3][2] = -(2.0f * far_plane * near_plane) / range;
    return matrix;
}

Mtxf mtxfOrtho(float left, float right, float bottom, float top,
               float near_plane, float far_plane) {
    Mtxf matrix {};
    const float width = right - left;
    const float height = top - bottom;
    const float depth = far_plane - near_plane;
    if (std::abs(width) < 1e-6f || std::abs(height) < 1e-6f
        || std::abs(depth) < 1e-6f) {
        return mtxfIdentity();
    }
    matrix[0][0] = 2.0f / width;
    matrix[1][1] = 2.0f / height;
    matrix[2][2] = -2.0f / depth;
    matrix[3][0] = -(right + left) / width;
    matrix[3][1] = -(top + bottom) / height;
    matrix[3][2] = -(far_plane + near_plane) / depth;
    matrix[3][3] = 1.0f;
    return matrix;
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

Mtxf mtxfRotationXYZ(Vec3<int16_t> rotation) {
    const float sx = std::sin(sm64AngleToRadians(rotation.x));
    const float cx = std::cos(sm64AngleToRadians(rotation.x));
    const float sy = std::sin(sm64AngleToRadians(rotation.y));
    const float cy = std::cos(sm64AngleToRadians(rotation.y));
    const float sz = std::sin(sm64AngleToRadians(rotation.z));
    const float cz = std::cos(sm64AngleToRadians(rotation.z));

    // 与 decomp 的 mtxf_rotate_xyz_and_translate 的旋转部分一致
    //（geo_process_animated_part 用它，旋转顺序 X→Y→Z）。
    Mtxf m = mtxfIdentity();
    m[0][0] = cy * cz;
    m[0][1] = cy * sz;
    m[0][2] = -sy;
    m[1][0] = sx * sy * cz - cx * sz;
    m[1][1] = sx * sy * sz + cx * cz;
    m[1][2] = sx * cy;
    m[2][0] = cx * sy * cz + sx * sz;
    m[2][1] = cx * sy * sz - sx * cz;
    m[2][2] = cx * cy;
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

Mtxf mtxfInverse(const Mtxf &m) {
    const float a00 = m[0][0];
    const float a01 = m[0][1];
    const float a02 = m[0][2];
    const float a10 = m[1][0];
    const float a11 = m[1][1];
    const float a12 = m[1][2];
    const float a20 = m[2][0];
    const float a21 = m[2][1];
    const float a22 = m[2][2];

    const float c00 = a11 * a22 - a12 * a21;
    const float c01 = a02 * a21 - a01 * a22;
    const float c02 = a01 * a12 - a02 * a11;
    const float c10 = a12 * a20 - a10 * a22;
    const float c11 = a00 * a22 - a02 * a20;
    const float c12 = a02 * a10 - a00 * a12;
    const float c20 = a10 * a21 - a11 * a20;
    const float c21 = a01 * a20 - a00 * a21;
    const float c22 = a00 * a11 - a01 * a10;
    const float determinant = a00 * c00 + a01 * c10 + a02 * c20;
    if (std::abs(determinant) < 1e-8f) {
        return mtxfIdentity();
    }

    const float inverse_determinant = 1.0f / determinant;
    Mtxf inverse = mtxfIdentity();
    inverse[0][0] = c00 * inverse_determinant;
    inverse[0][1] = c01 * inverse_determinant;
    inverse[0][2] = c02 * inverse_determinant;
    inverse[1][0] = c10 * inverse_determinant;
    inverse[1][1] = c11 * inverse_determinant;
    inverse[1][2] = c12 * inverse_determinant;
    inverse[2][0] = c20 * inverse_determinant;
    inverse[2][1] = c21 * inverse_determinant;
    inverse[2][2] = c22 * inverse_determinant;
    for (size_t j = 0; j < 3; j++) {
        inverse[3][j] = -(m[3][0] * inverse[0][j] + m[3][1] * inverse[1][j] +
                          m[3][2] * inverse[2][j]);
    }
    return inverse;
}
