#ifndef MOVTEX_H
#define MOVTEX_H

#include "Memory/segment.h"
#include <cstdint>
#include <vector>

// 移动纹理（movtex）解码器：水/熔岩等带动画纹理的表面。
//
// 关卡 geo 里的 GEO_ASM(param, geo_movtex_*) 节点引用 MovtexQuadCollection
//（s16 id → s16 流）。集合位于关卡段（如城堡外侧 seg 0x07），格式
//（moving_texture.c movtex_gen_from_quad_array + moving_texture_macros.h）：
//   MovtexQuadCollection = {s16 id, s32 quadArraySegmented}...（以 id=-1 结束）
//   quadArray = [numLists:s16, numLists × MovtexQuad(14×s16)]
//   MovtexQuad = rot, rotspeed, scale, x1,z1,x2,z2,x3,z3,x4,z4, rotDir, alpha,
//                textureId
// 四边形的 Y 由匹配的环境区域（水盒）高度提供；这里只提取四边形数据
//（本地 XY 平面，Y 由调用方从水盒补上）。
namespace Movtex {

// 单个 movtex 四边形（与 decomp 的 struct MovtexQuad 一致，14 个 s16）。
struct Quad {
    int16_t id {0};       // 集合里的条目 id（= 环境区域 id 关联）
    int16_t rot {0};
    int16_t rot_speed {0};
    int16_t scale {0};
    int16_t x1, z1, x2, z2, x3, z3, x4, z4;
    int16_t rot_dir {0};   // 0=顺时针 1=逆时针
    int16_t alpha {0};
    int16_t texture_id {0}; // TEXTURE_WATER/MIST/JRB_WATER/UNK_WATER/LAVA/...
};

struct Data {
    std::vector<Quad> quads;
    std::string error;
    bool ok {false};
};

// movtex 解码器（镜像 LevelScriptVM 的结构）：绑定段表，run(segments_to_scan)
// 在给定段里扫描 MovtexQuadCollection 并提取全部四边形，结果经 data() 取得。
class MovtexDecoder {
    const SegmentTable &seg_table_;
    Data data_;

public:
    explicit MovtexDecoder(const SegmentTable &seg_table) : seg_table_(seg_table) {}

    // 扫描给定段（关卡段，如城堡外侧 seg 0x07）里的 movtex 四边形集合。
    void run(const std::vector<int16_t> &segments_to_scan);

    const Data &data() const { return data_; }
};

} // namespace Movtex

#endif /* MOVTEX_H */
