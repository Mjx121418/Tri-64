#include "godot_bridge.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

using namespace godot;

void GodotBridge::_bind_methods() {
    ClassDB::bind_method(D_METHOD("loadROM", "path"), &GodotBridge::loadROM);
    ClassDB::bind_method(D_METHOD("ROMLoaded"), &GodotBridge::ROMLoaded);
    ClassDB::bind_method(D_METHOD("extractLevel", "level_num", "area_index"),
                         &GodotBridge::extractLevel);
    ClassDB::bind_method(D_METHOD("getMeshes"), &GodotBridge::getMeshes);
    ClassDB::bind_method(D_METHOD("getMaterials"), &GodotBridge::getMaterials);
    ClassDB::bind_method(D_METHOD("getObjects"), &GodotBridge::getObjects);
}

GodotBridge::GodotBridge() {
}

void GodotBridge::loadROM(String path) {
    std::string utf8_path = path.utf8().get_data();
    std::filesystem::path fs_path(std::u8string(reinterpret_cast<const char8_t *>(utf8_path.data()),
                                                utf8_path.size()));
    rom.load(fs_path);
    result_ = {};
}

bool GodotBridge::ROMLoaded() {
    return rom.is_loaded;
}

bool GodotBridge::extractLevel(int level_num, int area_index) {
    result_ = {};
    if (!rom.is_loaded) {
        return false;
    }
    result_ = LevelExtract::extract(rom, level_num, area_index);
    return result_.ok;
}

// 每个材质一个网格（与 OBJ 导出的 usemtl 分组一致），方便每个
// MeshInstance3D 单独挂材质。
Array GodotBridge::getMeshes() {
    Array out;
    const GBI::Mesh &mesh = result_.mesh;
    for (size_t m = 0; m < mesh.materials.size(); m++) {
        Dictionary d;
        PackedVector3Array verts;
        PackedVector3Array normals;
        PackedVector2Array uvs;
        PackedInt32Array indices;

        for (size_t t = 0; t < mesh.material_ids.size(); t++) {
            if (mesh.material_ids[t] != m) {
                continue;
            }
            const int32_t base = verts.size();
            for (uint32_t k = 0; k < 3; k++) {
                const GBI::MeshVertex &v = mesh.vertices[mesh.indices[t * 3 + k]];
                verts.push_back(Vector3(v.position[0], v.position[1], v.position[2]));
                normals.push_back(Vector3(v.normal[0], v.normal[1], v.normal[2]));
                uvs.push_back(Vector2(v.uv[0], v.uv[1]));
                indices.push_back(base + static_cast<int32_t>(k));
            }
        }
        if (indices.is_empty()) {
            continue;
        }

        d["vertices"] = verts;
        d["normals"] = normals;
        d["uvs"] = uvs;
        d["indices"] = indices;
        d["material"] = static_cast<int64_t>(m);
        out.push_back(d);
    }
    return out;
}

Array GodotBridge::getMaterials() {
    Array out;
    const GBI::Mesh &mesh = result_.mesh;
    for (size_t m = 0; m < mesh.materials.size(); m++) {
        Dictionary d;
        const GBI::Material &state = mesh.materials[m];
        const bool has_tex = m < result_.textures.size() && !result_.textures[m].pixels.empty();
        d["textured"] = has_tex;
        d["color"] = Color(state.prim_color[0] / 255.0f, state.prim_color[1] / 255.0f,
                           state.prim_color[2] / 255.0f);
        if (has_tex) {
            const GBI::Texture &tex = result_.textures[m];
            d["tex_width"] = static_cast<int64_t>(tex.width);
            d["tex_height"] = static_cast<int64_t>(tex.height);
            PackedByteArray pixels;
            pixels.resize(static_cast<int>(tex.pixels.size()));
            memcpy(pixels.ptrw(), tex.pixels.data(), tex.pixels.size());
            d["tex_pixels"] = pixels;
        } else {
            d["tex_width"] = static_cast<int64_t>(0);
            d["tex_height"] = static_cast<int64_t>(0);
            d["tex_pixels"] = PackedByteArray();
        }
        out.push_back(d);
    }
    return out;
}

Array GodotBridge::getObjects() {
    Array out;
    for (const auto &obj : result_.objects) {
        Dictionary d;
        d["pos"] = Vector3(obj.start_pos.x, obj.start_pos.y, obj.start_pos.z);
        d["angle"] = Vector3(obj.start_angle.x, obj.start_angle.y, obj.start_angle.z);
        d["behavior_arg"] = static_cast<int64_t>(obj.behavior_arg);
        d["behavior"] = static_cast<int64_t>((uint32_t(obj.behavior_script.seg) << 24) |
                                             (obj.behavior_script.offset & 0xFFFFFF));
        out.push_back(d);
    }
    return out;
}
