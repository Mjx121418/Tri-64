#include "Level/Object.h"

#include "Level/graph_node.h"
#include "Math/math.h"
#include <algorithm>
#include <optional>
#include <variant>

namespace ObjectExtract {

namespace {

// ---- Frame0Animator：动画 frame-0 的逐 AP 值（镜像 decomp） ----
// struct Animation（types.h）：flags(s16) divisor(s16) startFrame loopStart
// loopEnd boneCount(s16) values(u32) index(u32) length(u32)，共 0x18 字节。
// 属性游标按 retrieve_animation_index 推进：每个属性是 animindex 里的一对
// (start, count)，读后游标 +4 字节。

constexpr uint32_t kTranslateXYZ = 0;
constexpr uint32_t kTranslateY = 1;
constexpr uint32_t kTranslateXZ = 2;
constexpr uint32_t kTranslateNone = 3;

// 从段里读 BE16/BE32；越界返回 0（不抛异常，见 Memory/segment.cpp 的 subspan 行为）。
int16_t readS16At(const SegmentTable &seg, SegmentedAddress addr, uint32_t offset) {
    const std::span<uint8_t> seg_span = seg.data(SegmentedAddress { addr.seg, 0 });
    if (addr.offset + offset + 2 > seg_span.size()) {
        return 0;
    }
    return readInt<int16_t>(seg_span, addr.offset + offset);
}

uint32_t readU32At(const SegmentTable &seg, SegmentedAddress addr, uint32_t offset) {
    const std::span<uint8_t> seg_span = seg.data(SegmentedAddress { addr.seg, 0 });
    if (addr.offset + offset + 4 > seg_span.size()) {
        return 0;
    }
    return readInt<uint32_t>(seg_span, addr.offset + offset);
}

} // namespace

ObjectExtract::Frame0Animator::Frame0Animator(const SegmentTable &seg_table, SegmentedAddress animations,
                                              int16_t animate_index)
    : seg_table_(seg_table) {
    if (animate_index < 0 || animations.isNull()) {
        return;
    }
    // animations 是 Animation* 数组（段地址）；animations[animate_index] = Animation*。
    const uint32_t anim_ptr = readU32At(seg_table_, animations, static_cast<uint32_t>(animate_index) * 4);
    if (anim_ptr == 0) {
        return;
    }
    const SegmentedAddress anim = segAddress(anim_ptr);

    const int16_t flags = readS16At(seg_table_, anim, 0x00);
    const int16_t divisor = readS16At(seg_table_, anim, 0x02);
    index_addr_ = segAddress(readU32At(seg_table_, anim, 0x10));
    values_addr_ = segAddress(readU32At(seg_table_, anim, 0x0C));
    if (index_addr_.isNull() || values_addr_.isNull()) {
        return;
    }

    // geo_set_animation_globals：flags → 平移类型
    constexpr int16_t kAnimFlagHorTrans = 0x1;  // 只有 Y
    constexpr int16_t kAnimFlagVertTrans = 0x2; // 只有 X/Z
    constexpr int16_t kAnimFlag6 = 0x20;        // 无平移
    if (flags & kAnimFlagHorTrans) {
        translate_type_ = kTranslateY;
    } else if (flags & kAnimFlagVertTrans) {
        translate_type_ = kTranslateXZ;
    } else if (flags & kAnimFlag6) {
        translate_type_ = kTranslateNone;
    } else {
        translate_type_ = kTranslateXYZ;
    }

    // gCurrAnimTranslationMultiplier = animYTrans / divisor；静态导出 animYTrans = 0，
    // divisor 为 0 时为 1.0，否则为 0。
    multiplier_ = (divisor == 0) ? 1.0f : 0.0f;
    attr_offset_ = 0;
    rotation_mode_ = false;
    ok_ = true;
}

float ObjectExtract::Frame0Animator::readValue() {
    // retrieve_animation_index(0, &attr)：0 < start ? count + 0 : count + start - 1
    const uint16_t start = static_cast<uint16_t>(readS16At(seg_table_, index_addr_, attr_offset_));
    const uint16_t count = static_cast<uint16_t>(readS16At(seg_table_, index_addr_, attr_offset_ + 2));
    attr_offset_ += 4;
    const int32_t idx = (0 < static_cast<int32_t>(start))
                            ? static_cast<int32_t>(count)
                            : static_cast<int32_t>(count) + static_cast<int32_t>(start) - 1;
    return static_cast<float>(readS16At(seg_table_, values_addr_, static_cast<uint32_t>(idx) * 2));
}

void ObjectExtract::Frame0Animator::skipAttribute() {
    attr_offset_ += 4;
}

std::optional<Frame0Part> ObjectExtract::Frame0Animator::next() {
    if (!ok_) {
        return std::nullopt;
    }
    Frame0Part p;
    // 第一个 AP 按 flags 读平移；之后 gCurrAnimType 切到 ROTATION，只读旋转。
    if (!rotation_mode_) {
        switch (translate_type_) {
            case kTranslateXYZ:
                p.translation.x = readValue() * multiplier_;
                p.translation.y = readValue() * multiplier_;
                p.translation.z = readValue() * multiplier_;
                break;
            case kTranslateY:
                skipAttribute();
                p.translation.y = readValue() * multiplier_;
                skipAttribute();
                break;
            case kTranslateXZ:
                p.translation.x = readValue() * multiplier_;
                skipAttribute();
                p.translation.z = readValue() * multiplier_;
                break;
            case kTranslateNone:
                skipAttribute();
                skipAttribute();
                skipAttribute();
                break;
        }
        rotation_mode_ = true;
    }
    p.rotation.x = static_cast<int16_t>(readValue());
    p.rotation.y = static_cast<int16_t>(readValue());
    p.rotation.z = static_cast<int16_t>(readValue());
    return p;
}

// 把图节点变换（缩放/旋转/平移）烘焙进网格顶点与法线。
void applyTransform(GBI::Mesh &mesh, const Mtxf &m) {
    for (auto &v : mesh.vertices) {
        const Vec3<float> p = transformPoint(m, {v.position[0], v.position[1], v.position[2]});
        v.position[0] = p.x;
        v.position[1] = p.y;
        v.position[2] = p.z;
        const Vec3<float> n = transformNormal(m, {v.normal[0], v.normal[1], v.normal[2]});
        v.normal[0] = n.x;
        v.normal[1] = n.y;
        v.normal[2] = n.z;
    }
}

struct DisplayListWithTransform {
    SegmentedAddress dl;
    Mtxf transform;
};

// 遍历对象模型的 geo 图节点，累积缩放/旋转/平移，收集 (DL, 变换) 对。
// 相机朝向类节点（billboard / animated part）按静态导出一律只烘焙平移，
// 不做朝向相机的处理（未来支持 billboard 渲染时再扩展）。frame0 非空时，
// 每个 animated part 再叠加动画 frame-0 的平移/旋转增量（按 geo 遍历顺序
// 逐个调用 frame0->next()，与游戏的 gCurrAnimAttribute 游标一致）。
void collectDisplayListsWithTransform(const GraphNode &node, const Mtxf &parent,
                                      std::vector<DisplayListWithTransform> &out,
                                      Frame0Animator *frame0) {
    Mtxf current = parent;
    std::optional<SegmentedAddress> node_dl;

    std::visit([&](const auto &d) {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, GraphNodeDisplayList>) {
            node_dl = d.display_list;
        } else if constexpr (std::is_same_v<T, GraphNodeScale>) {
            // 缩放节点：游戏用 mtxf_scale_vec3f（= diag(s) × mtx，保留平移行），
            // 即预乘。在 decomp 的矩阵约定下 (A·B)·v = B·(A·v)。
            current = mtxfMul(mtxfScale(d.scale), current);
            node_dl = d.display_list;
        } else if constexpr (std::is_same_v<T, GraphNodeTranslation>) {
            // 平移节点：游戏 mtxf_mul(new, T, current)，预乘（父空间平移）
            current = mtxfMul(
                mtxfTranslation(d.translation.x, d.translation.y, d.translation.z), current);
            node_dl = d.display_list;
        } else if constexpr (std::is_same_v<T, GraphNodeRotation>) {
            current = mtxfMul(mtxfRotationZXY(d.rotation), current);
            node_dl = d.display_list;
        } else if constexpr (std::is_same_v<T, GraphNodeTranslationRotation>) {
            // mtxf_rotate_zxy_and_translate = 先旋转后平移（T × R），再预乘
            const Mtxf tr = mtxfMul(
                mtxfTranslation(d.translation.x, d.translation.y, d.translation.z),
                mtxfRotationZXY(d.rotation));
            current = mtxfMul(tr, current);
            node_dl = d.display_list;
        } else if constexpr (std::is_same_v<T, GraphNodeBillboard>) {
            current = mtxfMul(
                mtxfTranslation(d.translation.x, d.translation.y, d.translation.z), current);
            node_dl = d.display_list;
        } else if constexpr (std::is_same_v<T, GraphNodeAnimatedPart>) {
            // animated part：geo 平移 + 动画 frame-0 增量（mtxf_rotate_xyz_and_translate
            // = 先旋转后平移，再预乘，与 GraphNodeTranslationRotation 同构）。
            Vec3<float> t { static_cast<float>(d.translation.x),
                            static_cast<float>(d.translation.y),
                            static_cast<float>(d.translation.z) };
            Vec3<int16_t> r { 0, 0, 0 };
            if (frame0) {
                if (auto p = frame0->next()) {
                    t.x += p->translation.x;
                    t.y += p->translation.y;
                    t.z += p->translation.z;
                    r = p->rotation;
                }
            }
            const Mtxf tr = mtxfMul(mtxfTranslation(t.x, t.y, t.z), mtxfRotationZXY(r));
            current = mtxfMul(tr, current);
            node_dl = d.display_list;
        }
    }, node.data);

    if (node_dl) {
        out.push_back(DisplayListWithTransform {*node_dl, current});
    }

    // 开关节点（GEO_SWITCH_CASE）：只取选中的 case。静态导出没有动画，
    // 默认取 case 0（避免把所有动画帧叠加）。
    if (std::holds_alternative<GraphNodeSwitchCase>(node.data)) {
        const auto &sw = std::get<GraphNodeSwitchCase>(node.data);
        if (!node.children.empty()) {
            const int16_t idx = sw.selected_case >= 0 ? sw.selected_case : 0;
            collectDisplayListsWithTransform(
                *node.children[std::min<size_t>(idx, node.children.size() - 1)], current, out, frame0);
        }
        return;
    }

    // LOD 节点（GEO_RENDER_RANGE）：取包含相机距离 0 的档位（近景）。
    if (std::holds_alternative<GraphNodeLevelOfDetail>(node.data)) {
        const auto &lod = std::get<GraphNodeLevelOfDetail>(node.data);
        if (lod.min_distance <= 0 && 0 < lod.max_distance) {
            for (const auto &child : node.children) {
                collectDisplayListsWithTransform(*child, current, out, frame0);
            }
        }
        return;
    }

    for (const auto &child : node.children) {
        collectDisplayListsWithTransform(*child, current, out, frame0);
    }
}

void mergeMesh(GBI::Mesh &merged, GBI::Mesh &&src) {
    const uint32_t base = static_cast<uint32_t>(merged.vertices.size());
    merged.vertices.insert(merged.vertices.end(), src.vertices.begin(), src.vertices.end());
    merged.indices.reserve(merged.indices.size() + src.indices.size());
    for (uint32_t idx : src.indices) {
        merged.indices.push_back(base + idx);
    }

    const size_t tri_count = src.indices.size() / 3;
    merged.material_ids.reserve(merged.material_ids.size() + tri_count);
    for (size_t t = 0; t < tri_count; t++) {
        const uint32_t mi = src.material_ids[t];
        const GBI::Material &m = src.materials[mi];
        const uint32_t img = src.material_images[mi];
        uint32_t mid = 0;
        for (; mid < merged.materials.size(); mid++) {
            if (merged.materials[mid] == m && merged.material_images[mid] == img) {
                break;
            }
        }
        if (mid == merged.materials.size()) {
            merged.materials.push_back(m);
            merged.material_images.push_back(img);
        }
        merged.material_ids.push_back(mid);
    }
}

ObjectExtract::ObjectModel decodeModel(const SegmentTable &seg_table, const GraphNode *node,
                        ObjectExtract::Frame0Animator *frame0) {
    ObjectExtract::ObjectModel model;
    if (!node) {
        return model;
    }

    std::vector<ObjectExtract::DisplayListWithTransform> dls;
    collectDisplayListsWithTransform(*node, mtxfIdentity(), dls, frame0);
    if (dls.empty()) {
        return model;
    }

    GBI::Mesh merged;
    for (const auto &dlt : dls) {
        // Treasure World 等 hack 的对象模型 geo 里可能有空/无效 DL 地址
        // （0x00000000 / 0xFFFFFFFF），跳过而不是去解码（见 docs/engine-notes.md）。
        if (dlt.dl.seg < 0 || dlt.dl.seg > 31 || (dlt.dl.seg == 0 && dlt.dl.offset == 0)) {
            continue;
        }
        GBI::DLInterpreter interp(seg_table);
        GBI::Mesh &decoded = interp.run(dlt.dl);
        ObjectExtract::applyTransform(decoded, dlt.transform);
        mergeMesh(merged, std::move(decoded));
    }
    if (merged.indices.empty()) {
        return model;
    }

    model.mesh = std::move(merged);
    model.textures.resize(model.mesh.materials.size());
    for (size_t m = 0; m < model.mesh.materials.size(); m++) {
        if (model.mesh.materials[m].textured && model.mesh.material_images[m] != 0) {
            auto tex = GBI::decodeTexture(model.mesh.materials[m],
                                          segAddress(model.mesh.material_images[m]), seg_table);
            if (tex) {
                model.textures[m] = std::move(*tex);
            }
        }
    }
    return model;
}

void expandMacroObjects(const std::vector<MacroObjectSpawnInfo> &macro_objects, int8_t area_index,
                        const std::vector<PresetTables::MacroPreset> &presets,
                        std::vector<ObjectSpawnInfo> &out) {
    for (const auto &m : macro_objects) {
        if (m.preset < 0 || static_cast<size_t>(m.preset) >= presets.size()) {
            continue;
        }
        const PresetTables::MacroPreset &p = presets[m.preset];
        if (p.model == 0) {
            continue; // MODEL_NONE / 生成器（如 goomba 生成器、coin 编队）
        }
        ObjectSpawnInfo info;
        info.model_id = p.model;
        info.start_pos = m.pos;
        info.start_angle = {0, m.yaw, 0};
        info.area_index = area_index;
        info.active_area_index = area_index;
        // 行为参数：镜像 spawn_macro_objects —— preset 默认参数非 0 时替换条目
        // 参数的低字节。
        uint32_t param = static_cast<uint32_t>(static_cast<uint16_t>(m.bhv_param));
        if (p.param != 0) {
            param = (param & 0xFF00) + (static_cast<uint16_t>(p.param) & 0x00FF);
        }
        info.behavior_arg = param;
        info.behavior_script = p.behavior;
        out.push_back(info);
    }
}

void expandSpecialObjects(const std::vector<Collision::SpecialObject> &special_objects,
                          int8_t area_index, const std::vector<PresetTables::SpecialPreset> &presets,
                          std::vector<ObjectSpawnInfo> &out) {
    for (const auto &s : special_objects) {
        if (static_cast<size_t>(s.preset) >= presets.size()) {
            continue;
        }
        const PresetTables::SpecialPreset &p = presets[s.preset];
        if (p.model == 0) {
            continue; // MODEL_NONE / 生成器
        }
        ObjectSpawnInfo info;
        info.model_id = p.model;
        info.start_pos = {s.x, s.y, s.z};
        // 出生数据里的 yaw 是 256 一全圈，需转成 SM64 角度单位（同游戏 convert_rotation）。
        info.start_angle = {0, convertRotation(s.yaw), 0};
        info.area_index = area_index;
        info.active_area_index = area_index;
        // 行为参数：镜像 spawn_special_objects 的 SPTYPE 处理。
        switch (p.type) {
            case 4: // SPTYPE_DEF_PARAM_AND_YROT：用 preset 的默认参数
                info.behavior_arg = static_cast<uint32_t>(static_cast<uint16_t>(p.def_param) & 0xFF);
                break;
            case 2: // SPTYPE_PARAMS_AND_YROT：条目的额外参数
            case 3:
                info.behavior_arg = static_cast<uint32_t>(static_cast<uint16_t>(s.param));
                break;
            default: // 0 / 1：无参数
                info.behavior_arg = 0;
                break;
        }
        info.behavior_script = p.behavior;
        out.push_back(info);
    }
}

} // namespace ObjectExtract
