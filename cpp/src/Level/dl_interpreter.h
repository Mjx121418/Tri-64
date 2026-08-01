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

// 输出网格
struct Mesh {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;      // 每三角形 3 个索引
    std::vector<uint32_t> material_ids; // 每三角形材质 id（暂恒为 0）
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
//   - 材质/RDP 命令暂忽略（后续步骤记录材质）
class DLInterpreter {
    const SegmentTable &seg_table_;
    RSPState state_;
    std::vector<SegmentedAddress> dl_stack_;
    Mesh mesh_;
    uint64_t steps_ {0};

public:
    explicit DLInterpreter(const SegmentTable &seg_table) : seg_table_(seg_table) {}

    Mesh &run(SegmentedAddress dl);

private:
    void execute(const DecodedCommand &cmd, SegmentedAddress &pc);
    void handleMtx(const DecodedCommand &cmd);
    void handlePopMtx(const DecodedCommand &cmd);
    void loadVertices(const DecodedCommand &cmd);
    void drawTriangle(const DecodedCommand &cmd);
    void appendVertex(const Vtx &v);
};

} // namespace GBI

#endif /* DL_INTERPRETER_H */
