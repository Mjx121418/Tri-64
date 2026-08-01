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
    SegmentedAddress tex_image {};   // G_SETTEXIMAGE 纹理图像地址
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
            && tex_image.seg == o.tex_image.seg && tex_image.offset == o.tex_image.offset
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
    std::vector<Material> materials;    // 材质表（按内容去重）
};

// fast3d RSP 顶点缓冲大小
inline constexpr size_t kVertexBufferSize = 32;

// RSP 状态：投影矩阵 + 模型视图矩阵栈 + 顶点缓冲
struct RSPState {
    Mtxf projection {};              // 投影矩阵（不应用于网格输出，Godot 自行投影）
    std::vector<Mtxf> matrix_stack;  // 模型视图矩阵栈（栈顶 = 当前矩阵，初始为单位阵）
    std::array<Vtx, kVertexBufferSize> vertices {};
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
    uint32_t materialId();
};

} // namespace GBI

#endif /* DL_INTERPRETER_H */
