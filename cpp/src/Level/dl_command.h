#ifndef DL_COMMAND_H
#define DL_COMMAND_H

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

    // --- G_MOVEMEM ---
    uint8_t memIndex() const { return (w0 >> 16) & 0xFF; }
    uint16_t memLength() const { return w0 & 0xFFFF; }
    SegmentedAddress memAddress() const { return segAddress(w1); }
};

// 读取并解码 addr 处的一条命令（8 字节）
DecodedCommand decodeDLCommand(SegmentedAddress addr, const SegmentTable &seg_table);

} // namespace GBI

#endif /* DL_COMMAND_H */
