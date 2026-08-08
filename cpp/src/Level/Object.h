#ifndef OBJECT_H
#define OBJECT_H

#include "Level/area.h"
#include "Level/dl_interpreter.h"
#include "Level/texture.h"
#include "Memory/segment.h"
#include "Scripts/Collision.h"
#include "Scripts/preset_tables.h"
#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <vector>

// 对象相关逻辑（对象模型解码 + MACRO_OBJECTS/特殊对象展开），与关卡几何提取
// （LevelExtract）分离，避免 level_extract.cpp 过大。
namespace ObjectExtract {

// object_fields.h 的字段索引。行为脚本按这些索引读写对象状态。
namespace F {
    enum : uint8_t {
        Flags              = 0x01, // oFlags (U32)
        PosX               = 0x06, // oPosX (F32)
        PosY               = 0x07, // oPosY (F32)
        PosZ               = 0x08, // oPosZ (F32)
        MoveAnglePitch     = 0x0F, // oMoveAnglePitch (S32)
        MoveAngleYaw       = 0x10, // oMoveAngleYaw (S32)
        MoveAngleRoll      = 0x11, // oMoveAngleRoll (S32)
        FaceAnglePitch     = 0x12, // oFaceAnglePitch (S32)
        FaceAngleYaw       = 0x13, // oFaceAngleYaw (S32)
        FaceAngleRoll      = 0x14, // oFaceAngleRoll (S32)
        GraphYOffset       = 0x15, // oGraphYOffset (F32)
        AnimState          = 0x1A, // oAnimState (S32)
        Animations         = 0x26, // oAnimations（动画数组指针）
        InteractType       = 0x2A, // oInteractType (U32)
        BhvParams2ndByte   = 0x2F, // oBhvParams2ndByte (S32)
        Opacity            = 0x3D, // oOpacity (S32)
        BhvParams          = 0x40, // oBhvParams (S32)
        InteractionSubtype = 0x42, // oInteractionSubtype
        CollisionDistance  = 0x43, // oCollisionDistance (F32)
        DrawingDistance    = 0x45, // oDrawingDistance (F32)
    };
}

// 统一对象：行为脚本直接作用于它（像游戏里作用于 gCurrentObject）。rawData 是
// object_fields.h 的原始字段存储（索引 0x00-0x4F，每字段 32 位），带类型化
// 访问器；非字段成员镜像 struct Object（types.h）的直接成员 / header.gfx。
class Object {
public:
    std::array<uint32_t, 0x50> raw {};

    // --- object_fields.h 类型化访问器（镜像 OBJECT_FIELD_*）---
    uint32_t u32(uint8_t f) const { return raw[f]; }
    uint32_t &u32(uint8_t f) { return raw[f]; }
    int32_t s32(uint8_t f) const { return static_cast<int32_t>(raw[f]); }
    int32_t &s32(uint8_t f) { return *reinterpret_cast<int32_t *>(&raw[f]); }
    float f32(uint8_t f) const { return std::bit_cast<float>(raw[f]); }
    float &f32(uint8_t f) { return *reinterpret_cast<float *>(&raw[f]); }
    int16_t s16(uint8_t f, uint8_t sub) const {
        return reinterpret_cast<const int16_t *>(&raw[f])[sub];
    }
    int16_t &s16(uint8_t f, uint8_t sub) {
        return reinterpret_cast<int16_t *>(&raw[f])[sub];
    }
    // 指针字段（ANIMS/VPTR/CVPTR）：raw 存打包的分段地址（seg<<24 | offset）。
    SegmentedAddress addr(uint8_t f) const;
    void setAddr(uint8_t f, const SegmentedAddress &a);

    // --- 非字段成员（镜像 struct Object 的直接成员 / header.gfx）---
    SegmentedAddress behavior {};       // 行为脚本（struct Object .behavior）
    SegmentedAddress collision_data {}; // LOAD_COLLISION_DATA 目标（.collisionData）
    float hitbox_radius {0};            // .hitboxRadius / hitboxHeight / hitboxDownOffset
    float hitbox_height {0};
    float hitbox_down_offset {0};
    float hurtbox_radius {0};           // .hurtboxRadius / hurtboxHeight
    float hurtbox_height {0};
    int8_t obj_list {-1};               // BEGIN(objList)；行为处理（M2）时填
    int8_t area_index {0};              // header.gfx.areaIndex
    int8_t active_area_index {0};       // header.gfx.activeAreaIndex
    int16_t model_id {0};               // header.gfx.sharedChild（我们存 model id）
    int16_t animate_index {-1};         // gfx.animInfo（ANIMATE 选中，出生即播放）
    Vec3<float> scale {1, 1, 1};        // header.gfx.scale（默认 1）
    bool active {true};                 // GRAPH_RENDER_ACTIVE（DISABLE_RENDERING 清除）
    bool invisible {false};             // GRAPH_RENDER_INVISIBLE（HIDE）
    bool billboard {false};             // GRAPH_RENDER_BILLBOARD
    bool deactivated {false};           // ACTIVE_FLAG_DEACTIVATED（DEACTIVATE）

    // --- 常用字段便捷访问 ---
    Vec3<float> pos() const;            // oPosX/Y/Z
    void setPos(const Vec3<float> &p);
    Vec3<int16_t> faceAngle() const;    // oFaceAnglePitch/Yaw/Roll（SM64 角度单位）
    Vec3<int16_t> moveAngle() const;    // oMoveAnglePitch/Yaw/Roll
    uint32_t behaviorArg() const;       // oBhvParams

    // --- 变换：出生数据 → 统一 Object（镜像 spawn_object / spawn_macro_object /
    //     spawn_special_objects，见 game/object_helpers.c 与 macro_special_objects.c）---
    static Object fromSpawnInfo(const ObjectSpawnInfo &info);
    static Object fromMacroObject(const MacroObjectSpawnInfo &entry,
                                  const PresetTables::MacroPreset &preset);
    static Object fromSpecialObject(const Collision::SpecialObject &entry,
                                    const PresetTables::SpecialPreset &preset);
};

// 单个对象模型：由 OBJECT 命令的 model id 对应的 geo 布局解码而来。
// 每个唯一 model id 只解码一次，所有引用它的对象实例复用这份网格。
struct ObjectModel {
    GBI::Mesh mesh;                      // 合并后的模型网格（含材质表与纹理源图像）
    std::vector<GBI::Texture> textures;  // 与 mesh.materials 并行：解码纹理
};

// 动画 frame-0：每个 animated part（GEO_ANIMATED_PART）在出生帧的平移/旋转增量。
struct Frame0Part {
    Vec3<float> translation {0, 0, 0};
    Vec3<int16_t> rotation {0, 0, 0};
};

// 逐 AP 计算动画 frame-0 值，镜像 rendering_graph_node.c 的 geo_process_animated_part
// 属性游标：第一个 AP 按动画 flags 读取平移，之后只读旋转；平移乘
// gCurrAnimTranslationMultiplier（animYTransDivisor == 0 时为 1.0）。next() 按
// geo 遍历顺序返回下一个 AP 的增量；动画缺失/越界返回 nullopt。
class Frame0Animator {
public:
    // animations: LOAD_ANIMATIONS 的目标（Animation* 数组，段地址）；
    // animate_index: ANIMATE 的索引（出生即播放的动画）。
    Frame0Animator(const SegmentTable &seg_table, SegmentedAddress animations, int16_t animate_index);
    bool ok() const { return ok_; }
    std::optional<Frame0Part> next();

private:
    const SegmentTable &seg_table_;
    bool ok_ {false};
    bool rotation_mode_ {false};
    uint32_t translate_type_ {0};
    float multiplier_ {1.0f};
    SegmentedAddress index_addr_ {};   // animindex（u16 对 (start,count)）
    SegmentedAddress values_addr_ {};  // animvalue（s16）
    uint32_t attr_offset_ {0};         // 当前属性在 animindex 内的字节偏移
    float readValue();                 // values[retrieve_animation_index(0)]
    void skipAttribute();
};

// 对象模型/对象出生数据解码器（镜像 LevelScriptVM 的结构）：构造时绑定段表，
// runModel(node[, frame0]) 重置并解码一个对象模型，结果经 model() 取得。
class ObjectModelDecoder {
    const SegmentTable &seg_table_;
    ObjectModel model_;

public:
    explicit ObjectModelDecoder(const SegmentTable &seg_table) : seg_table_(seg_table) {}

    // 把 src 追加进 merged：顶点/索引加 base 偏移，材质按内容 + 纹理源图像去重。
    static void mergeMesh(GBI::Mesh &merged, GBI::Mesh &&src);

    // 从 geo 布局图节点解码对象模型：收集 (DL, 变换) 对，跳过无效 DL 地址
    // （0x00000000 / 段越界，见 docs/engine-notes.md），把图节点变换（缩放/
    // 旋转/平移）+ 可选 frame-0 动画烘焙进顶点，合并为一个网格并解码每材质纹理。
    // node 为 null 或没有可解码 DL 时 model() 返回空模型。
    void runModel(const GraphNode *node, Frame0Animator *frame0 = nullptr);

    const ObjectModel &model() const { return model_; }
};

} // namespace ObjectExtract

#endif /* OBJECT_H */
