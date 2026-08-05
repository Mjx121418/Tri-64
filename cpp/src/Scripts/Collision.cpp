#include "Scripts/Collision.h"

#include "Math/math.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <span>

namespace Collision {

namespace {

// 地形命令（surface_terrains.h）
constexpr int16_t kTerrainLoadVertices = 0x0040;
constexpr int16_t kTerrainLoadContinue = 0x0041;
constexpr int16_t kTerrainLoadEnd = 0x0042;
constexpr int16_t kTerrainLoadObjects = 0x0043;
constexpr int16_t kTerrainLoadEnvironment = 0x0044;

bool isSurfaceType(int16_t cmd) {
    return cmd < 0x40 || cmd >= 0x65;
}

// 特殊对象 preset id → SPTYPE（额外参数数量）。
// 按 decomp 的 enum SpecialPresets 从 special_presets.inc.c 生成；
// 未知 id 默认 SPTYPE_NO_YROT_OR_PARAMS（0 个额外参数）。
constexpr std::array<uint8_t, 0x100> kSpecialPresetTypes {
    1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0,
    0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1,
    1, 1, 1, 2, 2, 2, 2, 2, 2, 1, 4, 4, 4, 4, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// 类型是否带力/参数（surface_has_force，surface_load.c）
bool surfaceHasForce(int16_t type) {
    switch (type) {
        case 0x0004: // SURFACE_0004
        case 0x000E: // SURFACE_FLOWING_WATER
        case 0x0024: // SURFACE_DEEP_MOVING_QUICKSAND
        case 0x0025: // SURFACE_SHALLOW_MOVING_QUICKSAND
        case 0x0027: // SURFACE_MOVING_QUICKSAND
        case 0x002C: // SURFACE_HORIZONTAL_WIND
        case 0x002D: // SURFACE_INSTANT_MOVING_QUICKSAND
            return true;
        default:
            return false;
    }
}

// 带边界检查的 s16 读取游标。
struct Reader {
    std::span<const uint8_t> data;
    size_t pos {0};
    bool ok {true};

    int16_t next() {
        if (pos + 2 > data.size()) {
            ok = false;
            return 0;
        }
        const int16_t v = readInt<int16_t>(data, pos);
        pos += 2;
        return v;
    }
};

} // namespace

Data decode(const SegmentTable &seg_table, SegmentedAddress terrain, SegmentedAddress rooms) {
    Data data;
    if (terrain.seg < 0 || terrain.seg > 31) {
        data.error = "invalid terrain segment";
        return data;
    }

    std::span<const uint8_t> terrain_data;
    try {
        terrain_data = seg_table.data(terrain);
    } catch (const std::out_of_range &) {
        data.error = "terrain segment not loaded";
        return data;
    }

    std::span<const uint8_t> rooms_data;
    if (rooms.seg >= 0 && rooms.seg <= 31) {
        try {
            rooms_data = seg_table.data(rooms);
        } catch (const std::out_of_range &) {
            rooms_data = {};
        }
    }

    Reader reader {terrain_data};
    size_t rooms_pos = 0;
    bool done = false;

    while (!done && reader.ok) {
        const int16_t cmd = reader.next();
        if (!reader.ok) {
            break;
        }

        if (isSurfaceType(cmd)) {
            const int16_t count = reader.next();
            for (int16_t i = 0; i < count && reader.ok; i++) {
                Surface s;
                s.type = static_cast<uint16_t>(cmd);
                s.v1 = static_cast<uint32_t>(reader.next());
                s.v2 = static_cast<uint32_t>(reader.next());
                s.v3 = static_cast<uint32_t>(reader.next());
                s.force = surfaceHasForce(cmd) ? reader.next() : 0;
                if (rooms_pos + 1 <= rooms_data.size()) {
                    s.room = static_cast<int8_t>(rooms_data[rooms_pos]);
                    s.has_room = true;
                    rooms_pos++;
                }
                data.surfaces.push_back(s);
            }
        } else if (cmd == kTerrainLoadVertices) {
            const int16_t count = reader.next();
            data.vertices.clear();
            data.vertices.reserve(static_cast<size_t>(count));
            for (int16_t i = 0; i < count && reader.ok; i++) {
                Vertex v;
                v.x = reader.next();
                v.y = reader.next();
                v.z = reader.next();
                data.vertices.push_back(v);
            }
        } else if (cmd == kTerrainLoadContinue) {
            continue;
        } else if (cmd == kTerrainLoadEnd) {
            done = true;
        } else if (cmd == kTerrainLoadObjects) {
            const int16_t count = reader.next();
            for (int16_t i = 0; i < count && reader.ok; i++) {
                SpecialObject obj;
                obj.preset = static_cast<uint8_t>(reader.next());
                obj.x = reader.next();
                obj.y = reader.next();
                obj.z = reader.next();
                obj.yaw = 0;
                obj.param = 0;
                // 额外参数数量由 preset 的 SPTYPE 决定（special_presets.inc.c）
                const uint8_t type = kSpecialPresetTypes[obj.preset];
                if (type >= 1) {
                    obj.yaw = reader.next();
                }
                if (type >= 2) {
                    obj.param = reader.next();
                }
                if (type >= 3) {
                    reader.next(); // SPTYPE_UNKNOWN 的 3 个额外 s16
                    reader.next();
                    reader.next();
                }
                data.special_objects.push_back(obj);
            }
        } else if (cmd == kTerrainLoadEnvironment) {
            const int16_t count = reader.next();
            for (int16_t i = 0; i < count && reader.ok; i++) {
                WaterBox box;
                box.id = reader.next();
                box.x1 = reader.next();
                box.z1 = reader.next();
                box.x2 = reader.next();
                box.z2 = reader.next();
                box.y = reader.next();
                data.water_boxes.push_back(box);
            }
        } else {
            data.error = "unknown terrain command 0x" + [&]() {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%02X", cmd & 0xFF);
                return std::string(buf);
            }();
            return data;
        }
    }

    if (!reader.ok && !done) {
        data.error = "terrain data out of range";
        return data;
    }

    data.ok = true;
    return data;
}

TriangleMesh buildTriangleMesh(const Data &data) {
    TriangleMesh mesh;
    for (const auto &s : data.surfaces) {
        if (s.v1 >= data.vertices.size() || s.v2 >= data.vertices.size() ||
            s.v3 >= data.vertices.size()) {
            continue;
        }
        const Vec3<float> a {static_cast<float>(data.vertices[s.v1].x),
                             static_cast<float>(data.vertices[s.v1].y),
                             static_cast<float>(data.vertices[s.v1].z)};
        const Vec3<float> b {static_cast<float>(data.vertices[s.v2].x),
                             static_cast<float>(data.vertices[s.v2].y),
                             static_cast<float>(data.vertices[s.v2].z)};
        const Vec3<float> c {static_cast<float>(data.vertices[s.v3].x),
                             static_cast<float>(data.vertices[s.v3].y),
                             static_cast<float>(data.vertices[s.v3].z)};
        // 面法线 = (b-a) x (c-a)，归一化；退化三角形跳过
        Vec3<float> n {
            (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
            (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
            (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x),
        };
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len < 1e-6f) {
            continue;
        }
        n.x /= len;
        n.y /= len;
        n.z /= len;

        const uint32_t base = static_cast<uint32_t>(mesh.positions.size());
        mesh.positions.push_back(a);
        mesh.positions.push_back(b);
        mesh.positions.push_back(c);
        mesh.normals.push_back(n);
        mesh.normals.push_back(n);
        mesh.normals.push_back(n);
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
    }
    return mesh;
}

} // namespace Collision
