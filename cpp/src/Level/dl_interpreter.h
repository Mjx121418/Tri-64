#ifndef DL_INTERPRETER_H
#define DL_INTERPRETER_H

#include "Level/dl_command.h"
#include "Memory/segment.h"
#include <array>
#include <cstdint>
#include <vector>

namespace GBI {

// G_SETCOMBINE 的 mux 输入源（gbi.h G_CCMUX/G_ACMUX）。用于决定未纹理材质的
// 底色来源（SHADE=顶点色 / PRIMITIVE / ENVIRONMENT）与 alpha 来源。
enum class CombineSource : uint8_t {
    Combined = 0, Texel0, Texel1, Primitive, Shade, Env, One, Zero, Noise, Other,
};

// 解码 G_SETCOMBINE 两个 mux 字：颜色/alpha 输出的有效来源。C 输入为 0（或
// G_CCMUX_0=31）时输出 ≈ D；否则混入 A/B/C（常见的是 G_CC_MODULATERGB 的
// TEXEL0×SHADE，已由 combineUsesTexel 判为纹理）。未纹理的常见组合（G_CC_SHADE
// /G_CC_PRIMITIVE/G_CC_ENVIRONMENT）都落在"输出 = D"上。
CombineSource combineColorSource(uint32_t mux0, uint32_t mux1);
CombineSource combineAlphaSource(uint32_t mux0, uint32_t mux1);
// 颜色输出是否用到 SHADE（顶点色/光照 shade）：G_CC_MODULATERGB（C=SHADE）、
// G_CC_SHADE（D=SHADE）等。渲染端据此决定是否用顶点色调制/作底色。
bool combineUsesShade(uint32_t mux0, uint32_t mux1);
// 颜色输出是否采样纹素（与 combineUsesTexel 等价，供渲染端判断纹理材质）。
bool combineUsesTexel(uint32_t mux0, uint32_t mux1);

// 输出网格顶点（模型空间）
struct MeshVertex {
    float position[3];
    float normal[3];
    float uv[2];
    // 顶点第 4 个字（coordinate_or_normal）：G_LIGHTING 开启 = 有符号法线
    // （见 normal）；关闭 = 顶点 RGBA 色（fast3d 的 G_SHADE 插值用）。
    // 导出未输出顶点色，保留供未来使用。
    uint8_t color[4];
};

// 材质：RDP 命令累积的渲染状态，三角形按内容归组（材质表去重）
struct Material {
    // G_SETCOMBINE 的两个 mux 字：选择颜色/alpha 混合的 A/B/C/D 输入
    // （纹素、prim/env/fog 颜色、alpha 等）。SM64 关卡 DL 用它配置
    // "纹素 × prim + env" 等组合。导出未解析混合，保留原始字以忠实记录状态。
    uint32_t combine_w0 {0};
    uint32_t combine_w1 {0};
    uint8_t prim_color[4] {0, 0, 0, 0}; // G_SETPRIMCOLOR（MTL 的 Kd 用）
    // G_SETENVCOLOR：combine 的 E 输入与 LOD 相关颜色，SM64 用它调制纹素。
    // 导出未使用，保留以记录材质状态。
    uint8_t env_color[4] {0, 0, 0, 0};
    // G_SETFOGCOLOR：RDP 雾混合的颜色（G_FOG 开启时随距离渐入）。
    // 导出未模拟雾，保留以记录材质状态。
    uint8_t fog_color[4] {0, 0, 0, 0};
    uint8_t tile_fmt {0};            // G_SETTILE 纹理格式（0=RGBA 2=CI 3=IA 4=I）
    uint8_t tile_siz {0};            // G_SETTILE 位深（0=4b 1=8b 2=16b 3=32b）
    uint16_t tex_sl {0};          // 纹理横坐标最小值
    uint16_t tex_tl {0};          // 纹理纵坐标最小值
    uint16_t tex_sh {0};          // 纹理横坐标最大值
    uint16_t tex_th {0};          // 纹理纵坐标最大值
    uint16_t tex_dxt {0};            // G_LOADBLOCK 的 DXT（w1 低 12 位，编码源图像行宽）
    bool textured {false};           // 有效纹理开关：G_TEXTURE_ENABLE && 材质采样 TEXEL
    bool combine_uses_texel {false}; // G_SETCOMBINE 的颜色/alpha mux 用到 TEXEL0/1
    bool lit {false};                // 几何模式 G_LIGHTING（未纹理材质据此选环境色）
    bool tex_clamp_s {false};        // G_SETTILE 的 S 方向 G_TX_CLAMP（否则 WRAP/MIRROR）
    bool tex_clamp_t {false};        // G_SETTILE 的 T 方向 G_TX_CLAMP
    uint8_t tex_palette {0};         // G_SETTILE 的 palette（CI4 调色板索引）
    uint16_t tex_line {0};           // G_SETTILE 的 line（TMEM 行跨度 64 位字）
    uint8_t lut_type {0};            // OTHERMODE 的 TEXTLUT 位域（G_TT_*：CI 调色板格式）

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
            && textured == o.textured
            && combine_uses_texel == o.combine_uses_texel
            && lit == o.lit
            && tex_clamp_s == o.tex_clamp_s && tex_clamp_t == o.tex_clamp_t
            && tex_palette == o.tex_palette && tex_line == o.tex_line
            && lut_type == o.lut_type;
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
    // 与 materials 并行的 CI 调色板（TLUT）源图像地址（0 = 无调色板）；
    // 同材质同纹理但调色板不同（palette 索引不同）的三角形也要分开。
    std::vector<uint32_t> material_tlut;
};

// fast3d RSP 顶点缓冲大小
inline constexpr size_t kVertexBufferSize = 32;

// RDP 纹理状态常量：tmem 共 4096 字节（512 个 64 位字），渲染 tile 固定为 0
inline constexpr uint32_t kTMEMWords = 512;
inline constexpr uint8_t kRenderTile = 0;

// RSP 状态：投影矩阵 + 模型视图矩阵栈 + 顶点缓冲 + RDP 纹理绑定
struct RSPState {
    // 投影矩阵（gSPMatrix 带 MTX_PROJECTION 时载入）：SM64 用它把世界坐标
    // 变换到 NDC 供光栅化。导出不做投影（Godot 自行投影），仅忠实记录状态。
    Mtxf projection {};
    std::vector<Mtxf> matrix_stack;  // 模型视图矩阵栈（栈顶 = 当前矩阵，初始为单位阵）
    std::array<Vtx, kVertexBufferSize> vertices {};
    // 防御性纹理绑定：G_SETTILE 记录每个 tile 的 tmem；G_LOADBLOCK/LOADTILE
    // 把当前 G_SETTEXIMAGE 图像绑定到该 tmem 槽位。三角形采样渲染 tile（0）
    // 的 tmem → 查表得到真正加载的图像（支持"先加载多个纹理再切换"）。
    std::array<uint16_t, 8> tile_tmem {};       // G_SETTILE 的 tmem（64 位字，9 位）
    std::array<uint32_t, kTMEMWords> tmem_images {}; // tmem → 图像段地址（0=未绑定）
    // G_TEXTURE 的纹理坐标缩放：w1 = (S<<16)|T，16.16 定点（0xFFFF≈1.0）。
    // fast3d 在 G_VTX 时把它乘到每个顶点的纹理坐标上（dmem 0x124/0x126，
    // 默认 0，见 rsp/fast3d.s 的 dma_VTX/imm_TEXTURE）。
    uint16_t tex_scale_s {0};
    uint16_t tex_scale_t {0};
    // G_TEXTURE 的渲染 tile 与 mipmap 层级（F3D：w0 bits 8-13）。SM64 用 tile 0，
    // 多 tile/多级纹理渲染未模拟，仅记录状态。
    uint8_t texture_tile {0};
    uint8_t texture_lod {0};
    // G_SETTEXIMAGE 记录的当前图像（RDP 状态）：地址 + 行宽字段。
    SegmentedAddress tex_image {};
    // G_SETTEXIMAGE 的 w0 低 12 位（源图像行宽，texel）。
    // 注意：该字段实际未被使用 —— gbi.h 的 gsSetImage 存的是 (width-1) 且
    // SM64 传 1，所以恒为 0；纹理解码改用 G_LOADBLOCK 的 DXT 反推行宽。
    // 保留它仅为忠实记录原始命令状态（潜在 G_LOADTILE 支持）。
    uint16_t tex_image_width {0};
    // fast3d 的 Light_t（16 字节 = col[3],pad,colc[3],pad,dir[3],pad）。
    struct Light {
        uint8_t col[3];
        int8_t dir[3];
    };
    // 灯光槽（gsSPLight / G_MOVEMEM G_MV_L0-7）与活动灯数（G_MW_NUMLIGHT）。
    std::array<Light, 8> lights {};
    uint8_t num_lights {0};
    // 本 DL 是否加载过灯光（gsSPLight）。逐顶层 DL 解释会重置状态，依赖父 DL
    // 灯光（跨顶层 DL）的几何没有可用灯光：此时顶点色回退为白（不调光）。
    bool lights_loaded {false};
    // G_MW_FOG 的 fog 系数（mult<<16 | offset，gsSPFogFactor）。
    uint16_t fog_mult {0};
    uint16_t fog_offset {0};
    // G_SETOTHERMODE_H/L 累积的 OTHERMODE 位域（周期类型/纹理过滤/LUT 等）。
    uint32_t othermode {0};
    // G_LOADTLUT 加载的调色板（tmem 槽位 → 源图像段地址，0 = 未加载）：
    // CI 纹理解码时查表得到调色板图像。
    std::array<uint32_t, kTMEMWords> tlut_images {};

    // --- 持久 RDP 渲染寄存器 ---
    // 游戏只在分层渲染时改 render mode，其余渲染状态（combine/颜色/tile/几何
    // 模式/纹理绑定/灯光）跨顶层 DL 继承（rendering_graph_node.c 的
    // geo_process_master_list）。这些寄存器放在 RSPState（而非 Material），
    // 解释器"继续运行"时保留，Material 在 drawTriangle 时从它们快照。
    uint32_t geometry_mode {0}; // 几何模式位（G_LIGHTING / G_TEXTURE_ENABLE 等）
    // G_SETCOMBINE 的两个 mux 字：颜色/alpha 混合的 A/B/C/D 输入。RDP 复位默认
    // 为 0（全 COMBINED）；未设置 combine 的 DL 继承前一个 DL 留下的值。
    uint32_t combine_w0 {0};
    uint32_t combine_w1 {0};
    uint8_t prim_color[4] {0, 0, 0, 0}; // G_SETPRIMCOLOR
    uint8_t env_color[4] {0, 0, 0, 0};  // G_SETENVCOLOR
    uint8_t fog_color[4] {0, 0, 0, 0};  // G_SETFOGCOLOR
    // G_SETTILE / G_SETTILESIZE / G_LOADBLOCK 的渲染 tile 配置。
    uint8_t tile_fmt {0};            // 纹理格式（0=RGBA 2=CI 3=IA 4=I）
    uint8_t tile_siz {0};            // 位深（0=4b 1=8b 2=16b 3=32b）
    uint16_t tex_sl {0};          // 纹理横坐标最小值
    uint16_t tex_tl {0};          // 纹理纵坐标最小值
    uint16_t tex_sh {0};          // 纹理横坐标最大值
    uint16_t tex_th {0};          // 纹理纵坐标最大值
    uint16_t tex_dxt {0};            // G_LOADBLOCK 的 DXT（编码源图像行宽）
    bool tex_clamp_s {false};        // S 方向 G_TX_CLAMP（否则 WRAP/MIRROR）
    bool tex_clamp_t {false};        // T 方向 G_TX_CLAMP
    uint8_t tex_palette {0};         // CI4 调色板索引
    uint16_t tex_line {0};           // TMEM 行跨度（64 位字）
    uint8_t lut_type {0};            // OTHERMODE 的 TEXTLUT（CI 调色板格式）
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
    CommandDecoder cmd_decoder_;
    RSPState state_;
    std::vector<SegmentedAddress> dl_stack_;
    Mesh mesh_;
    uint64_t steps_ {0};

    bool finished {false};
    Material material_;          // 当前材质快照（drawTriangle 时从 state_ 重建）

public:
    explicit DLInterpreter(const SegmentTable &seg_table) :
        seg_table_(seg_table), cmd_decoder_(seg_table) {}

    // 执行 dl。reset_state 为 true（场景第一个 DL）时把 RDP/RSP 复位（combine
    // 清 0、几何模式=游戏启动默认、num_lights=1、清空纹理绑定/灯光）；false 时
    // 保留上个 DL 的渲染寄存器（游戏里跨 DL 继承）。矩阵栈总是重置为单位阵。
    // 每次 run 从空 mesh_ 开始，本 DL 的三角形经 mesh() 取得。
    Mesh &run(SegmentedAddress dl, bool reset_state);

    // 本 DL 累积的网格（run 后读取）。
    Mesh &mesh() { return mesh_; }

    // 只读访问 RSP 状态（灯光/OTHERMODE/fog/TLUT 等提取的数据），供测试与未来
    // 光照实现使用。
    const RSPState &state() const { return state_; }

private:
    void execute(const DecodedCommand &cmd, SegmentedAddress &pc);
    void branch(const DecodedCommand &cmd, SegmentedAddress &pc);
    void end(SegmentedAddress &pc);
    void handleMtx(const DecodedCommand &cmd);
    void handlePopMtx(const DecodedCommand &cmd);
    void handleGeometryMode(const DecodedCommand &cmd, bool set);
    // G_MOVEMEM：解析灯光（gsSPLight / G_MV_L0-7）等 RSP 状态数据。
    void handleMovemem(const DecodedCommand &cmd);
    // G_MOVEWORD：G_MW_NUMLIGHT / G_MW_FOG / G_MW_LIGHTCOL 等 RSP 状态数据。
    void handleMoveword(const DecodedCommand &cmd);
    // G_SETOTHERMODE_H/L：更新 OTHERMODE 位域。
    void handleOtherMode(const DecodedCommand &cmd);
    // G_RDPSETOTHERMODE（0xEF）：整体设置 OTHERMODE 高 32 位。
    void handleRdpOtherMode(const DecodedCommand &cmd);
    // G_LOADTLUT：记录调色板加载（tmem → 源图像），供 CI 纹理解码。
    void handleLoadTLUT(const DecodedCommand &cmd);
    // 从 OTHERMODE 提取材质相关字段（TEXTLUT 类型 → state_.lut_type）。
    void updateOtherModeFields();
    void loadVertices(const DecodedCommand &cmd);
    void drawTriangle(const DecodedCommand &cmd);
    void appendVertex(const Vtx &v);
    uint32_t materialId(uint32_t tex_image, uint32_t tlut);
    // 从 state_ 的渲染寄存器重建 material_（drawTriangle 时调用；三角形按
    // combine/颜色/tile/几何模式等内容归组去重）。
    void snapshotMaterial();
};

} // namespace GBI

#endif /* DL_INTERPRETER_H */
