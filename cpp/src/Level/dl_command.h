#ifndef DL_COMMAND_H
#define DL_COMMAND_H

#include "Math/math.h"
#include "Memory/segment.h"
#include <cstdint>

// GBI (Graphics Binary Interface) 命令层。
//
// SM64 使用 fast3d (F3D) 微码，权威参考为 decomp 的 rsp/fast3d.s：
//   - DMA 命令      0x00-0x3F（G_MTX/G_MOVEMEM/G_VTX/G_DL，其余 NOOP）
//   - 立即命令      0x80-0xBF（G_SETGEOMETRYMODE...G_TRI1）
//   - RDP 命令      0xC0-0xFF
// 每条命令 8 字节（大端），opcode 为第一字节。
//
// 各命令编码（已用真实 DL 数据验证）：
//   G_MTX   w0 = (0x01<<24)|(params<<16)|0x40        w1 = 矩阵分段地址
//   G_VTX   w0 = (0x04<<24)|(((n-1)<<4|v0)<<16)|(16n) w1 = 顶点数据地址
//   G_DL    w0 = (0x06<<24)|(push<<16)                w1 = 子 DL 地址
//   G_TRI1  w0 = (0xBF<<24)                           w1 = (flag<<24)|(v0*10<<16)|(v1*10<<8)|(v2*10)
//   G_SETGEOMETRYMODE w0 = (0xB7<<24)                 w1 = 几何模式位
//   G_TEXTURE w0 = (0xBB<<24)|(s<<16)|(t<<8)|(level<<4)|(tile)  w1 = 缩放

namespace GBI {

// fast3d (SM64) 命令 opcode
enum Opcode : uint8_t {
    G_SPNOOP          = 0x00,
    G_MTX             = 0x01,
    G_MOVEMEM         = 0x03,
    G_VTX             = 0x04,
    G_DL              = 0x06,

    G_CLEARGEOMETRYMODE = 0xB6,
    G_SETGEOMETRYMODE = 0xB7,
    G_ENDDL           = 0xB8,
    G_SETOTHERMODE_L  = 0xB9,
    G_SETOTHERMODE_H  = 0xBA,
    G_TEXTURE         = 0xBB,
    G_MOVEWORD        = 0xBC,
    G_POPMTX          = 0xBD,
    G_CULLDL          = 0xBE,
    G_TRI1            = 0xBF,

    // RDP
    G_DPLOADSYNC      = 0xE6,
    G_DPPIPESYNC      = 0xE7,
    G_DPTILESYNC      = 0xE8,
    G_DPFULLSYNC      = 0xE9,
    G_SETTILESIZE     = 0xF2,
    G_LOADBLOCK       = 0xF3,
    G_LOADTILE        = 0xF4,
    G_SETTILE         = 0xF5,
    G_SETFILLCOLOR    = 0xF7,
    G_SETFOGCOLOR     = 0xF8,
    G_SETBLENDCOLOR   = 0xF9,
    G_SETPRIMCOLOR    = 0xFA,
    G_SETENVCOLOR     = 0xFB,
    G_SETCOMBINE      = 0xFC,
    G_SETTEXIMAGE     = 0xFD,
    G_SETZIMG         = 0xFE,
    G_SETCIMG         = 0xFF,
};

// G_MTX 参数（gbi.h）
enum MtxParams : uint8_t {
    MTX_PROJECTION = 0x01,
    MTX_LOAD       = 0x02,
    MTX_PUSH       = 0x04,
};

// fast3d 几何模式位（rsp/gbi.inc）
enum GeometryMode : uint32_t {
    G_ZBUFFER         = 0x00000001,
    G_TEXTURE_ENABLE  = 0x00000002,
    G_SHADE           = 0x00000004,
    G_CULL_FRONT      = 0x00001000,
    G_CULL_BACK       = 0x00002000,
    G_FOG             = 0x00010000,
    G_LIGHTING        = 0x00020000,
    G_TEXTURE_GEN     = 0x00040000,
    G_TEXTURE_GEN_LINEAR = 0x00080000,
};

// G_MOVEWORD 偏移（gbi.h G_MW_*）
enum MoveWordOffset : uint16_t {
    G_MW_NUMLIGHT = 0x0008,
    G_MW_CLIP     = 0x000C,
    G_MW_SEGMENT  = 0x0010,
    G_MW_FOG      = 0x0014,
    G_MW_LIGHTCOL = 0x0018,
    G_MW_MATRIX   = 0x001C,
};

// 解码后的命令：保留原始字，提供按命令解析的访问器
struct DecodedCommand {
    uint8_t opcode;
    uint32_t w0;
    uint32_t w1;
    SegmentedAddress addr; // 命令所在地址（调试用）

    // --- G_MTX ---
    uint8_t mtxParams() const { return (w0 >> 16) & 0xFF; }
    SegmentedAddress mtxAddress() const { return segAddress(w1); }

    // --- G_VTX：w0 字节1 = (n-1)<<4 | v0，低16位 = 16*n ---
    uint8_t vtxCount() const { return (((w0 >> 16) & 0xFF) >> 4) + 1; }
    uint8_t vtxDestIndex() const { return ((w0 >> 16) & 0xFF) & 0x0F; }
    uint16_t vtxBytes() const { return w0 & 0xFFFF; }
    SegmentedAddress vtxAddress() const { return segAddress(w1); }

    // --- G_DL：w0 字节1 = 0 表示压栈调用（ENDDL 返回），非 0 表示分支（不返回） ---
    bool dlBranch() const { return ((w0 >> 16) & 0xFF) != 0; }
    SegmentedAddress dlAddress() const { return segAddress(w1); }

    // --- G_TRI1：w1 = (flag<<24)|(v0*10<<16)|(v1*10<<8)|(v2*10) ---
    uint8_t triFlag() const { return w1 >> 24; }
    uint8_t triV0() const { return ((w1 >> 16) & 0xFF) / 10; }
    uint8_t triV1() const { return ((w1 >> 8) & 0xFF) / 10; }
    uint8_t triV2() const { return (w1 & 0xFF) / 10; }

    // --- G_TEXTURE ---
    uint8_t texTile() const { return w0 & 0x0F; }
    uint8_t texLevel() const { return (w0 >> 4) & 0x0F; }
    uint8_t texS() const { return (w0 >> 16) & 0xFF; }
    uint8_t texT() const { return (w0 >> 8) & 0xFF; }
    uint32_t texScale() const { return w1; }

    // --- G_MOVEWORD ---
    uint16_t mwOffset() const { return w0 & 0xFFFF; }
    uint32_t mwValue() const { return w1; }

    // --- G_POPMTX：w1 = 弹出数量 ---
    uint8_t popCount() const { return w1 & 0xFF; }

    // --- G_SET/CLEARGEOMETRYMODE：w1 = 几何模式位 ---
    uint32_t geometryMode() const { return w1; }

    // --- G_SETOTHERMODE_H/L：w0 = (cmd<<24)|(sft<<16)|(len<<8)|(高位)，w1 = 低32位 ---
    uint8_t omSft() const { return (w0 >> 16) & 0xFF; }
    uint8_t omLen() const { return (w0 >> 8) & 0xFF; }
    uint64_t omParams() const { return ((uint64_t)(w0 & 0xFF) << 32) | w1; }

    // --- G_SETCONBINE ---
    uint32_t combineMux0() const { return w0 & 0x00FFFFFF; }
    uint32_t combineMux1() const { return w1; }

    // --- G_SETPRIMCOLOR, G_SETENVCOLOR, G_SETFOGCOLOR ---
    uint8_t primColorM() const { return (w0 >> 8) & 0xFF; } // Minimum possible LOD value (clamped to this at minimum)
    uint8_t primColorL() const { return w0 & 0xFF; } // Primitive LOD fraction for mipmap filtering
    uint8_t colorR() const { return (w1 >> 24) & 0xFF; }
    uint8_t colorG() const { return (w1 >> 16) & 0xFF; }
    uint8_t colorB() const { return (w1 >> 8) & 0xFF; }
    uint8_t colorA() const { return w1 & 0xFF; }

    // --- G_SETTILE ---
    uint8_t tileFmt() const { return (w0 >> 21) & 0x7; }
    uint8_t tileSize() const { return (w0 >> 19) & 0x3; }

    // --- G_MOVEMEM ---
    uint8_t memIndex() const { return (w0 >> 16) & 0xFF; }
    uint16_t memLength() const { return w0 & 0xFFFF; }
    SegmentedAddress memAddress() const { return segAddress(w1); }
};

// 读取并解码 addr 处的一条命令（8 字节）
DecodedCommand decodeDLCommand(SegmentedAddress addr, const SegmentTable &seg_table);

// --- GBI 数据类型 ---

// 顶点（16 字节，与游戏内存布局一致）
struct Vtx {
    int16_t position[3];
    uint16_t flag;
    int16_t texture_coordinate[2]; // 5.11 定点
    uint8_t coordinate_or_normal[4]; // 法线（有符号）/ 颜色 + alpha
};

// 视口
struct Vp {
    int16_t scale[4];
    int16_t translate[4];
};

// 三角形（顶点索引 ×10 编码前）
struct Tri {
    uint8_t flag;
    uint8_t vertices[3];
};

// RSP 矩阵：4x4 16.16 定点
typedef Mat4<int32_t> Mtx;

// 浮点矩阵（行主序，平移在最后一行 m[3][0..2]）——解释器内部使用
using Mtxf = std::array<std::array<float, 4>, 4>;

// 单位阵
Mtxf mtxfIdentity();

// 矩阵乘法 a × b（结果先应用 b 再应用 a，与 decomp 的 mtxf_mul 一致）
Mtxf mtxfMul(const Mtxf &a, const Mtxf &b);

// 读取并解码一条 G_MTX 的 64 字节定点矩阵：
//   字节 0-31 = 各元素高 16 位（整数部分），字节 32-63 = 低 16 位（小数部分）
Mtxf decodeMtx(SegmentedAddress addr, const SegmentTable &seg_table);

// 光照（暂未使用，保留供后续光照支持）
struct Light {
    uint8_t diffuse[3];
    uint8_t diffuse_copy[3];
    int8_t direction[3]; // normalized
};

} // namespace GBI

#endif /* DL_COMMAND_H */
