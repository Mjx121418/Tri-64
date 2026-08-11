#include "Level/Object.h"

#include "Level/graph_node.h"
#include "Math/math.h"
#include <algorithm>
#include <optional>
#include <utility>
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

// ---- Object：统一对象（镜像 decomp struct Object）----

SegmentedAddress Object::addr(uint8_t f) const {
    SegmentedAddress a;
    a.setAddress(raw[f]);
    return a;
}

void Object::setAddr(uint8_t f, const SegmentedAddress &a) {
    raw[f] = (static_cast<uint32_t>(a.seg) << 24) | (a.offset & 0x00FFFFFF);
}

Vec3<float> Object::pos() const {
    return {f32(F::PosX), f32(F::PosY), f32(F::PosZ)};
}

void Object::setPos(const Vec3<float> &p) {
    f32(F::PosX) = p.x;
    f32(F::PosY) = p.y;
    f32(F::PosZ) = p.z;
}

Vec3<int16_t> Object::faceAngle() const {
    return {static_cast<int16_t>(s32(F::FaceAnglePitch)),
            static_cast<int16_t>(s32(F::FaceAngleYaw)),
            static_cast<int16_t>(s32(F::FaceAngleRoll))};
}

Vec3<int16_t> Object::moveAngle() const {
    return {static_cast<int16_t>(s32(F::MoveAnglePitch)),
            static_cast<int16_t>(s32(F::MoveAngleYaw)),
            static_cast<int16_t>(s32(F::MoveAngleRoll))};
}

uint32_t Object::behaviorArg() const {
    return static_cast<uint32_t>(s32(F::BhvParams));
}

// OBJECT 命令对象（镜像 game/object_list_processor.c 的 spawn_object_to_pool）：
// 出生位置/角度、行为参数（oBhvParams + 2ndByte）直接来自 ObjectSpawnInfo。
Object Object::fromSpawnInfo(const ObjectSpawnInfo &info) {
    Object o;
    o.model_id = info.model_id;
    o.behavior = info.behavior_script;
    o.area_index = info.area_index;
    o.active_area_index = info.active_area_index;
    o.setPos({static_cast<float>(info.start_pos.x), static_cast<float>(info.start_pos.y),
              static_cast<float>(info.start_pos.z)});
    o.s32(F::FaceAnglePitch) = info.start_angle.x;
    o.s32(F::FaceAngleYaw) = info.start_angle.y;
    o.s32(F::FaceAngleRoll) = info.start_angle.z;
    o.s32(F::MoveAnglePitch) = info.start_angle.x;
    o.s32(F::MoveAngleYaw) = info.start_angle.y;
    o.s32(F::MoveAngleRoll) = info.start_angle.z;
    o.s32(F::BhvParams) = static_cast<int32_t>(info.behavior_arg);
    o.s32(F::BhvParams2ndByte) = static_cast<int32_t>((info.behavior_arg >> 16) & 0xFF);
    return o;
}

// 宏对象（镜像 spawn_macro_objects + spawn_macro_abs_yrot_2params 的参数规则）。
// 条目 yaw 已是 SM64 角度单位（level_script 解码时 ×0x200）。
Object Object::fromMacroObject(const MacroObjectSpawnInfo &entry,
                               const PresetTables::MacroPreset &preset) {
    Object o;
    o.model_id = preset.model;
    o.behavior = preset.behavior;
    o.setPos({static_cast<float>(entry.pos.x), static_cast<float>(entry.pos.y),
              static_cast<float>(entry.pos.z)});
    o.s32(F::FaceAngleYaw) = entry.yaw;
    o.s32(F::MoveAngleYaw) = entry.yaw;
    // 行为参数：preset 默认参数非 0 时替换条目参数的低字节。
    uint32_t param = static_cast<uint32_t>(static_cast<uint16_t>(entry.bhv_param));
    if (preset.param != 0) {
        param = (param & 0xFF00) + (static_cast<uint16_t>(preset.param) & 0x00FF);
    }
    // 镜像 spawn_macro_objects：oBhvParams = (param&0xFF)<<16 | (param&0xFF00)。
    o.s32(F::BhvParams) = static_cast<int32_t>(((param & 0xFF) << 16) + (param & 0xFF00));
    o.s32(F::BhvParams2ndByte) = static_cast<int32_t>(param & 0xFF);
    return o;
}

// 碰撞特殊对象（镜像 spawn_special_objects 的 SPTYPE 处理）。yaw 是 256 一全圈，
// 用 convert_rotation 转成 SM64 角度单位。
Object Object::fromSpecialObject(const Collision::SpecialObject &entry,
                                 const PresetTables::SpecialPreset &preset) {
    Object o;
    o.model_id = preset.model;
    o.behavior = preset.behavior;
    o.setPos({static_cast<float>(entry.x), static_cast<float>(entry.y),
              static_cast<float>(entry.z)});
    const int16_t yaw = convertRotation(entry.yaw);
    o.s32(F::FaceAngleYaw) = yaw;
    o.s32(F::MoveAngleYaw) = yaw;
    switch (preset.type) {
        case 4: // SPTYPE_DEF_PARAM_AND_YROT：oBhvParams = defParam << 24
            o.s32(F::BhvParams) = static_cast<int32_t>(static_cast<uint16_t>(preset.def_param) & 0xFF) << 24;
            break;
        case 2: // SPTYPE_PARAMS_AND_YROT：oBhvParams = entry.param << 16
            o.s32(F::BhvParams) = static_cast<int32_t>(static_cast<uint16_t>(entry.param)) << 16;
            break;
        default: // 0 / 1 / 3（SPTYPE_UNKNOWN 的 3 个额外 s16 转浮点，暂不导出）
            break;
    }
    return o;
}

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
    const int16_t start_frame = readS16At(seg_table_, anim, 0x04);
    const int16_t loop_start = readS16At(seg_table_, anim, 0x06);
    const int16_t loop_end = readS16At(seg_table_, anim, 0x08);
    index_addr_ = segAddress(readU32At(seg_table_, anim, 0x10));
    values_addr_ = segAddress(readU32At(seg_table_, anim, 0x0C));
    if (index_addr_.isNull() || values_addr_.isNull()) {
        return;
    }

    // 游戏出生帧（镜像 graph_node.c 的 geo_obj_init_animation +
    // geo_update_animation_frame 首次渲染）：geo_obj_init_animation 把 animFrame
    // 置为 startFrame + (BACKWARD ? 1 : -1)；首次 geo_update_animation_frame 在
    // ANIM_FLAG_2（0x04）时不推进，否则向前推 1（向后减 1），越界时回绕。
    // retrieve_animation_index 用这个帧取 animvalue。
    constexpr int16_t kAnimFlagNOLoop = 0x01;
    constexpr int16_t kAnimFlagBackward = 0x02;
    constexpr int16_t kAnimFlag2 = 0x04;
    int16_t first_frame = start_frame;
    if (flags & kAnimFlagBackward) {
        if (flags & kAnimFlag2) {
            first_frame = start_frame + 1; // 不推进，停在 startFrame+1
        } else if (first_frame < loop_start) {
            first_frame = (flags & kAnimFlagNOLoop) ? loop_start : loop_end - 1;
        }
    } else {
        if (flags & kAnimFlag2) {
            first_frame = start_frame - 1; // 不推进，停在 startFrame-1
        } else if (first_frame >= loop_end) {
            first_frame = (flags & kAnimFlagNOLoop) ? loop_end - 1 : loop_start;
        }
    }
    frame_ = first_frame;

    // geo_set_animation_globals：flags → 平移类型（include/types.h 的 ANIM_FLAG_*）。
    // HOR_TRANS(0x08) → 只有 Y；VERT_TRANS(0x10) → 只有 X/Z；ANIM_FLAG_6(0x40) → 无。
    constexpr int16_t kAnimFlagHorTrans = 0x08;  // 只有 Y
    constexpr int16_t kAnimFlagVertTrans = 0x10; // 只有 X/Z
    constexpr int16_t kAnimFlag6 = 0x40;         // 无平移
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
    // retrieve_animation_index(frame_, &attr)：
    // frame_ < limit ? start + frame_ : start + limit - 1
    const int16_t limit = readS16At(seg_table_, index_addr_, attr_offset_);
    const int16_t start = readS16At(seg_table_, index_addr_, attr_offset_ + 2);
    attr_offset_ += 4;
    const int32_t idx = (frame_ < limit) ? start + frame_ : start + limit - 1;
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

// 遍历对象模型的 geo 图节点，累积缩放/旋转/平移，收集 (DL, 变换) 对。
// billboard 子树按 mtxf_billboard 语义只取父矩阵的平移（T(pivot)，旋转/缩放
// 丢弃）——位置跟随父链，朝向由渲染端每帧面向相机补足。frame0 非空时，
// 每个 animated part 再叠加动画 frame-0 的平移/旋转增量（按 geo 遍历顺序
// 逐个调用 frame0->next()，与游戏的 gCurrAnimAttribute 游标一致）。
void collectDisplayListsWithTransform(const GraphNode &node, const Mtxf &parent,
                                      const Fast3D::FixedMatrix &fixed_parent,
                                      std::vector<DisplayListWithTransform> &out,
                                      Frame0Animator *frame0,
                                      std::optional<Vec3<float>> billboard_pivot = std::nullopt) {
    Mtxf current = parent;
    Fast3D::FixedMatrix fixed_current = fixed_parent;
    std::optional<SegmentedAddress> node_dl;

    std::visit([&](const auto &d) {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, GraphNodeDisplayList>) {
            node_dl = d.display_list;
        } else if constexpr (std::is_same_v<T, GraphNodeScale>) {
            // 缩放节点：游戏用 mtxf_scale_vec3f（= diag(s) × mtx，保留平移行），
            // 即预乘。在 decomp 的矩阵约定下 (A·B)·v = B·(A·v)。
            current = mtxfMul(mtxfScale(d.scale), current);
            fixed_current = Fast3D::matrixMultiply(
                Fast3D::fromFloatMatrix(mtxfScale(d.scale)), fixed_current);
            node_dl = d.display_list;
        } else if constexpr (std::is_same_v<T, GraphNodeTranslation>) {
            // 平移节点：游戏 mtxf_mul(new, T, current)，预乘（父空间平移）
            current = mtxfMul(
                mtxfTranslation(d.translation.x, d.translation.y, d.translation.z), current);
            fixed_current = Fast3D::matrixMultiply(
                Fast3D::fromFloatMatrix(mtxfTranslation(d.translation.x, d.translation.y,
                                                         d.translation.z)),
                fixed_current);
            node_dl = d.display_list;
        } else if constexpr (std::is_same_v<T, GraphNodeRotation>) {
            current = mtxfMul(mtxfRotationZXY(d.rotation), current);
            fixed_current = Fast3D::matrixMultiply(
                Fast3D::fromFloatMatrix(mtxfRotationZXY(d.rotation)), fixed_current);
            node_dl = d.display_list;
        } else if constexpr (std::is_same_v<T, GraphNodeTranslationRotation>) {
            // mtxf_rotate_zxy_and_translate：旋转后平移。行向量约定下等价于
            // R × T（平移不被自身旋转带偏），再预乘父矩阵。
            const Mtxf tr = mtxfMul(
                mtxfRotationZXY(d.rotation),
                mtxfTranslation(d.translation.x, d.translation.y, d.translation.z));
            current = mtxfMul(tr, current);
            fixed_current = Fast3D::matrixMultiply(Fast3D::fromFloatMatrix(tr), fixed_current);
            node_dl = d.display_list;
        } else if constexpr (std::is_same_v<T, GraphNodeBillboard>) {
            // 镜像 geo_process_billboard 的 mtxf_billboard（roll=0）：只保留父
            // 矩阵的平移（父旋转/缩放丢弃），旋转为恒等。pivot = 父链（含
            // 旋转/缩放）应用到节点平移；该节点的 DL 与整个子树都归入这个
            // pivot（渲染端在此锚点上每帧面向相机）。
            const Vec3<float> p = transformPoint(
                current, {static_cast<float>(d.translation.x), static_cast<float>(d.translation.y),
                          static_cast<float>(d.translation.z)});
            current = mtxfTranslation(p.x, p.y, p.z);
            fixed_current = Fast3D::fromFloatMatrix(current);
            billboard_pivot = p;
            node_dl = d.display_list;
        } else if constexpr (std::is_same_v<T, GraphNodeAnimatedPart>) {
            // animated part：geo 平移 + 动画 frame-0 增量。旋转用与游戏
            // geo_process_animated_part 相同的 mtxf_rotate_xyz_and_translate
            //（XYZ 顺序）。平移不参与自身旋转：行向量约定 R × T（等价于 decomp
            // 直接写入的"旋转 + 未旋转平移"矩阵），再预乘父矩阵。
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
            const Mtxf tr = mtxfMul(mtxfRotationXYZ(r), mtxfTranslation(t.x, t.y, t.z));
            current = mtxfMul(tr, current);
            fixed_current = Fast3D::matrixMultiply(Fast3D::fromFloatMatrix(tr), fixed_current);
            node_dl = d.display_list;
        }
    }, node.data);

    if (node_dl) {
        DisplayListWithTransform dlt {
            *node_dl, current, fixed_current, false, {}, 0};
        if (billboard_pivot) {
            dlt.is_billboard = true;
            dlt.billboard_pivot = *billboard_pivot;
        }
        // 与游戏的 geo_append_display_list(node->displayList, node->flags >> 8)
        // 一致：本节点的 flags 高 8 位就是绘制层。
        dlt.layer = static_cast<uint8_t>(node.flags >> 8);
        out.push_back(std::move(dlt));
    }

    // 开关节点（GEO_SWITCH_CASE）：只取选中的 case。静态导出没有动画，
    // 默认取 case 0（避免把所有动画帧叠加）。
    if (std::holds_alternative<GraphNodeSwitchCase>(node.data)) {
        const auto &sw = std::get<GraphNodeSwitchCase>(node.data);
        if (!node.children.empty()) {
            const int16_t idx = sw.selected_case >= 0 ? sw.selected_case : 0;
            collectDisplayListsWithTransform(
                *node.children[std::min<size_t>(idx, node.children.size() - 1)], current,
                fixed_current, out, frame0, billboard_pivot);
        }
        return;
    }

    // LOD 节点（GEO_RENDER_RANGE）：取包含相机距离 0 的档位（近景）。
    if (std::holds_alternative<GraphNodeLevelOfDetail>(node.data)) {
        const auto &lod = std::get<GraphNodeLevelOfDetail>(node.data);
        if (lod.min_distance <= 0 && 0 < lod.max_distance) {
            for (const auto &child : node.children) {
                collectDisplayListsWithTransform(
                    *child, current, fixed_current, out, frame0, billboard_pivot);
            }
        }
        return;
    }

    for (const auto &child : node.children) {
        collectDisplayListsWithTransform(*child, current, fixed_current, out, frame0,
                                         billboard_pivot);
    }
}

std::vector<DisplayListWithTransform> collectDisplayLists(const GraphNode &root,
                                                          Frame0Animator *frame0) {
    std::vector<DisplayListWithTransform> out;
    collectDisplayListsWithTransform(
        root, mtxfIdentity(), Fast3D::identityMatrix(), out, frame0);
    return out;
}

void ObjectModelDecoder::mergeMesh(GBI::Mesh &merged, GBI::Mesh &&src) {
    const uint32_t base = static_cast<uint32_t>(merged.vertices.size());
    merged.vertices.insert(merged.vertices.end(), src.vertices.begin(), src.vertices.end());
    merged.indices.reserve(merged.indices.size() + src.indices.size());
    for (uint32_t idx : src.indices) {
        merged.indices.push_back(base + idx);
    }

    const size_t tri_count = src.indices.size() / 3;
    merged.material_ids.reserve(merged.material_ids.size() + tri_count);
    merged.triangle_layers.reserve(merged.triangle_layers.size() + tri_count);
    for (size_t t = 0; t < tri_count; t++) {
        const uint32_t mi = src.material_ids[t];
        const GBI::Material &m = src.materials[mi];
        const uint32_t img = src.material_images[mi];
        const uint32_t tlut = src.material_tlut[mi];
        uint32_t mid = 0;
        for (; mid < merged.materials.size(); mid++) {
            if (merged.materials[mid] == m && merged.material_images[mid] == img
                && merged.material_tlut[mid] == tlut) {
                break;
            }
        }
        if (mid == merged.materials.size()) {
            merged.materials.push_back(m);
            merged.material_images.push_back(img);
            merged.material_tlut.push_back(tlut);
        }
        merged.material_ids.push_back(mid);
        merged.triangle_layers.push_back(src.triangle_layers[t]);
    }
}

void ObjectModelDecoder::runModel(const GraphNode *node, ObjectExtract::Frame0Animator *frame0) {
    model_ = {};
    if (!node) {
        return;
    }

    std::vector<ObjectExtract::DisplayListWithTransform> dls =
        ObjectExtract::collectDisplayLists(*node, frame0);
    if (dls.empty()) {
        return;
    }

    // 与区域路径一致：按 layer 升序解码（游戏按层升序渲染且每层设置 render
    // mode；stable_sort 保持同层内的 geo 遍历顺序）。对象模型目前逐 DL 全新
    // 状态解释，排序主要保证输出顺序与未来的状态继承语义一致。
    std::stable_sort(dls.begin(), dls.end(),
                     [](const DisplayListWithTransform &a, const DisplayListWithTransform &b) {
                         return a.layer < b.layer;
                     });

    // billboard DL 按 pivot 分组：每个 GEO_BILLBOARD 节点一个 BillboardPart
    //（该节点自身 DL + 其子树 DL；pivot 相同的子树合并为同一 part）。
    struct PartAcc {
        Vec3<float> pivot;
        GBI::Mesh mesh;
    };
    GBI::Mesh merged;
    std::vector<PartAcc> parts;
    for (const auto &dlt : dls) {
        // Treasure World 等 hack 的对象模型 geo 里可能有空/无效 DL 地址
        // （0x00000000 / 0xFFFFFFFF），跳过而不是去解码（见 docs/engine-notes.md）。
        if (dlt.dl.seg < 0 || dlt.dl.seg > 31 || (dlt.dl.seg == 0 && dlt.dl.offset == 0)) {
            continue;
        }
        GBI::DLInterpreter interp(seg_table_, warnings_);
        // 对象模型暂按逐 DL 全新状态解释（不做跨 DL 状态继承，见 Engine.md §5）。
        GBI::Mesh &decoded = interp.run(dlt.dl, /*reset_state=*/true, dlt.layer);
        ObjectExtract::applyTransform(decoded, dlt.transform);
        if (dlt.is_billboard) {
            auto it = std::find_if(parts.begin(), parts.end(), [&](const PartAcc &p) {
                return p.pivot.x == dlt.billboard_pivot.x && p.pivot.y == dlt.billboard_pivot.y
                    && p.pivot.z == dlt.billboard_pivot.z;
            });
            if (it == parts.end()) {
                parts.push_back(PartAcc {dlt.billboard_pivot, {}});
                it = parts.end() - 1;
            }
            mergeMesh(it->mesh, std::move(decoded));
        } else {
            mergeMesh(merged, std::move(decoded));
        }
    }
    if (merged.indices.empty() && parts.empty()) {
        return;
    }

    model_.mesh = std::move(merged);
    model_.billboard_parts.reserve(parts.size());
    for (auto &part : parts) {
        if (part.mesh.indices.empty()) {
            continue;
        }
        // 顶点相对 pivot 定位（pivot-relative）：渲染端把部分节点放在 pivot 上，
        // 每帧把朝向设为相机的逆基（面向相机的旋转以 pivot 为轴心）。
        ObjectExtract::applyTransform(
            part.mesh, mtxfTranslation(-part.pivot.x, -part.pivot.y, -part.pivot.z));
        BillboardPart out_part;
        out_part.pivot = part.pivot;
        out_part.mesh = std::move(part.mesh);
        decodeTextures(out_part.mesh, out_part.textures);
        model_.billboard_parts.push_back(std::move(out_part));
    }
    decodeTextures(model_.mesh, model_.textures);
}

void ObjectModelDecoder::decodeTextures(const GBI::Mesh &mesh,
                                        std::vector<GBI::Texture> &textures) {
    textures.resize(mesh.materials.size());
    GBI::TextureDecoder tex_decoder(seg_table_);
    for (size_t m = 0; m < mesh.materials.size(); m++) {
        if (mesh.materials[m].textured && mesh.material_images[m] != 0) {
            if (tex_decoder.run(mesh.materials[m], segAddress(mesh.material_images[m]),
                                mesh.material_tlut[m])) {
                textures[m] = tex_decoder.takeTexture();
            }
        }
    }
}

ObjectModel ObjectModelDecoder::takeModel() {
    return std::move(model_);
}

} // namespace ObjectExtract
