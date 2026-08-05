#ifndef DL_COMMAND_H
#define DL_COMMAND_H

#include "Memory/segment.h"
#include "Math/math.h"
#include <array>
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
    // SM64 角色：tile = 渲染 tile 编号，level = mipmap 层级，s/t = 纹理偏移
    // （配合顶点纹理坐标实现滚动纹理），w1 = S/T 缩放（16.16 定点，见解释器的
    // tex_scale_s/t）。当前只用 w0 位 0（G_TEXTURE_ENABLE）与 w1 缩放。
    uint8_t texTile() const { return w0 & 0x0F; }
    uint8_t texLevel() const { return (w0 >> 4) & 0x0F; }
    uint8_t texS() const { return (w0 >> 16) & 0xFF; }
    uint8_t texT() const { return (w0 >> 8) & 0xFF; }
    uint32_t texScale() const { return w1; }

    // --- G_MOVEWORD ---
    // SM64 角色：把 w1 写入 RSP DMEM 偏移 mwOffset 处（G_MW_*：段表
    // G_MW_SEGMENT、灯光数 G_MW_NUMLIGHT、fog 系数等）。级别脚本用它设置段表；
    // DL 内的 MOVEWORD 目前被忽略。
    uint16_t mwOffset() const { return w0 & 0xFFFF; }
    uint32_t mwValue() const { return w1; }

    // --- G_POPMTX：fast3d 固定弹出 1 个矩阵，w1 被忽略（见 rsp/fast3d.s imm_POPMTX） ---

    // --- G_SET/CLEARGEOMETRYMODE：w1 = 几何模式位 ---
    uint32_t geometryMode() const { return w1; }

    // --- G_SETOTHERMODE_H/L ---
    // SM64 角色：按 (sft, len) 选中 OTHERMODE 字内的位域并写入 w1（周期类型、
    // 纹理过滤/镜像、zbuffer 等渲染参数）。导出目前不需要这些参数。
    uint8_t omSft() const { return (w0 >> 16) & 0xFF; }
    uint8_t omLen() const { return (w0 >> 8) & 0xFF; }
    uint64_t omParams() const { return ((uint64_t)(w0 & 0xFF) << 32) | w1; }

    // --- G_SETCONBINE ---
    uint32_t combineMux0() const { return w0 & 0x00FFFFFF; }
    uint32_t combineMux1() const { return w1; }

    // --- G_SETPRIMCOLOR, G_SETENVCOLOR, G_SETFOGCOLOR ---
    // primColorM/L = prim 颜色的 mipmap LOD 下限/小数部分（min level / lod
    // fraction，G_SETPRIMCOLOR 特有）；颜色本体见 colorR..A。导出未用。
    uint8_t primColorM() const { return (w0 >> 8) & 0xFF; }
    uint8_t primColorL() const { return w0 & 0xFF; }
    uint8_t colorR() const { return (w1 >> 24) & 0xFF; }
    uint8_t colorG() const { return (w1 >> 16) & 0xFF; }
    uint8_t colorB() const { return (w1 >> 8) & 0xFF; }
    uint8_t colorA() const { return w1 & 0xFF; }

    // --- G_LOADBLOCK, G_SETTILESIZE, G_LOADTLUT
    // G_LOADBLOCK 的 w1 低 12 位 = DXT（编码源图像行宽，见 gbi.h CALC_DXT）
    uint16_t lowS() const { return (w0 >> 12) & 0xFFF; }
    uint16_t lowT() const { return w0 & 0xFFF; }
    uint16_t highS() const { return (w1 >> 12) & 0xFFF; }
    uint16_t highT() const { return w1 & 0xFFF; }

    // --- G_SETTILE / G_LOADBLOCK / G_LOADTILE ---
    uint8_t rdpFmt() const { return (w0 >> 21) & 0x7; }
    uint8_t rdpSize() const { return (w0 >> 19) & 0x3; }
    uint8_t tileNum() const { return (w1 >> 24) & 0x7; } // w1 bits 24-26
    uint16_t tileTMEM() const { return w0 & 0x1FF; }     // 仅 G_SETTILE：tmem（64 位字）

    // --- G_MOVEMEM ---
    // SM64 角色：按 dmem 索引（G_MV_*：视口/灯光/矩阵等）把 DRAM 数据拷入
    // RSP DMEM。导出不渲染视口与光照，目前忽略。
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

// 浮点矩阵（行主序，平移在最后一行 m[3][0..2]）——解释器内部使用。
// 与 Math/math.h 的全局 Mtxf 同一布局，共用同一类型。
using Mtxf = ::Mtxf;

// 读取并解码一条 G_MTX 的 64 字节定点矩阵：
//   字节 0-31 = 各元素高 16 位（整数部分），字节 32-63 = 低 16 位（小数部分）
Mtxf decodeMtx(SegmentedAddress addr, const SegmentTable &seg_table);

} // namespace GBI

#endif /* DL_COMMAND_H */
