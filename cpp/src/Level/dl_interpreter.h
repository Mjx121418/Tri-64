#ifndef DL_INTERPRETER_H
#define DL_INTERPRETER_H

#include "Level/dl_command.h"
#include "Memory/segment.h"
#include <array>
#include <cstdint>
#include <vector>

namespace GBI {

// 输出网格顶点（模型空间）
struct MeshVertex {
    float position[3];
    float normal[3];
    float uv[2];
    uint8_t color[4];
};

// 材质：RDP 命令累积的渲染状态，三角形按内容归组（材质表去重）
struct Material {
    uint32_t combine_w0 {0};         // G_SETCOMBINE 原始字（M3 解析混合方式）
    uint32_t combine_w1 {0};
    uint8_t prim_color[4] {0, 0, 0, 0}; // G_SETPRIMCOLOR
    uint8_t env_color[4] {0, 0, 0, 0};  // G_SETENVCOLOR
    uint8_t fog_color[4] {0, 0, 0, 0};  // G_SETFOGCOLOR
    uint8_t tile_fmt {0};            // G_SETTILE 纹理格式（0=RGBA 2=CI 3=IA 4=I）
    uint8_t tile_siz {0};            // G_SETTILE 位深（0=4b 1=8b 2=16b 3=32b）
    uint16_t tex_sl {0};          // 纹理横坐标最小值
    uint16_t tex_tl {0};          // 纹理纵坐标最小值
    uint16_t tex_sh {0};          // 纹理横坐标最大值
    uint16_t tex_th {0};          // 纹理纵坐标最大值
    uint16_t tex_dxt {0};            // G_LOADBLOCK 的 DXT（w1 低 12 位，编码源图像行宽）
    bool textured {false};           // 几何模式 G_TEXTURE_ENABLE（G_TEXTURE 开关）

    bool operator==(const Material &o) const {
        return combine_w0 == o.combine_w0 && combine_w1 == o.combine_w1
            && prim_color[0] == o.prim_color[0] && prim_color[1] == o.prim_color[1]
            && prim_color[2] == o.prim_color[2] && prim_color[3] == o.prim_color[3]
            && env_color[0] == o.env_color[0] && env_color[1] == o.env_color[1]
            && env_color[2] == o.env_color[2] && env_color[3] == o.env_color[3]
            && fog_color[0] == o.fog_color[0] && fog_color[1] == o.fog_color[1]
            && fog_color[2] == o.fog_color[2] && fog_color[3] == o.fog_color[3]
            && tile_fmt == o.tile_fmt && tile_siz == o.tile_siz
            && tex_sl == o.tex_sl && tex_tl == o.tex_tl
            && tex_sh == o.tex_sh && tex_th == o.tex_th
            && tex_dxt == o.tex_dxt
            && textured == o.textured;
    }

    uint16_t tex_width() const {
        return static_cast<uint16_t>((tex_sh - tex_sl) / 4 + 1);
    }

    uint16_t tex_height() const {
        return static_cast<uint16_t>((tex_th - tex_tl) / 4 + 1);
    }
};

// 输出网格
struct Mesh {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;      // 每三角形 3 个索引
    std::vector<uint32_t> material_ids; // 每三角形 → materials 表
    std::vector<Material> materials;    // 材质表（按内容去重，不含图像地址）
    // 与 materials 并行的纹理源：每个材质的解析图像地址（段地址打包成 u32，
    // 0 = 未绑定）。图像属于材质去重键（同图块配置、不同图像的三角形不能合并），
    // 但它是 drawTriangle 时从 RSP 状态解析出来的，不属于 Material 本身。
    std::vector<uint32_t> material_images;
};

// fast3d RSP 顶点缓冲大小
inline constexpr size_t kVertexBufferSize = 32;

// RDP 纹理状态常量：tmem 共 4096 字节（512 个 64 位字），渲染 tile 固定为 0
inline constexpr uint32_t kTMEMWords = 512;
inline constexpr uint8_t kRenderTile = 0;

// RSP 状态：投影矩阵 + 模型视图矩阵栈 + 顶点缓冲 + RDP 纹理绑定
struct RSPState {
    Mtxf projection {};              // 投影矩阵（不应用于网格输出，Godot 自行投影）
    std::vector<Mtxf> matrix_stack;  // 模型视图矩阵栈（栈顶 = 当前矩阵，初始为单位阵）
    std::array<Vtx, kVertexBufferSize> vertices {};
    // 防御性纹理绑定：G_SETTILE 记录每个 tile 的 tmem；G_LOADBLOCK/LOADTILE
    // 把当前 G_SETTEXIMAGE 图像绑定到该 tmem 槽位。三角形采样渲染 tile（0）
    // 的 tmem → 查表得到真正加载的图像（支持"先加载多个纹理再切换"）。
    std::array<uint8_t, 8> tile_tmem {};        // G_SETTILE 的 tmem（64 位字）
    std::array<uint32_t, kTMEMWords> tmem_images {}; // tmem → 图像段地址（0=未绑定）
    // G_TEXTURE 的纹理坐标缩放：w1 = (S<<16)|T，16.16 定点（0xFFFF≈1.0）。
    // fast3d 在 G_VTX 时把它乘到每个顶点的纹理坐标上（dmem 0x124/0x126，
    // 默认 0，见 rsp/fast3d.s 的 dma_VTX/imm_TEXTURE）。
    uint16_t tex_scale_s {0};
    uint16_t tex_scale_t {0};
    // G_SETTEXIMAGE 记录的当前图像（RDP 状态）：地址 + 行宽字段。
    SegmentedAddress tex_image {};
    // G_SETTEXIMAGE 的 w0 低 12 位（源图像行宽，texel）。
    // 注意：该字段实际未被使用 —— gbi.h 的 gsSetImage 存的是 (width-1) 且
    // SM64 传 1，所以恒为 0；纹理解码改用 G_LOADBLOCK 的 DXT 反推行宽。
    // 保留它仅为忠实记录原始命令状态（潜在 G_LOADTILE 支持）。
    uint16_t tex_image_width {0};
};

// DL 解释器：执行一条 DL（含子 DL 调用），累积三角形到 Mesh。
//
// 当前阶段（Milestone 2）：
//   - G_MTX / G_POPMTX：模型视图矩阵栈（push/load/multiply）+ 投影矩阵
//   - 顶点经模型视图矩阵变换到世界空间（投影跳过）
//   - G_VTX / G_TRI1 / G_DL / G_ENDDL 累积三角形
//   - 材质命令（SETCOMBINE/颜色/SETTILE/TEXIMAGE/TEXTURE/几何模式）→ Material 表
class DLInterpreter {
    const SegmentTable &seg_table_;
    RSPState state_;
    std::vector<SegmentedAddress> dl_stack_;
    Mesh mesh_;
    uint64_t steps_ {0};

    bool finished {false};
    uint32_t geometry_mode_ {0}; // fast3d 几何模式位（G_TEXTURE_ENABLE 等）
    Material material_;          // 当前累积的材质

public:
    explicit DLInterpreter(const SegmentTable &seg_table) : seg_table_(seg_table) {}

    Mesh &run(SegmentedAddress dl);

private:
    void execute(const DecodedCommand &cmd, SegmentedAddress &pc);
    void branch(const DecodedCommand &cmd, SegmentedAddress &pc);
    void end(SegmentedAddress &pc);
    void handleMtx(const DecodedCommand &cmd);
    void handlePopMtx(const DecodedCommand &cmd);
    void handleGeometryMode(const DecodedCommand &cmd, bool set);
    void loadVertices(const DecodedCommand &cmd);
    void drawTriangle(const DecodedCommand &cmd);
    void appendVertex(const Vtx &v);
    uint32_t materialId(uint32_t tex_image);
};

} // namespace GBI

#endif /* DL_INTERPRETER_H */
