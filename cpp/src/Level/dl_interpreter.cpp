#include "Level/dl_interpreter.h"
#include "Math/math.h"
#include <cmath>
#include <cstdio>

namespace GBI {

namespace {

float fixedToFloat(Fast3D::Fixed value) {
    return static_cast<float>(value) / static_cast<float>(Fast3D::kFixedOne);
}

int32_t scaleTextureCoordinate(int16_t value, uint16_t scale) {
    // G_TEXTURE uses the RSP's signed product followed by a 16-bit shift.
    return static_cast<int32_t>((static_cast<int64_t>(value) * scale) >> 16);
}

} // namespace

namespace {
constexpr uint64_t kMaxSteps = 10'000'000;
constexpr size_t kMaxDLDepth = 32;

CombineSource decodeColorMux(uint32_t v) {
    switch (v & 0xF) {
        case 0: return CombineSource::Combined;
        case 1: return CombineSource::Texel0;
        case 2: return CombineSource::Texel1;
        case 3: return CombineSource::Primitive;
        case 4: return CombineSource::Shade;
        case 5: return CombineSource::Env;
        case 6: return CombineSource::One;
        case 7: return CombineSource::Noise;
        default: return CombineSource::Other;
    }
}

CombineSource decodeAlphaMux(uint32_t v) {
    switch (v & 0x7) {
        case 0: return CombineSource::Combined;
        case 1: return CombineSource::Texel0;
        case 2: return CombineSource::Texel1;
        case 3: return CombineSource::Primitive;
        case 4: return CombineSource::Shade;
        case 5: return CombineSource::Env;
        default: return CombineSource::Other;
    }
}

} // namespace

// 解码 G_SETCOMBINE 的 mux（gbi.h 的 GCCc0w0/GCCc1w0/GCCc0w1/GCCc1w1 打包布局），
// 检查颜色/alpha 的 A/B/C/D 输入是否用到 TEXEL0(1)/TEXEL1(2) —— RDP 只有此时
// 才采样纹理。G_CC_SHADE（全 SHADE，无 TEXEL）应判为未纹理。
bool combineUsesTexel(uint32_t mux0, uint32_t mux1) {
    const uint32_t vals[] = {
        (mux0 >> 20) & 0xF,  // 颜色 A
        (mux0 >> 15) & 0x1F, // 颜色 C
        (mux0 >> 12) & 0x7,  // alpha A
        (mux0 >> 9) & 0x7,   // alpha C
        (mux0 >> 5) & 0xF,   // alpha A1
        (mux0 >> 0) & 0x1F,  // alpha C1
        (mux1 >> 28) & 0xF,  // 颜色 B
        (mux1 >> 15) & 0x7,  // 颜色 D（gbi.h GCCc0w1：shift 15）
        (mux1 >> 12) & 0x7,  // alpha B
        (mux1 >> 9) & 0x7,   // alpha D
        (mux1 >> 24) & 0xF,  // alpha B1
        (mux1 >> 21) & 0x7,  // alpha A1
        (mux1 >> 18) & 0x7,  // alpha C1
        (mux1 >> 6) & 0x7,   // alpha D1
        (mux1 >> 3) & 0x7,   // alpha B1
        (mux1 >> 0) & 0x7,   // alpha D1
    };
    for (uint32_t v : vals) {
        if (v == 1 || v == 2) { // G_CCMUX_TEXEL0 / G_CCMUX_TEXEL1
            return true;
        }
    }
    return false;
}

// 颜色输出是否用到 SHADE（顶点色 / 光照 shade）。
bool combineUsesShade(uint32_t mux0, uint32_t mux1) {
    const uint32_t a = (mux0 >> 20) & 0xF;
    const uint32_t c = (mux0 >> 15) & 0x1F;
    const uint32_t b = (mux1 >> 28) & 0xF;
    const uint32_t d = (mux1 >> 15) & 0x7;
    return a == 4 || b == 4 || c == 4 || d == 4;
}

// 颜色输出源：C 为 0/COMBINED/零（G_CCMUX_0=31）时输出 ≈ D；否则混入 A/B/C。
// 未纹理的常见组合（G_CC_SHADE / G_CC_PRIMITIVE / G_CC_ENVIRONMENT）都是输出 = D。
CombineSource combineColorSource(uint32_t mux0, uint32_t mux1) {
    const uint32_t a = (mux0 >> 20) & 0xF;
    const uint32_t c = (mux0 >> 15) & 0x1F;
    const uint32_t b = (mux1 >> 28) & 0xF;
    const uint32_t d = (mux1 >> 15) & 0x7;
    if (a == 1 || a == 2 || b == 1 || b == 2 || c == 1 || c == 2 || d == 1 || d == 2) {
        return CombineSource::Texel0; // 具体哪个纹素不影响"有纹理"的判定
    }
    const CombineSource cs = decodeColorMux(c);
    if (c == 31 || cs == CombineSource::Combined || cs == CombineSource::Zero) {
        return decodeColorMux(d);
    }
    const CombineSource ds = decodeColorMux(d);
    if (ds != CombineSource::Combined && ds != CombineSource::Zero) {
        return ds;
    }
    return decodeColorMux(a);
}

// alpha 输出源（1-cycle：(A0-B0)*C0 + D0）。G_CC_SHADE 的 alpha D0 = SHADE_ALPHA。
CombineSource combineAlphaSource(uint32_t mux0, uint32_t mux1) {
    const uint32_t a0 = (mux0 >> 12) & 0x7;
    const uint32_t c0 = (mux0 >> 9) & 0x7;
    const uint32_t d0 = (mux1 >> 9) & 0x7;
    if (c0 == 0) {
        return decodeAlphaMux(d0);
    }
    const CombineSource ds = decodeAlphaMux(d0);
    if (ds != CombineSource::Combined) {
        return ds;
    }
    return decodeAlphaMux(a0);
}

Mesh &DLInterpreter::run(SegmentedAddress dl, bool reset_state, uint8_t layer) {
    return run(dl, reset_state, layer, mtxfIdentity(), Fast3D::identityMatrix(), {});
}

Mesh &DLInterpreter::run(SegmentedAddress dl, bool reset_state, uint8_t layer,
                         const Mtxf &initial_matrix,
                         const Fast3D::FixedMatrix &initial_fixed_matrix,
                         const ProjectionContext &projection_context) {
    dl_stack_.clear();
    mesh_ = {};
    finished = false;
    steps_ = 0;
    current_layer_ = layer;
    if (reset_state) {
        // 场景第一个 DL：RDP/RSP 复位。combine/prim/env/fog/tile 清 0（RDP 复位
        // 默认）；几何模式 = 游戏启动默认（game_init.c:120，依赖默认光照的对象
        // 据此判定 lit）；num_lights = 1（游戏持久 NUMLIGHTS_1，关卡 DL 不设置）。
        state_ = {};
        state_.num_lights = 1;
        state_.geometry_mode = kDefaultGeometryMode;
        state_.projection = mtxfIdentity();
        state_.fixed_projection = Fast3D::identityMatrix();
        state_.view_matrix = mtxfIdentity();
        state_.fixed_view_matrix = Fast3D::identityMatrix();
        state_.view_loaded = false;
        state_.viewport = {};
    }
    if (projection_context.valid) {
        state_.projection = projection_context.projection_matrix;
        state_.fixed_projection = projection_context.fixed_projection_matrix;
        state_.projection_loaded = true;
        state_.view_matrix = projection_context.view_matrix;
        state_.fixed_view_matrix = projection_context.fixed_view_matrix;
        state_.view_loaded = true;
        state_.viewport = projection_context.viewport;
        state_.persp_norm = projection_context.persp_norm;
    }
    // 模型视图矩阵栈初始为单位阵（不使用矩阵的 DL 输出原始顶点）。继续运行时
    // 保留上一个 DL 的渲染寄存器（combine/颜色/tile/几何模式/纹理绑定/灯光）——
    // 游戏只在分层渲染时改 render mode，其余状态跨顶层 DL 继承。
    state_.matrix_stack.clear();
    state_.matrix_stack.push_back(initial_matrix);
    state_.fixed_matrix_stack.clear();
    state_.fixed_matrix_stack.push_back(initial_fixed_matrix);
    material_ = {};

    SegmentedAddress pc = dl;
    while (!finished) {
        if (++steps_ > kMaxSteps) {
            printf("DLInterpreter: exceeded step limit\n");
            break;
        }

        DecodedCommand cmd;

        try {
            cmd = cmd_decoder_.decode(pc);
        } catch (const std::out_of_range &e) {
            printf("DLInterpreter: out_of_range at %02x:%06x (pc)\n", pc.seg, pc.offset);
            warnings_.add(
                "dl",
                "the display list pointer 0x" + [&]() {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%08X", (pc.seg << 24) | pc.offset);
                    return std::string(buf);
                }() +
                    " (segment " + std::to_string(pc.seg) + ") ran past the end of its "
                    "segment while decoding the next command; the rest of this display "
                    "list was skipped (" + std::string(e.what()) + ")");
            break;
        }

        try {
            execute(cmd, pc);
        } catch (const std::out_of_range &e) {
            printf("DLInterpreter: out_of_range at %02x:%06x\n", cmd.addr.seg, cmd.addr.offset);
            warnings_.add(
                "dl",
                "while executing the display list command at segment " +
                    std::to_string(cmd.addr.seg) + " offset 0x" + [&]() {
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), "%06X", cmd.addr.offset);
                        return std::string(buf);
                    }() +
                    " the decoder read past the end of its segment; the rest of this "
                    "display list was skipped (" + std::string(e.what()) + ")");
            break;
        }
        // G_ENDDL / G_DL 已设置 pc（弹栈返回 / 跳转），其他命令按 8 字节顺序前进
        if (cmd.opcode != G_ENDDL && cmd.opcode != G_DL) {
            pc += 8;
        }
    }
    return mesh_;
}

void DLInterpreter::end(SegmentedAddress &pc) {
    if (dl_stack_.empty()) {
        finished = true;
    } else {
        pc = dl_stack_.back();
        dl_stack_.pop_back();
    }
}

void DLInterpreter::branch(const DecodedCommand &cmd, SegmentedAddress &pc) {
    if (cmd.dlBranch()) {
        pc = cmd.dlAddress();
    } else {
        pc += 8;
        dl_stack_.push_back(pc);
        if (dl_stack_.size() > kMaxDLDepth) {
            printf("DLInterpreter: DL stack overflow\n");
            finished = true;
        } else {
            pc = cmd.dlAddress();
        }
    }
}

void DLInterpreter::execute(const DecodedCommand &cmd, SegmentedAddress &pc) {
    switch (cmd.opcode) {
    case G_ENDDL:
        end(pc);
        break;
    case G_DL:
        branch(cmd, pc);
        break;
    case G_VTX:
        loadVertices(cmd);
        break;
    case G_TRI1:
        drawTriangle(cmd);
        break;
    case G_MTX:
        handleMtx(cmd);
        break;
    case G_POPMTX:
        handlePopMtx(cmd);
        break;
    // --- 材质/渲染状态（写入 state_ 的持久 RDP 寄存器；Material 在 drawTriangle
    //     时从 state_ 快照）---
    case G_SETCOMBINE:
        state_.combine_w0 = cmd.combineMux0();
        state_.combine_w1 = cmd.combineMux1();
        break;
    case G_SETPRIMCOLOR:
        state_.prim_color[0] = cmd.colorR();
        state_.prim_color[1] = cmd.colorG();
        state_.prim_color[2] = cmd.colorB();
        state_.prim_color[3] = cmd.colorA();
        break;
    case G_SETENVCOLOR:
        state_.env_color[0] = cmd.colorR();
        state_.env_color[1] = cmd.colorG();
        state_.env_color[2] = cmd.colorB();
        state_.env_color[3] = cmd.colorA();
        break;
    case G_SETFOGCOLOR:
        state_.fog_color[0] = cmd.colorR();
        state_.fog_color[1] = cmd.colorG();
        state_.fog_color[2] = cmd.colorB();
        state_.fog_color[3] = cmd.colorA();
        break;
    case G_SETTILE:
        // 防御性绑定：记录该 tile 的 tmem 地址（LOAD 时绑定图像用）
        state_.tile_tmem[cmd.tileNum()] = cmd.tileTMEM();
        // RDP tile state is per tile. The load tile (usually tile 7) has its
        // own format/line/wrap setup and must not overwrite the render tile's
        // sampling state used by Material and TextureDecoder.
        if (cmd.tileNum() != kRenderTile) {
            break;
        }
        state_.tile_fmt = cmd.rdpFmt();
        state_.tile_siz = cmd.rdpSize();
        // G_SETTILE 的 S/T clamp/mirror 模式（0=WRAP 1=MIRROR 2=CLAMP）。
        // angrylion's calculate_tile_derivs uses cs || !mask, so G_TX_NOMASK
        // is a forced clamp even when the command says WRAP.
        state_.tex_clamp_s = cmd.tileClampS() == 2;
        state_.tex_clamp_t = cmd.tileClampT() == 2;
        state_.tex_mask_s = cmd.tileMaskS();
        state_.tex_mask_t = cmd.tileMaskT();
        // palette（CI4 调色板索引）与 line（TMEM 行跨度）——CI 纹理解码需要。
        state_.tex_palette = cmd.tilePalette();
        state_.tex_line = cmd.tileLine();
        break;
    case G_LOADBLOCK:
    case G_LOADTILE:
        // 防御性绑定：把当前 G_SETTEXIMAGE 图像绑定到该 tile 的 tmem 槽位
        if (state_.tex_image.seg >= 0) {
            const uint32_t img = (uint32_t(state_.tex_image.seg) << 24) |
                                 (state_.tex_image.offset & 0xFFFFFF);
            state_.tmem_images[state_.tile_tmem[cmd.tileNum()]] = img;
        }
        // G_LOADBLOCK 的 DXT（w1 低 12 位）编码了源图像每行的 64 位字数
        // （dxt = ceil(2^11/words)，见 gbi.h 的 CALC_DXT），纹理解码用它反推行宽；
        // G_LOADTILE 无 DXT，重置后解码退回图块宽。
        state_.tex_dxt = (cmd.opcode == G_LOADBLOCK) ? cmd.highT() : 0;
        break;
    case G_SETTILESIZE: {
        // w0: uls<<12 | ult ; w1: lrs<<12 | lrt；尺寸 = (lrs-uls)/4 + 1（RDP 单位 1/4 纹素）
        if (cmd.tileNum() != kRenderTile) {
            break;
        }
        state_.tex_sl = cmd.lowS();
        state_.tex_tl = cmd.lowT();
        state_.tex_sh = cmd.highS();
        state_.tex_th = cmd.highT();
        break;
    }
    case G_SETTEXIMAGE:
        // 记录到 RSP 状态（RDP 命令状态，不属于材质）。
        // w0 低 12 位 = 图像行宽字段（gbi.h 存 width-1，SM64 传 1 → 恒为 0，
        // 纹理解码改用 G_LOADBLOCK 的 DXT 反推行宽）。
        state_.tex_image.setAddress(cmd.w1);
        state_.tex_image_width = cmd.w0 & 0xFFF;
        break;
    case G_TEXTURE:
        // fast3d: G_TEXTURE 的 w0 位 0（F3D 的 on 位）切换 G_TEXTURE_ENABLE；
        // w1 = (S<<16)|T 是纹理坐标缩放（16.16 定点，0xFFFF≈1.0），
        // 由 dma_VTX 在载入顶点时应用（与开关位无关，G_OFF 也照样存储）。
        if (cmd.texOn()) {
            state_.geometry_mode |= G_TEXTURE_ENABLE;
        } else {
            state_.geometry_mode &= ~G_TEXTURE_ENABLE;
        }
        state_.tex_scale_s = cmd.texScaleS();
        state_.tex_scale_t = cmd.texScaleT();
        // 记录渲染 tile 与 mipmap 层级（SM64 用 tile 0；多 tile 渲染未模拟）。
        state_.texture_tile = cmd.texTile();
        state_.texture_lod = cmd.texLevel();
        break;
    case G_SETGEOMETRYMODE:
        handleGeometryMode(cmd, true);
        break;
    case G_CLEARGEOMETRYMODE:
        handleGeometryMode(cmd, false);
        break;
    case G_MOVEMEM:
        handleMovemem(cmd);
        break;
    case G_MOVEWORD:
        handleMoveword(cmd);
        break;
    case G_SETOTHERMODE_L:
    case G_SETOTHERMODE_H:
        handleOtherMode(cmd);
        break;
    case G_RDPSETOTHERMODE:
        handleRdpOtherMode(cmd);
        break;
    case G_LOADTLUT:
        handleLoadTLUT(cmd);
        break;
    // 已实现但不产生几何的命令：忽略
    // （fast3d 的 DMA 表里 0x02/0x05/0x07-0x09 也是 SP_NOOP）
    case 0x02:
    case 0x05:
    case 0x07:
    case 0x08:
    case 0x09:
    case G_CULLDL:
    case G_SPNOOP:
    case G_DPLOADSYNC:
    case G_DPPIPESYNC:
    case G_DPTILESYNC:
    case G_DPFULLSYNC:
    case G_SETFILLCOLOR:
    case G_SETBLENDCOLOR:
    case G_SETZIMG:
    case G_SETCIMG:
    case G_TEXRECT:
    case G_TEXRECTFLIP:
    case G_SETKEYGB:
    case G_SETKEYR:
    case G_SETCONVERT:
    case G_SETSCISSOR:
    case G_SETPRIMDEPTH:
    case G_FILLRECT:
        break;
    default:
        printf("DLInterpreter: unknown opcode 0x%02x at %02x:%06x\n", cmd.opcode, cmd.addr.seg,
               cmd.addr.offset);
        break;
    }
}

void DLInterpreter::handleMtx(const DecodedCommand &cmd) {
    const Fast3D::FixedMatrix fixed = cmd_decoder_.decodeFixedMtx(cmd.mtxAddress());
    Mtxf m = cmd_decoder_.decodeMtx(cmd.mtxAddress());
    uint8_t params = cmd.mtxParams();

    if (params & MTX_PROJECTION) {
        if (params & MTX_LOAD) {
            state_.projection = m;
            state_.fixed_projection = fixed;
        } else {
            state_.projection = mtxfMul(state_.projection, m);
            state_.fixed_projection = Fast3D::matrixMultiply(
                state_.fixed_projection, fixed);
        }
        state_.projection_loaded = true;
        return;
    }

    state_.transformed_lights_valid = false;

    // 模型视图矩阵栈
    if (params & MTX_PUSH) {
        // 压栈：复制当前栈顶（栈保证至少有一个单位阵）
        state_.matrix_stack.push_back(state_.matrix_stack.back());
        state_.fixed_matrix_stack.push_back(state_.fixed_matrix_stack.back());
    }

    Mtxf &top = state_.matrix_stack.back();
    Fast3D::FixedMatrix &fixed_top = state_.fixed_matrix_stack.back();
    if (params & MTX_LOAD) {
        top = m; // 载入：替换栈顶
        fixed_top = fixed; // 载入：替换栈顶
    } else {
        top = mtxfMul(top, m); // 乘：top = top × m（先应用 m）
        fixed_top = Fast3D::matrixMultiply(fixed_top, fixed);
    }
}

void DLInterpreter::handlePopMtx(const DecodedCommand &cmd) {
    // fast3d 的 imm_POPMTX 固定弹出 1 个矩阵（栈指针 -0x40），w1 被完全忽略
    // （SM64 的 gSPPopMatrix 传 G_MTX_MODELVIEW=0，见 rsp/fast3d.s imm_POPMTX）。
    if (state_.matrix_stack.size() > 1) {
        state_.transformed_lights_valid = false;
        state_.matrix_stack.pop_back(); // 保留栈底单位阵
        state_.fixed_matrix_stack.pop_back();
    }
}

void DLInterpreter::handleGeometryMode(const DecodedCommand &cmd, bool set) {
    const uint32_t bits = cmd.geometryMode();
    if (set) {
        state_.geometry_mode |= bits;
    } else {
        state_.geometry_mode &= ~bits;
    }
}

// 从 state_ 的持久 RDP 寄存器重建当前材质快照（drawTriangle 时调用）。
void DLInterpreter::snapshotMaterial() {
    material_ = {};
    material_.combine_w0 = state_.combine_w0;
    material_.combine_w1 = state_.combine_w1;
    std::copy(state_.prim_color, state_.prim_color + 4, material_.prim_color);
    std::copy(state_.env_color, state_.env_color + 4, material_.env_color);
    std::copy(state_.fog_color, state_.fog_color + 4, material_.fog_color);
    material_.tile_fmt = state_.tile_fmt;
    material_.tile_siz = state_.tile_siz;
    material_.tex_sl = state_.tex_sl;
    material_.tex_tl = state_.tex_tl;
    material_.tex_sh = state_.tex_sh;
    material_.tex_th = state_.tex_th;
    material_.tex_dxt = state_.tex_dxt;
    material_.tex_clamp_s = state_.tex_clamp_s;
    material_.tex_clamp_t = state_.tex_clamp_t;
    material_.tex_mask_s = state_.tex_mask_s;
    material_.tex_mask_t = state_.tex_mask_t;
    material_.tex_palette = state_.tex_palette;
    material_.tex_line = state_.tex_line;
    material_.lut_type = state_.lut_type;
    material_.lit = (state_.geometry_mode & G_LIGHTING) != 0;
    material_.cull_back = (state_.geometry_mode & G_CULL_BACK) != 0;
    // 灯光：快照当前 RSP 灯光槽（含环境光所在的槽 num_lights）。
    material_.lights.light = state_.lights;
    material_.lights.num_lights = state_.num_lights;
    material_.lights.loaded = state_.lights_loaded;
    // RDP 采样纹理由 G_SETCOMBINE 决定（用 TEXEL0/1 才取纹理）。G_TEXTURE_ENABLE
    // 由父 DL 设置、子 DL 常不再 G_ON，跨顶层 DL 持久后这里仍以 combine 为准。
    material_.combine_uses_texel = combineUsesTexel(state_.combine_w0, state_.combine_w1);
    material_.textured = material_.combine_uses_texel;
}

void DLInterpreter::handleMovemem(const DecodedCommand &cmd) {
    // gsSPLight(l, n) = gsDma1p(G_MOVEMEM, l, 16, (n-1)*2+G_MV_L0)：把 16 字节
    // Light_t 拷入灯光槽。G_MV_VIEWPORT/LOOKAT/MATRIX 等是运行时/未使用状态。
    const uint8_t index = cmd.memIndex();
    if (index == G_MV_VIEWPORT) {
        const std::span<const uint8_t> d = seg_table_.data(cmd.memAddress(), 16);
        if (d.size() < 16) {
            return;
        }
        for (size_t i = 0; i < 3; i++) {
            const int16_t scale = readInt<int16_t>(d, i * sizeof(int16_t));
            const int16_t translate = readInt<int16_t>(
                d, (i + 4) * sizeof(int16_t));
            state_.viewport.scale[i] = Fast3D::fixedFromInteger(scale) / 4;
            state_.viewport.translate[i] = Fast3D::fixedFromInteger(translate) / 4;
        }
        state_.viewport.valid = true;
        return;
    }
    if (index >= G_MV_L0 && index <= G_MV_L7) {
        const size_t slot = static_cast<size_t>((index - G_MV_L0) / 2);
        if (slot >= state_.lights.size()) {
            return;
        }
        // Light_t（gbi.h）：col[3], pad, colc[3], pad, dir[3], pad
        const std::span<const uint8_t> d = seg_table_.data(cmd.memAddress(), 16);
        if (d.size() < 16) {
            return;
        }
        state_.lights[slot].col[0] = d[0];
        state_.lights[slot].col[1] = d[1];
        state_.lights[slot].col[2] = d[2];
        state_.lights[slot].dir[0] = static_cast<int8_t>(d[8]);
        state_.lights[slot].dir[1] = static_cast<int8_t>(d[9]);
        state_.lights[slot].dir[2] = static_cast<int8_t>(d[10]);
        state_.lights_loaded = true;
        state_.transformed_lights_valid = false;
    }
}

void DLInterpreter::handleMoveword(const DecodedCommand &cmd) {
    // gsMoveWd(G_MW_*, offset, data)：w0 低 8 位 = G_MW_*，w1 = data。
    switch (cmd.mwIndex()) {
    case G_MW_NUMLIGHT:
        // bit31 = 重新初始化标志，低 12 位 = (numLights+1)*32（gbi.h gsSPSetNumLights）
        state_.num_lights = static_cast<uint8_t>(((cmd.mwValue() & 0xFFF) / 32) - 1);
        state_.transformed_lights_valid = false;
        break;
    case G_MW_FOG:
        // fog 系数：mult<<16 | offset（gsSPFogFactor）
        state_.fog_mult = static_cast<uint16_t>(cmd.mwValue() >> 16);
        state_.fog_offset = static_cast<uint16_t>(cmd.mwValue() & 0xFFFF);
        break;
    case G_MW_PERSPNORM:
        state_.persp_norm = static_cast<uint16_t>(cmd.mwValue());
        break;
    case G_MW_LIGHTCOL: {
        // 打补丁灯光颜色（gsSPLightColor）：dmem 偏移 (n-1)*24+4 选灯。
        const int16_t off = static_cast<int16_t>(cmd.mwOffset());
        const size_t slot = static_cast<size_t>((off - 4) / 24);
        if (slot < state_.lights.size()) {
            const uint32_t col = cmd.mwValue();
            state_.lights[slot].col[0] = static_cast<uint8_t>(col >> 24);
            state_.lights[slot].col[1] = static_cast<uint8_t>(col >> 16);
            state_.lights[slot].col[2] = static_cast<uint8_t>(col >> 8);
            state_.transformed_lights_valid = false;
        }
        break;
    }
    default:
        // G_MW_SEGMENT / G_MW_CLIP / G_MW_MATRIX：运行时或级别脚本层处理。
        break;
    }
}

void DLInterpreter::handleOtherMode(const DecodedCommand &cmd) {
    // fast3d/F3D：w0 = (op<<24)|(sft<<8)|(len)，w1 = data。
    const uint8_t sft = cmd.omSft();
    const uint8_t len = cmd.omLen();
    if (sft + len > 32) {
        return;
    }
    const uint32_t mask = (len >= 32) ? ~0u : ((1u << len) - 1);
    state_.othermode &= ~(mask << sft);
    state_.othermode |= (cmd.omData() & mask) << sft;
    updateOtherModeFields();
}

void DLInterpreter::handleRdpOtherMode(const DecodedCommand &cmd) {
    // gsDPSetOtherMode(mode0, mode1)：w0 低 24 位 = mode0（OTHERMODE 高 32 位
    // 字段，已按位域定位），w1 = mode1（RDP 渲染模式，暂不导出）。
    state_.othermode = cmd.w0 & 0x00FFFFFF;
    updateOtherModeFields();
}

void DLInterpreter::updateOtherModeFields() {
    // TEXTLUT 类型（CI 调色板格式）→ state_.lut_type；周期类型/纹理过滤等
    // 记录在 state_.othermode（渲染参数，导出暂不用）。
    state_.lut_type = static_cast<uint8_t>((state_.othermode >> G_MDSFT_TEXTLUT) & 0x3);
}

void DLInterpreter::handleLoadTLUT(const DecodedCommand &cmd) {
    // 从当前 G_SETTEXIMAGE 图像加载 count+1 个 16 位 TLUT 条目到 tile 的 tmem。
    // 记录 tmem → 调色板源图像（CI 纹理解码时查表）。
    if (state_.tex_image.seg >= 0) {
        const uint32_t img = (uint32_t(state_.tex_image.seg) << 24) |
                             (state_.tex_image.offset & 0xFFFFFF);
        const uint8_t tile = cmd.tlutTile();
        if (tile < state_.tile_tmem.size()) {
            state_.tlut_images[state_.tile_tmem[tile]] = img;
        }
    }
}

uint32_t DLInterpreter::materialId(uint32_t tex_image, uint32_t tlut) {
    for (size_t i = 0; i < mesh_.materials.size(); i++) {
        if (mesh_.materials[i] == material_ && mesh_.material_images[i] == tex_image
            && mesh_.material_tlut[i] == tlut) {
            return static_cast<uint32_t>(i);
        }
    }
    mesh_.materials.push_back(material_);
    mesh_.material_images.push_back(tex_image);
    mesh_.material_tlut.push_back(tlut);
    return static_cast<uint32_t>(mesh_.materials.size() - 1);
}

void DLInterpreter::loadVertices(const DecodedCommand &cmd) {
    std::span<const uint8_t> data = seg_table_.data(cmd.vtxAddress(), cmd.vtxBytes());
    uint8_t n = cmd.vtxCount();
    uint8_t dest = cmd.vtxDestIndex();
    for (uint8_t i = 0; i < n && dest + i < kVertexBufferSize; i++) {
        size_t o = size_t(i) * 16;
        if (o + 16 > data.size()) {
            break;
        }
        ProcessedVertex &v = state_.vertices[dest + i];
        v = {};
        v.source.position[0] = readInt<int16_t>(data, o + 0);
        v.source.position[1] = readInt<int16_t>(data, o + 2);
        v.source.position[2] = readInt<int16_t>(data, o + 4);
        v.source.flag = readInt<uint16_t>(data, o + 6);
        v.source.texture_coordinate[0] = readInt<int16_t>(data, o + 8);
        v.source.texture_coordinate[1] = readInt<int16_t>(data, o + 10);
        v.source.coordinate_or_normal[0] = data[o + 12];
        v.source.coordinate_or_normal[1] = data[o + 13];
        v.source.coordinate_or_normal[2] = data[o + 14];
        v.source.coordinate_or_normal[3] = data[o + 15];
        processVertex(v);
    }
}

void DLInterpreter::processVertex(ProcessedVertex &dst) {
    const auto &matrix = state_.fixed_matrix_stack.back();
    const Fast3D::FixedVector3 position {
        Fast3D::fixedFromInteger(dst.source.position[0]),
        Fast3D::fixedFromInteger(dst.source.position[1]),
        Fast3D::fixedFromInteger(dst.source.position[2]),
    };
    dst.position = Fast3D::transformPoint(matrix, position);
    if (state_.projection_loaded) {
        // fast3d.s builds the combined model-view/projection matrix before
        // G_VTX (f3d_04001484 and load_mp_matrix). Multiplying the matrices
        // first preserves the RSP's accumulator/saturation points instead of
        // rounding after each separate vector transform.
        const Fast3D::FixedMatrix model_view = state_.view_loaded
            ? Fast3D::matrixMultiply(matrix, state_.fixed_view_matrix)
            : matrix;
        const Fast3D::FixedMatrix model_view_projection = Fast3D::matrixMultiply(
            model_view, state_.fixed_projection);
        dst.clip_position = Fast3D::vectorMatrixMultiply(
            Fast3D::makeVector4(position[0], position[1], position[2]),
            model_view_projection);
        const Fast3D::Fixed x = dst.clip_position[0];
        const Fast3D::Fixed y = dst.clip_position[1];
        const Fast3D::Fixed z = dst.clip_position[2];
        const Fast3D::Fixed w = dst.clip_position[3];
        dst.clip_code = 0;
        if (x < -w) dst.clip_code |= 1 << 0;
        if (x > w) dst.clip_code |= 1 << 1;
        if (y < -w) dst.clip_code |= 1 << 2;
        if (y > w) dst.clip_code |= 1 << 3;
        if (z < -w) dst.clip_code |= 1 << 4;
        if (z > w) dst.clip_code |= 1 << 5;
        // Fast3D scales the clip coordinates by the Q0.16 perspNorm value
        // before the VRCP overlay and removes that scale after the divide
        // (fast3d.s:816-835). Keep the two fixed-point products separate: the
        // RSP rounds each VMAD result, so combining them changes edge values.
        const uint16_t persp_norm = state_.persp_norm;
        const Fast3D::Fixed normalized_w = Fast3D::fixedMultiplyScalar(w, persp_norm);
        const Fast3D::Fixed normalized_inverse_w = Fast3D::fixedMultiply(
            Fast3D::rspReciprocal(normalized_w), Fast3D::fixedFromInteger(2));
        const Fast3D::Fixed inverse_w = Fast3D::fixedMultiplyScalar(
            normalized_inverse_w, persp_norm);
        dst.inverse_w = inverse_w;
        if (w != 0) {
            dst.ndc_position = Fast3D::makeVector3(
                Fast3D::fixedMultiplyScalar(
                    Fast3D::fixedMultiply(x, normalized_inverse_w), persp_norm),
                Fast3D::fixedMultiplyScalar(
                    Fast3D::fixedMultiply(y, normalized_inverse_w), persp_norm),
                Fast3D::fixedMultiplyScalar(
                    Fast3D::fixedMultiply(z, normalized_inverse_w), persp_norm));
            if (state_.viewport.valid) {
                for (size_t i = 0; i < 3; i++) {
                    dst.viewport_position[i] = state_.viewport.translate[i]
                        + Fast3D::fixedMultiply(dst.ndc_position[i],
                                                state_.viewport.scale[i]);
                }
            }
        }
        dst.projected = true;
    }

    const Fast3D::FixedVector3 normal {
        static_cast<Fast3D::Fixed>(static_cast<int32_t>(
            static_cast<int8_t>(dst.source.coordinate_or_normal[0])) * 512),
        static_cast<Fast3D::Fixed>(static_cast<int32_t>(
            static_cast<int8_t>(dst.source.coordinate_or_normal[1])) * 512),
        static_cast<Fast3D::Fixed>(static_cast<int32_t>(
            static_cast<int8_t>(dst.source.coordinate_or_normal[2])) * 512),
    };
    dst.normal = Fast3D::transformDirection(matrix, normal);
    dst.texture_coordinate = {
        scaleTextureCoordinate(dst.source.texture_coordinate[0], state_.tex_scale_s),
        scaleTextureCoordinate(dst.source.texture_coordinate[1], state_.tex_scale_t),
    };
    dst.lighting = (state_.geometry_mode & G_LIGHTING) != 0;
    if (dst.lighting) {
        if (!state_.transformed_lights_valid) {
            updateTransformedLights();
        }
        const Fast3D::FixedVector3 local_normal {
            static_cast<Fast3D::Fixed>(static_cast<int32_t>(
                static_cast<int8_t>(dst.source.coordinate_or_normal[0])) * 512),
            static_cast<Fast3D::Fixed>(static_cast<int32_t>(
                static_cast<int8_t>(dst.source.coordinate_or_normal[1])) * 512),
            static_cast<Fast3D::Fixed>(static_cast<int32_t>(
                static_cast<int8_t>(dst.source.coordinate_or_normal[2])) * 512),
        };
        std::array<Fast3D::Light, 8> lights {};
        for (size_t i = 0; i < lights.size(); i++) {
            lights[i].color = {
                state_.lights[i].col[0], state_.lights[i].col[1], state_.lights[i].col[2],
            };
            lights[i].direction = {
                state_.lights[i].dir[0], state_.lights[i].dir[1], state_.lights[i].dir[2],
            };
        }
        dst.shade = Fast3D::shadeVertex(local_normal, state_.transformed_lights,
                                        lights, state_.num_lights, state_.lights_loaded,
                                        dst.source.coordinate_or_normal[3]);
    } else {
        dst.shade = {
            dst.source.coordinate_or_normal[0], dst.source.coordinate_or_normal[1],
            dst.source.coordinate_or_normal[2], dst.source.coordinate_or_normal[3],
        };
    }
    dst.valid = true;
}

void DLInterpreter::updateTransformedLights() {
    const auto &matrix = state_.fixed_matrix_stack.back();
    for (size_t i = 0; i < state_.transformed_lights.size(); i++) {
        Fast3D::Light light;
        light.color = {
            state_.lights[i].col[0], state_.lights[i].col[1], state_.lights[i].col[2],
        };
        light.direction = {
            state_.lights[i].dir[0], state_.lights[i].dir[1], state_.lights[i].dir[2],
        };
        state_.transformed_lights[i] = Fast3D::normalizeVector(
            Fast3D::transformDirection(matrix, Fast3D::lightDirectionQ16(light)));
    }
    state_.transformed_lights_valid = true;
}

void DLInterpreter::drawTriangle(const DecodedCommand &cmd) {
    // 本三角形按 draw 时刻的 RDP 状态归组（combine/颜色/tile/几何模式）。
    snapshotMaterial();
    uint8_t v0 = cmd.triV0();
    uint8_t v1 = cmd.triV1();
    uint8_t v2 = cmd.triV2();
    uint8_t flag = cmd.triFlag();
    // flag 旋转（gbi.h 约定）：0→(v0,v1,v2) 1→(v1,v2,v0) 2→(v2,v0,v1)
    if (flag == 1) {
        uint8_t t = v0;
        v0 = v1;
        v1 = v2;
        v2 = t;
    } else if (flag == 2) {
        uint8_t t = v0;
        v0 = v2;
        v2 = v1;
        v1 = t;
    }
    if (v0 >= kVertexBufferSize || v1 >= kVertexBufferSize || v2 >= kVertexBufferSize) {
        printf("DLInterpreter: triangle vertex index out of range (%u,%u,%u)\n", v0, v1, v2);
        return;
    }
    // 解析本三角形的纹理源：渲染 tile（fast3d 固定 0）对应 tmem 槽位绑定的图像；
    // 该 tmem 未加载过（=0）时回退到最近 G_SETTEXIMAGE（当前行为）。
    // 图像是材质去重键的一部分，但属于 drawTriangle 时解析出的 RSP 状态，
    // 因此记录在 Mesh.material_images（与 materials 并行），而不是 Material 里。
    uint32_t tex_image = state_.tmem_images[state_.tile_tmem[kRenderTile]];
    if (tex_image == 0 && state_.tex_image.seg >= 0) {
        tex_image = (uint32_t(state_.tex_image.seg) << 24) |
                    (state_.tex_image.offset & 0xFFFFFF);
    }
    // CI 调色板源（G_LOADTLUT 绑定的 tmem → 调色板图像；0 = 无）。
    const uint32_t tlut = state_.tlut_images[state_.tile_tmem[kRenderTile]];
    uint32_t base = static_cast<uint32_t>(mesh_.vertices.size());
    appendVertex(state_.vertices[v0]);
    appendVertex(state_.vertices[v1]);
    appendVertex(state_.vertices[v2]);
    mesh_.indices.push_back(base);
    mesh_.indices.push_back(base + 1);
    mesh_.indices.push_back(base + 2);
    mesh_.material_ids.push_back(materialId(tex_image, tlut));
    mesh_.triangle_layers.push_back(current_layer_);
}

void DLInterpreter::appendVertex(const ProcessedVertex &v) {
    MeshVertex mv;
    mv.position[0] = fixedToFloat(v.position[0]);
    mv.position[1] = fixedToFloat(v.position[1]);
    mv.position[2] = fixedToFloat(v.position[2]);
    if (v.projected) {
        for (size_t i = 0; i < 4; i++) {
            mv.clip_position[i] = fixedToFloat(v.clip_position[i]);
        }
        for (size_t i = 0; i < 3; i++) {
            mv.ndc_position[i] = fixedToFloat(v.ndc_position[i]);
            mv.viewport_position[i] = fixedToFloat(v.viewport_position[i]);
        }
        mv.inverse_w = fixedToFloat(v.inverse_w);
        mv.clip_code = v.clip_code;
        mv.projected = true;
    }
    const float nx = fixedToFloat(v.normal[0]);
    const float ny = fixedToFloat(v.normal[1]);
    const float nz = fixedToFloat(v.normal[2]);
    const float normal_length = std::sqrt(nx * nx + ny * ny + nz * nz);
    mv.normal[0] = normal_length > 0.000001f ? nx / normal_length : 0.0f;
    mv.normal[1] = normal_length > 0.000001f ? ny / normal_length : 0.0f;
    mv.normal[2] = normal_length > 0.000001f ? nz / normal_length : 0.0f;
    // Keep the existing normalized texture convention, but use the scaled
    // coordinate cached at G_VTX rather than the later RSP state.
    mv.uv[0] = v.texture_coordinate[0] / 32.0f;
    mv.uv[1] = v.texture_coordinate[1] / 32.0f;
    if (material_.textured && material_.tex_width() > 0 && material_.tex_height() > 0) {
        // angrylion samples RDP texel coordinates in [i, i+1), while Godot
        // filters around texel centers. Subtract the SETTILESIZE origin and
        // add half a texel so both samplers use the same interpolation point.
        const float s = mv.uv[0] - material_.tex_sl / 4.0f + 0.5f;
        const float t = mv.uv[1] - material_.tex_tl / 4.0f + 0.5f;
        mv.uv[0] = s / material_.tex_width();
        mv.uv[1] = t / material_.tex_height();
    }
    // 注意：不翻转 v 轴。Godot 的 ArrayMesh ARRAY_TEX_UV 用 v=0 为顶部（与
    // 图像第 0 行一致），N64 的 t=0 也是顶部，直接映射即可；翻转会导致所有
    // 纹理上下颠倒（树木最明显，见 docs/engine-notes.md）。
    std::copy(v.source.coordinate_or_normal,
              v.source.coordinate_or_normal + 4, mv.source_color);
    std::copy(v.shade.begin(), v.shade.end(), mv.shade);
    mv.shade_valid = v.valid;
    std::copy(v.shade.begin(), v.shade.end(), mv.color);
    mesh_.vertices.push_back(mv);
}

} // namespace GBI
