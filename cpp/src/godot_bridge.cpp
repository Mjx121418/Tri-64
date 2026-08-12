#include "godot_bridge.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

using namespace godot;

namespace {

// 每个（材质, 绘制层）一个网格字典（材质表去重不变，层是每三角形的属性：
// geo 节点 flags 高 8 位，0-7；游戏按层升序渲染并在每层设置 render mode）。
// 渲染端按 d["layer"] 做分层材质（0-3 OPA / 4 TEX_EDGE / 5-7 XLU，见 Engine.md）。
Array meshDicts(const GBI::Mesh &mesh, bool exact_shade) {
    Array out;
    for (size_t m = 0; m < mesh.materials.size(); m++) {
        // 顶点色导出规则（按 combine 是否用到 SHADE）：
        // - combine 用到 SHADE（G_CC_MODULATERGB 的 C=SHADE、G_CC_SHADE 的
        //   D=SHADE 等）：受光时顶点第 4 字是法线、shade 已烘焙进顶点色；未受光
        //   时顶点第 4 字就是 RGBA 色。导出让 Godot 用 texture × shade 或 shade。
        // - 纯 PRIMITIVE / ENVIRONMENT（D=PRIMITIVE/ENV）：顶点色不参与，底色取
        //   prim/env 颜色（materialDicts 的 color），不导出顶点色。
        const bool use_vertex_colors =
            GBI::combineUsesShade(mesh.materials[m].combine_w0, mesh.materials[m].combine_w1);

        for (uint8_t layer = 0; layer <= 7; layer++) {
            Dictionary d;
            PackedVector3Array verts;
            PackedVector3Array normals;
            PackedVector2Array uvs;
            PackedInt32Array indices;
            PackedColorArray colors;
            PackedFloat32Array rsp_ndc;
            PackedFloat32Array rsp_viewport;
            PackedFloat32Array rsp_depth;
            PackedFloat32Array rsp_inverse_w;
            PackedInt32Array rsp_clip_codes;
            bool all_projected = true;
            bool has_vertices = false;

            for (size_t t = 0; t < mesh.material_ids.size(); t++) {
                if (mesh.material_ids[t] != m) {
                    continue;
                }
                // 手工构造的网格（movtex 等）没有 triangle_layers，按层 0 处理。
                const uint8_t tri_layer =
                    t < mesh.triangle_layers.size() ? mesh.triangle_layers[t] : 0;
                if (tri_layer != layer) {
                    continue;
                }
                const int32_t base = verts.size();
                for (uint32_t k = 0; k < 3; k++) {
                    const GBI::MeshVertex &v = mesh.vertices[mesh.indices[t * 3 + k]];
                    verts.push_back(Vector3(v.position[0], v.position[1], v.position[2]));
                    normals.push_back(Vector3(v.normal[0], v.normal[1], v.normal[2]));
                    uvs.push_back(Vector2(v.uv[0], v.uv[1]));
                    has_vertices = true;
                    all_projected = all_projected && v.projected;
                    rsp_ndc.push_back(v.ndc_position[0]);
                    rsp_ndc.push_back(v.ndc_position[1]);
                    rsp_ndc.push_back(v.ndc_position[2]);
                    rsp_ndc.push_back(1.0f);
                    rsp_viewport.push_back(v.viewport_position[0]);
                    rsp_viewport.push_back(v.viewport_position[1]);
                    rsp_viewport.push_back(v.viewport_position[2]);
                    rsp_depth.push_back(v.viewport_position[2]);
                    rsp_inverse_w.push_back(v.inverse_w);
                    rsp_clip_codes.push_back(v.clip_code);
                    if (use_vertex_colors) {
                        const uint8_t *shade = v.shade_valid ? v.shade : v.color;
                        colors.push_back(Color(shade[0] / 255.0f, shade[1] / 255.0f,
                                               shade[2] / 255.0f, shade[3] / 255.0f));
                    }
                    indices.push_back(base + static_cast<int32_t>(k));
                }
            }
            if (indices.is_empty()) {
                continue;
            }

            d["vertices"] = verts;
            d["normals"] = normals;
            d["uvs"] = uvs;
            if (use_vertex_colors) {
                d["colors"] = colors;
            }
            d["indices"] = indices;
            d["material"] = static_cast<int64_t>(m);
            d["layer"] = static_cast<int64_t>(layer);
            d["rsp_projected"] = has_vertices && all_projected;
            if (has_vertices && all_projected) {
                d["rsp_ndc"] = rsp_ndc;
                d["rsp_viewport"] = rsp_viewport;
                d["rsp_depth"] = rsp_depth;
                d["rsp_inverse_w"] = rsp_inverse_w;
                d["rsp_clip_codes"] = rsp_clip_codes;
            }
            out.push_back(d);
        }
    }
    return out;
}

PackedFloat32Array matrixValues(const Mtxf &matrix) {
    PackedFloat32Array values;
    for (const auto &row : matrix) {
        for (float value : row) {
            values.push_back(value);
        }
    }
    return values;
}

// 与 meshDicts 的 material 一一对应：{ textured, color, tex_width, tex_height,
// tex_pixels }。textures 与 mesh.materials 并行。
Array materialDicts(const GBI::Mesh &mesh, const std::vector<GBI::Texture> &textures,
                    bool exact_shade, bool force_projection = false) {
    Array out;
    for (size_t m = 0; m < mesh.materials.size(); m++) {
        Dictionary d;
        const GBI::Material &state = mesh.materials[m];
        const bool has_tex = m < textures.size() && !textures[m].pixels.empty();
        d["textured"] = has_tex;
        d["lit"] = state.lit; // G_LIGHTING
        d["cull_back"] = state.cull_back;
        d["exact_shade"] = exact_shade;
        bool all_projected = !mesh.vertices.empty();
        for (const auto &vertex : mesh.vertices) {
            all_projected = all_projected && vertex.projected;
        }
        d["rsp_projection"] = force_projection || (exact_shade && all_projected);
        d["force_projection"] = force_projection;
        // combine 颜色源（G_CC_SHADE → 顶点色；PRIMITIVE → prim；ENV → env）。
        // 渲染端据此决定底色：SHADE 用 WHITE × 顶点色，PRIMITIVE/ENV 用对应色。
        const GBI::CombineSource color_source =
            GBI::combineColorSource(state.combine_w0, state.combine_w1);
        d["use_vertex"] = GBI::combineUsesShade(state.combine_w0, state.combine_w1);
        d["color_source"] = static_cast<int64_t>(static_cast<uint8_t>(color_source));
        d["color"] = Color(state.prim_color[0] / 255.0f, state.prim_color[1] / 255.0f,
                           state.prim_color[2] / 255.0f);
        d["env_color"] = Color(state.env_color[0] / 255.0f, state.env_color[1] / 255.0f,
                               state.env_color[2] / 255.0f);
        // 材质 alpha（0-255，255 = 不透明）：来自 combine 的 alpha 源——
        // SHADE → 材质所有顶点统一的顶点 alpha（否则 255）；PRIMITIVE/ENV →
        // prim/env 的 alpha；否则 255。渲染端据此开透明度。
        int alpha = 255;
        switch (GBI::combineAlphaSource(state.combine_w0, state.combine_w1)) {
            case GBI::CombineSource::Primitive:
                alpha = state.prim_color[3];
                break;
            case GBI::CombineSource::Env:
                alpha = state.env_color[3];
                break;
            case GBI::CombineSource::Shade: {
                // 顶点 alpha：材质所有顶点一致才用，否则按不透明处理（渐变需要
                // 逐顶点 alpha，StandardMaterial3D 不支持，近似为均匀值）。
                int valpha = -1;
                bool uniform = true;
                for (size_t t = 0; t < mesh.material_ids.size(); t++) {
                    if (mesh.material_ids[t] != m) {
                        continue;
                    }
                    for (int k = 0; k < 3; k++) {
                        const auto &c =
                            mesh.vertices[mesh.indices[t * 3 + k]].color[3];
                        if (valpha < 0) {
                            valpha = c;
                        } else if (valpha != c) {
                            uniform = false;
                        }
                    }
                }
                if (uniform && valpha >= 0) {
                    alpha = valpha;
                }
                break;
            }
            default:
                break;
        }
        d["alpha"] = static_cast<int64_t>(alpha);
        // 灯光（原始 Light_t 方向，供 Fast3D model-view shader 变换）：方向光槽 0..num_lights-1 +
        // 环境光（槽 num_lights）。loaded=false 或 num_lights=0 → 无灯光，
        // 渲染端按不调光处理（shade = 白）。
        d["num_lights"] = static_cast<int64_t>(state.lights.loaded ? state.lights.num_lights : 0);
        if (state.lights.loaded) {
            PackedVector3Array light_dirs;
            PackedColorArray light_cols;
            const int n = std::min<int>(state.lights.num_lights, 8);
            for (int i = 0; i < n; i++) {
                const auto &L = state.lights.light[i];
                light_dirs.push_back(Vector3(L.dir[0] / 127.0f, L.dir[1] / 127.0f,
                                             L.dir[2] / 127.0f));
                light_cols.push_back(Color(L.col[0] / 255.0f, L.col[1] / 255.0f,
                                           L.col[2] / 255.0f));
            }
            d["light_dirs"] = light_dirs;
            d["light_cols"] = light_cols;
            const auto &A = state.lights.light[state.lights.num_lights];
            d["ambient"] = Color(A.col[0] / 255.0f, A.col[1] / 255.0f, A.col[2] / 255.0f);
        }
        // G_SETTILE 的 S/T clamp 模式 → Godot texture_repeat（true=重复/平铺）。
        // angrylion uses clamp = explicit CLAMP || mask == 0.
        const bool clamp_s = state.tex_clamp_s || state.tex_mask_s == 0;
        const bool clamp_t = state.tex_clamp_t || state.tex_mask_t == 0;
        d["repeat_s"] = !clamp_s;
        d["repeat_t"] = !clamp_t;
        d["tile_mask_s"] = static_cast<int64_t>(state.tex_mask_s);
        d["tile_mask_t"] = static_cast<int64_t>(state.tex_mask_t);
        // TextureDecoder has already cropped the decoded image to the tile's
        // (sl..sh, tl..th) region. RDP clamps to texel 0 through the last texel
        // center; the half-texel range matches angrylion's sfrac/tfrac path.
        if (clamp_s && clamp_t && state.tex_width() > 0 &&
            state.tex_height() > 0 && state.tex_sh >= state.tex_sl &&
            state.tex_th >= state.tex_tl) {
            const float width = static_cast<float>(state.tex_width());
            const float height = static_cast<float>(state.tex_height());
            d["uv_clamp"] = Vector4(
                0.5f / width, 0.5f / height,
                (width - 0.5f) / width, (height - 0.5f) / height);
        }
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
    ClassDB::bind_method(D_METHOD("extractLevel", "level_num", "area_index", "act_num"),
                         &GodotBridge::extractLevel, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("getMeshes"), &GodotBridge::getMeshes);
    ClassDB::bind_method(D_METHOD("getMaterials"), &GodotBridge::getMaterials);
    ClassDB::bind_method(D_METHOD("getObjects"), &GodotBridge::getObjects);
    ClassDB::bind_method(D_METHOD("getObjectModels"), &GodotBridge::getObjectModels);
    ClassDB::bind_method(D_METHOD("getInlineObjectModels"),
                         &GodotBridge::getInlineObjectModels);
    ClassDB::bind_method(D_METHOD("getCollisionTriangles"), &GodotBridge::getCollisionTriangles);
    ClassDB::bind_method(D_METHOD("getObjectCollisions"), &GodotBridge::getObjectCollisions);
    ClassDB::bind_method(D_METHOD("getWarnings"), &GodotBridge::getWarnings);
    ClassDB::bind_method(D_METHOD("getLevelName"), &GodotBridge::getLevelName);
    ClassDB::bind_method(D_METHOD("getLevelNameFor", "level_num"),
                         &GodotBridge::getLevelNameFor);
    ClassDB::bind_method(D_METHOD("getAllLevelNames"), &GodotBridge::getAllLevelNames);
    ClassDB::bind_method(D_METHOD("getMarioStartPos"), &GodotBridge::getMarioStartPos);
    ClassDB::bind_method(D_METHOD("getProjectionContext"), &GodotBridge::getProjectionContext);
    ClassDB::bind_method(D_METHOD("getBackground"), &GodotBridge::getBackground);
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

// act_num 0 = 所有 act（忽略 OBJECT_WITH_ACTS 掩码），默认 0。
bool GodotBridge::extractLevel(int level_num, int area_index, int act_num) {
    result_ = {};
    if (!rom.is_loaded) {
        return false;
    }
    LevelExtract::LevelExtractor extractor(rom);
    extractor.setActNum(act_num);
    extractor.run(level_num, area_index);
    result_ = extractor.takeResult();
    return result_.ok;
}

// 每个材质一个网格（与 OBJ 导出的 usemtl 分组一致），方便每个
// MeshInstance3D 单独挂材质。
Array GodotBridge::getMeshes() {
    return meshDicts(result_.mesh, true);
}

Array GodotBridge::getMaterials() {
    return materialDicts(result_.mesh, result_.textures, true);
}

Array GodotBridge::getObjects() {
    Array out;
    for (const auto &obj : result_.objects) {
        Dictionary d;
        const Vec3<float> pos = obj.pos();
        // 渲染位置 = oPos + oGraphYOffset（游戏 obj_update_gfx_pos_and_angle 只给
        // Y 加偏移；碰撞仍是 oPos，见 getObjectCollisions）。
        d["pos"] = Vector3(pos.x, pos.y + obj.f32(ObjectExtract::F::GraphYOffset), pos.z);
        // SM64 角度单位 → 弧度，供 Godot 的旋转直接使用
        const Vec3<int16_t> angle = obj.faceAngle();
        d["angle"] = Vector3(sm64AngleToRadians(angle.x), sm64AngleToRadians(angle.y),
                             sm64AngleToRadians(angle.z));
        d["model"] = static_cast<int64_t>(obj.model_id);
        d["behavior_arg"] = static_cast<int64_t>(obj.behaviorArg());
        d["behavior"] = static_cast<int64_t>((uint32_t(obj.behavior.seg) << 24) |
                                              (obj.behavior.offset & 0xFFFFFF));
        // 出生状态（行为脚本作用于 Object 后的结果）：隐藏/反生成、缩放、
        // 透明度、billboard。
        d["hidden"] = obj.deactivated || obj.invisible;
        d["scale"] = Vector3(obj.scale.x, obj.scale.y, obj.scale.z);
        // oOpacity 默认 0（对象池清零），只在行为显式 SET 后有意义；未设置按 255。
        const int32_t opacity = obj.s32(ObjectExtract::F::Opacity);
        d["opacity"] = static_cast<int64_t>(opacity == 0 ? 255 : opacity);
        d["billboard"] = obj.billboard;
        out.push_back(d);
    }
    return out;
}

// 对象模型按 model id 去重，每个模型一份（网格 + 材质），所有实例共享。
// GEO_BILLBOARD 子树的三角形按 billboard 节点拆成 billboard_parts：每个
// { pivot, meshes, materials } 一部分。游戏把 billboard 渲染成相机空间轴对齐
//（始终面向相机，mtxf_billboard 的 R_z(roll)，roll≈0）；渲染端把部分节点放在
// pivot 上并每帧把其朝向设为相机的逆基（见 Engine.md §4）。
Array GodotBridge::getObjectModels() {
    Array out;
    for (const auto &[model_id, model] : result_.object_models) {
        Dictionary d;
        d["model"] = static_cast<int64_t>(model_id);
        d["meshes"] = meshDicts(model.mesh, false);
        d["materials"] = materialDicts(model.mesh, model.textures, false);
        Array parts;
        for (const auto &part : model.billboard_parts) {
            Dictionary p;
            p["pivot"] = Vector3(part.pivot.x, part.pivot.y, part.pivot.z);
            p["meshes"] = meshDicts(part.mesh, false);
            p["materials"] = materialDicts(part.mesh, part.textures, false, true);
            parts.push_back(p);
        }
        d["billboard_parts"] = parts;
        out.push_back(d);
    }
    return out;
}

Array GodotBridge::getInlineObjectModels() {
    Array out;
    for (size_t i = 0; i < result_.inline_object_models.size(); i++) {
        const auto &model = result_.inline_object_models[i];
        if (model.mesh.indices.empty() && model.billboard_parts.empty()) {
            continue;
        }
        Dictionary d;
        d["object"] = static_cast<int64_t>(i);
        d["billboard"] = i < result_.objects.size() && result_.objects[i].billboard;
        d["meshes"] = meshDicts(model.mesh, true);
        Array materials = materialDicts(model.mesh, model.textures, true);
        for (int m = 0; m < materials.size(); m++) {
            Dictionary material = materials[m];
            material["inline_object"] = true;
            materials[m] = material;
        }
        d["materials"] = materials;
        Array parts;
        for (const auto &part : model.billboard_parts) {
            Dictionary p;
            p["pivot"] = Vector3(part.pivot.x, part.pivot.y, part.pivot.z);
            p["meshes"] = meshDicts(part.mesh, false);
            p["materials"] = materialDicts(part.mesh, part.textures, false, true);
            parts.push_back(p);
        }
        d["billboard_parts"] = parts;
        out.push_back(d);
    }
    return out;
}

// 当前区域的静态碰撞三角形（平坦着色；每三角形附带 SurfaceClass，
// 由渲染端选择颜色）。
Dictionary GodotBridge::getCollisionTriangles() {
    Dictionary d;
    const Collision::TriangleMesh mesh = Collision::buildTriangleMesh(result_.collision);

    PackedVector3Array verts;
    PackedVector3Array normals;
    PackedInt32Array classes;
    PackedInt32Array indices;
    PackedInt32Array rooms;
    verts.resize(static_cast<int>(mesh.positions.size()));
    normals.resize(static_cast<int>(mesh.normals.size()));
    classes.resize(static_cast<int>(mesh.classes.size()));
    for (size_t i = 0; i < mesh.positions.size(); i++) {
        verts.set(i, Vector3(mesh.positions[i].x, mesh.positions[i].y, mesh.positions[i].z));
    }
    for (size_t i = 0; i < mesh.normals.size(); i++) {
        normals.set(i, Vector3(mesh.normals[i].x, mesh.normals[i].y, mesh.normals[i].z));
    }
    for (size_t i = 0; i < mesh.classes.size(); i++) {
        classes.set(i, static_cast<int32_t>(mesh.classes[i]));
    }
    indices.resize(static_cast<int>(mesh.indices.size()));
    for (size_t i = 0; i < mesh.indices.size(); i++) {
        indices.set(i, static_cast<int32_t>(mesh.indices[i]));
    }
    rooms.resize(static_cast<int>(mesh.rooms.size()));
    for (size_t i = 0; i < mesh.rooms.size(); i++) {
        rooms.set(i, static_cast<int32_t>(mesh.rooms[i]));
    }

    d["vertices"] = verts;
    d["normals"] = normals;
    d["classes"] = classes;
    d["indices"] = indices;
    d["rooms"] = rooms;
    return d;
}

// 各对象的碰撞三角形（行为 LOAD_COLLISION_DATA）。顶点为对象本地空间，
// 与对象模型共用同一个 Godot 节点变换（pos/angle，见 getObjects）。
Array GodotBridge::getObjectCollisions() {
    Array out;
    for (size_t i = 0; i < result_.object_collisions.size() && i < result_.objects.size(); i++) {
        const Collision::Data &oc = result_.object_collisions[i];
        if (!oc.ok || oc.surfaces.empty()) {
            continue;
        }
        const Collision::TriangleMesh mesh = Collision::buildTriangleMesh(oc);
        if (mesh.indices.empty()) {
            continue;
        }
        const auto &obj = result_.objects[i];
        if (obj.deactivated) {
            continue; // DEACTIVATE 反生成的对象没有碰撞
        }

        Dictionary d;
        const Vec3<float> pos = obj.pos();
        d["pos"] = Vector3(pos.x, pos.y, pos.z);
        const Vec3<int16_t> angle = obj.faceAngle();
        d["angle"] = Vector3(sm64AngleToRadians(angle.x), sm64AngleToRadians(angle.y),
                             sm64AngleToRadians(angle.z));

        PackedVector3Array verts;
        PackedVector3Array normals;
        PackedInt32Array classes;
        PackedInt32Array indices;
        verts.resize(static_cast<int>(mesh.positions.size()));
        normals.resize(static_cast<int>(mesh.normals.size()));
        classes.resize(static_cast<int>(mesh.classes.size()));
        for (size_t k = 0; k < mesh.positions.size(); k++) {
            verts.set(k, Vector3(mesh.positions[k].x, mesh.positions[k].y, mesh.positions[k].z));
        }
        for (size_t k = 0; k < mesh.normals.size(); k++) {
            normals.set(k, Vector3(mesh.normals[k].x, mesh.normals[k].y, mesh.normals[k].z));
        }
        for (size_t k = 0; k < mesh.classes.size(); k++) {
            classes.set(k, static_cast<int32_t>(mesh.classes[k]));
        }
        indices.resize(static_cast<int>(mesh.indices.size()));
        for (size_t k = 0; k < mesh.indices.size(); k++) {
            indices.set(k, static_cast<int32_t>(mesh.indices[k]));
        }

        d["vertices"] = verts;
        d["normals"] = normals;
        d["classes"] = classes;
        d["indices"] = indices;
        out.push_back(d);
    }
    return out;
}

String GodotBridge::getLevelName() {
    return String(result_.level_name.c_str());
}

// 本次提取的警告/被守卫的异常：{ stage, message } 数组（供 UI 弹窗展示）。
Array GodotBridge::getWarnings() {
    Array out;
    for (const auto &w : result_.warnings) {
        Dictionary d;
        d["stage"] = String(w.stage.c_str());
        d["message"] = String(w.message.c_str());
        out.push_back(d);
    }
    return out;
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

Dictionary GodotBridge::getProjectionContext() {
    Dictionary d;
    if (!result_.projection_context) {
        return d;
    }
    const GBI::ProjectionContext &context = *result_.projection_context;
    d["perspective"] = context.perspective;
    d["fov"] = context.perspective_fov;
    d["near"] = context.perspective_near;
    d["far"] = context.perspective_far;
    d["persp_norm"] = context.persp_norm;
    d["view"] = matrixValues(context.view_matrix);
    d["projection"] = matrixValues(context.projection_matrix);
    return d;
}

Dictionary GodotBridge::getBackground() {
    Dictionary d;
    d["background"] = static_cast<int64_t>(result_.background.background);
    d["func"] = static_cast<int64_t>(result_.background.func);
    d["is_skybox"] = result_.background.is_skybox();
    // func 为空时 background 是 RGBA5551 填充色（geo_process_background 的
    // gDPSetFillColor）。
    const uint16_t v = static_cast<uint16_t>(result_.background.background);
    d["fill_color"] = Color(((v >> 11) & 0x1F) / 31.0f, ((v >> 6) & 0x1F) / 31.0f,
                            ((v >> 1) & 0x1F) / 31.0f, (v & 1) ? 1.0f : 0.0f);
    // 天空盒贴图集（10×8 图块，320×256 RGBA8；纯色背景为空）。
    if (!result_.skybox.pixels.empty()) {
        PackedByteArray px;
        px.resize(static_cast<int>(result_.skybox.pixels.size()));
        memcpy(px.ptrw(), result_.skybox.pixels.data(), result_.skybox.pixels.size());
        d["skybox_width"] = static_cast<int64_t>(result_.skybox.width);
        d["skybox_height"] = static_cast<int64_t>(result_.skybox.height);
        d["skybox_pixels"] = px;
    }
    return d;
}
