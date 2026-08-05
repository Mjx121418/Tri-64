#include "godot_bridge.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

using namespace godot;

namespace {

// 每个材质一个网格字典（与 OBJ 导出的 usemtl 分组一致），方便每个
// MeshInstance3D 单独挂材质。
Array meshDicts(const GBI::Mesh &mesh) {
    Array out;
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

// 与 meshDicts 的 material 一一对应：{ textured, color, tex_width, tex_height,
// tex_pixels }。textures 与 mesh.materials 并行。
Array materialDicts(const GBI::Mesh &mesh, const std::vector<GBI::Texture> &textures) {
    Array out;
    for (size_t m = 0; m < mesh.materials.size(); m++) {
        Dictionary d;
        const GBI::Material &state = mesh.materials[m];
        const bool has_tex = m < textures.size() && !textures[m].pixels.empty();
        d["textured"] = has_tex;
        d["color"] = Color(state.prim_color[0] / 255.0f, state.prim_color[1] / 255.0f,
                           state.prim_color[2] / 255.0f);
        if (has_tex) {
            const GBI::Texture &tex = textures[m];
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

} // namespace

void GodotBridge::_bind_methods() {
    ClassDB::bind_method(D_METHOD("loadROM", "path"), &GodotBridge::loadROM);
    ClassDB::bind_method(D_METHOD("ROMLoaded"), &GodotBridge::ROMLoaded);
    ClassDB::bind_method(D_METHOD("getLevelAreas", "level_num"), &GodotBridge::getLevelAreas);
    ClassDB::bind_method(D_METHOD("extractLevel", "level_num", "area_index"),
                         &GodotBridge::extractLevel);
    ClassDB::bind_method(D_METHOD("getMeshes"), &GodotBridge::getMeshes);
    ClassDB::bind_method(D_METHOD("getMaterials"), &GodotBridge::getMaterials);
    ClassDB::bind_method(D_METHOD("getObjects"), &GodotBridge::getObjects);
    ClassDB::bind_method(D_METHOD("getObjectModels"), &GodotBridge::getObjectModels);
    ClassDB::bind_method(D_METHOD("getCollisionTriangles"), &GodotBridge::getCollisionTriangles);
    ClassDB::bind_method(D_METHOD("getLevelName"), &GodotBridge::getLevelName);
    ClassDB::bind_method(D_METHOD("getLevelNameFor", "level_num"),
                         &GodotBridge::getLevelNameFor);
    ClassDB::bind_method(D_METHOD("getAllLevelNames"), &GodotBridge::getAllLevelNames);
    ClassDB::bind_method(D_METHOD("getMarioStartPos"), &GodotBridge::getMarioStartPos);
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

PackedInt32Array GodotBridge::getLevelAreas(int level_num) {
    PackedInt32Array out;
    if (!rom.is_loaded) {
        return out;
    }
    for (int area : LevelExtract::listAreas(rom, level_num)) {
        out.push_back(area);
    }
    return out;
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
    return meshDicts(result_.mesh);
}

Array GodotBridge::getMaterials() {
    return materialDicts(result_.mesh, result_.textures);
}

Array GodotBridge::getObjects() {
    Array out;
    for (const auto &obj : result_.objects) {
        Dictionary d;
        d["pos"] = Vector3(obj.start_pos.x, obj.start_pos.y, obj.start_pos.z);
        // SM64 角度单位 → 弧度，供 Godot 的旋转直接使用
        d["angle"] = Vector3(sm64AngleToRadians(obj.start_angle.x),
                             sm64AngleToRadians(obj.start_angle.y),
                             sm64AngleToRadians(obj.start_angle.z));
        d["model"] = static_cast<int64_t>(obj.model_id);
        d["behavior_arg"] = static_cast<int64_t>(obj.behavior_arg);
        d["behavior"] = static_cast<int64_t>((uint32_t(obj.behavior_script.seg) << 24) |
                                              (obj.behavior_script.offset & 0xFFFFFF));
        out.push_back(d);
    }
    return out;
}

// 对象模型按 model id 去重，每个模型一份（网格 + 材质），所有实例共享。
Array GodotBridge::getObjectModels() {
    Array out;
    for (const auto &[model_id, model] : result_.object_models) {
        Dictionary d;
        d["model"] = static_cast<int64_t>(model_id);
        d["meshes"] = meshDicts(model.mesh);
        d["materials"] = materialDicts(model.mesh, model.textures);
        out.push_back(d);
    }
    return out;
}

// 当前区域的静态碰撞三角形（平坦着色蓝色）。
Dictionary GodotBridge::getCollisionTriangles() {
    Dictionary d;
    const Collision::TriangleMesh mesh = Collision::buildTriangleMesh(result_.collision);

    PackedVector3Array verts;
    PackedVector3Array normals;
    PackedInt32Array indices;
    verts.resize(static_cast<int>(mesh.positions.size()));
    normals.resize(static_cast<int>(mesh.normals.size()));
    for (size_t i = 0; i < mesh.positions.size(); i++) {
        verts.set(i, Vector3(mesh.positions[i].x, mesh.positions[i].y, mesh.positions[i].z));
    }
    for (size_t i = 0; i < mesh.normals.size(); i++) {
        normals.set(i, Vector3(mesh.normals[i].x, mesh.normals[i].y, mesh.normals[i].z));
    }
    indices.resize(static_cast<int>(mesh.indices.size()));
    for (size_t i = 0; i < mesh.indices.size(); i++) {
        indices.set(i, static_cast<int32_t>(mesh.indices[i]));
    }

    d["vertices"] = verts;
    d["normals"] = normals;
    d["indices"] = indices;
    return d;
}

String GodotBridge::getLevelName() {
    return String(result_.level_name.c_str());
}

String GodotBridge::getLevelNameFor(int level_num) {
    if (!rom.is_loaded) {
        return String();
    }
    std::string name = LevelExtract::extractLevelName(rom, level_num);
    return String(name.c_str());
}

Dictionary GodotBridge::getAllLevelNames() {
    Dictionary out;
    if (!rom.is_loaded) {
        return out;
    }
    auto names = LevelExtract::loadAllLevelNames(rom);
    for (const auto &[lv, name] : names) {
        out[static_cast<int64_t>(lv)] = String(name.c_str());
    }
    return out;
}

Dictionary GodotBridge::getMarioStartPos() {
    Dictionary d;
    d["pos"] = Vector3(result_.mario_start_pos.x, result_.mario_start_pos.y,
                        result_.mario_start_pos.z);
    d["angle_y"] = static_cast<double>(result_.mario_start_angle_y);
    return d;
}
