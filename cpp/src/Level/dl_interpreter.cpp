#include "Level/dl_interpreter.h"
#include "Math/math.h"
#include <cstdio>

namespace GBI {

namespace {
constexpr uint64_t kMaxSteps = 10'000'000;
constexpr size_t kMaxDLDepth = 32;

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
        (mux1 >> 17) & 0x7,  // 颜色 D
        (mux1 >> 14) & 0x7,  // alpha B
        (mux1 >> 11) & 0x7,  // alpha D
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

} // namespace

Mesh &DLInterpreter::run(SegmentedAddress dl) {
    dl_stack_.clear();
    // 模型视图矩阵栈初始为单位阵（不使用矩阵的 DL 输出原始顶点）
    state_.matrix_stack.clear();
    state_.matrix_stack.push_back(mtxfIdentity());
    finished = false;
    steps_ = 0;
    // 游戏启动的默认几何模式（game_init.c:120），依赖默认光照的对象据此判定 lit。
    geometry_mode_ = kDefaultGeometryMode;
    material_ = {};
    material_.lit = (geometry_mode_ & G_LIGHTING) != 0;
    state_.tile_tmem = {};
    state_.tmem_images = {};
    state_.tex_scale_s = 0;
    state_.tex_scale_t = 0;
    state_.tex_image = {};
    state_.tex_image_width = 0;

    SegmentedAddress pc = dl;
    while (!finished) {
        if (++steps_ > kMaxSteps) {
            printf("DLInterpreter: exceeded step limit\n");
            break;
        }

        DecodedCommand cmd;

        try {
            cmd = decodeDLCommand(pc, seg_table_);
        } catch (const std::out_of_range &e) {
            printf("DLInterpreter: out_of_range at %02x:%06x (pc)\n", pc.seg, pc.offset);
            break;
        }

        try {
            execute(cmd, pc);
        } catch (const std::out_of_range &e) {
            printf("DLInterpreter: out_of_range at %02x:%06x\n", cmd.addr.seg, cmd.addr.offset);
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
    // --- 材质/渲染状态 ---
    case G_SETCOMBINE:
        material_.combine_w0 = cmd.combineMux0();
        material_.combine_w1 = cmd.combineMux1();
        material_.combine_uses_texel = combineUsesTexel(material_.combine_w0, material_.combine_w1);
        updateTextured();
        break;
    case G_SETPRIMCOLOR:
        material_.prim_color[0] = cmd.colorR();
        material_.prim_color[1] = cmd.colorG();
        material_.prim_color[2] = cmd.colorB();
        material_.prim_color[3] = cmd.colorA();
        break;
    case G_SETENVCOLOR:
        material_.env_color[0] = cmd.colorR();
        material_.env_color[1] = cmd.colorG();
        material_.env_color[2] = cmd.colorB();
        material_.env_color[3] = cmd.colorA();
        break;
    case G_SETFOGCOLOR:
        material_.fog_color[0] = cmd.colorR();
        material_.fog_color[1] = cmd.colorG();
        material_.fog_color[2] = cmd.colorB();
        material_.fog_color[3] = cmd.colorA();
        break;
    case G_SETTILE:
        material_.tile_fmt = cmd.rdpFmt();
        material_.tile_siz = cmd.rdpSize();
        // G_SETTILE 的 S/T clamp/mirror 模式（0=WRAP 1=MIRROR 2=CLAMP）。
        // 导出到 Godot 的 texture_repeat：CLAMP 关闭重复，否则开启。
        material_.tex_clamp_s = cmd.tileClampS() == 2;
        material_.tex_clamp_t = cmd.tileClampT() == 2;
        // 防御性绑定：记录该 tile 的 tmem 地址（LOAD 时绑定图像用）
        state_.tile_tmem[cmd.tileNum()] = cmd.tileTMEM();
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
        material_.tex_dxt = (cmd.opcode == G_LOADBLOCK) ? cmd.highT() : 0;
        break;
    case G_SETTILESIZE: {
        // w0: uls<<12 | ult ; w1: lrs<<12 | lrt；尺寸 = (lrs-uls)/4 + 1（RDP 单位 1/4 纹素）
        material_.tex_sl = cmd.lowS();
        material_.tex_tl = cmd.lowT();
        material_.tex_sh = cmd.highS();
        material_.tex_th = cmd.highT();
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
        // fast3d: G_TEXTURE 的 w0 位 0 切换几何模式的 G_TEXTURE_ENABLE；
        // w1 = (S<<16)|T 是纹理坐标缩放（16.16 定点，0xFFFF≈1.0），
        // 由 dma_VTX 在载入顶点时应用（与开关位无关，G_OFF 也照样存储）。
        if (cmd.w0 & 1) {
            geometry_mode_ |= G_TEXTURE_ENABLE;
        } else {
            geometry_mode_ &= ~G_TEXTURE_ENABLE;
        }
        state_.tex_scale_s = (cmd.w1 >> 16) & 0xFFFF;
        state_.tex_scale_t = cmd.w1 & 0xFFFF;
        updateTextured();
        break;
    case G_SETGEOMETRYMODE:
        handleGeometryMode(cmd, true);
        break;
    case G_CLEARGEOMETRYMODE:
        handleGeometryMode(cmd, false);
        break;
    // 已实现但不产生几何的命令：忽略
    // （fast3d 的 DMA 表里 0x02/0x05/0x07-0x09 也是 SP_NOOP）
    case 0x02:
    case 0x05:
    case 0x07:
    case 0x08:
    case 0x09:
    case G_MOVEMEM:
    case G_MOVEWORD:
    case G_SETOTHERMODE_L:
    case G_SETOTHERMODE_H:
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
        break;
    default:
        printf("DLInterpreter: unknown opcode 0x%02x at %02x:%06x\n", cmd.opcode, cmd.addr.seg,
               cmd.addr.offset);
        break;
    }
}

void DLInterpreter::handleMtx(const DecodedCommand &cmd) {
    Mtxf m = decodeMtx(cmd.mtxAddress(), seg_table_);
    uint8_t params = cmd.mtxParams();

    if (params & MTX_PROJECTION) {
        state_.projection = m; // 投影矩阵只记录，网格输出不应用（Godot 自行投影）
        return;
    }

    // 模型视图矩阵栈
    if (params & MTX_PUSH) {
        // 压栈：复制当前栈顶（栈保证至少有一个单位阵）
        state_.matrix_stack.push_back(state_.matrix_stack.back());
    }

    Mtxf &top = state_.matrix_stack.back();
    if (params & MTX_LOAD) {
        top = m; // 载入：替换栈顶
    } else {
        top = mtxfMul(top, m); // 乘：top = top × m（先应用 m）
    }
}

void DLInterpreter::handlePopMtx(const DecodedCommand &cmd) {
    // fast3d 的 imm_POPMTX 固定弹出 1 个矩阵（栈指针 -0x40），w1 被完全忽略
    // （SM64 的 gSPPopMatrix 传 G_MTX_MODELVIEW=0，见 rsp/fast3d.s imm_POPMTX）。
    if (state_.matrix_stack.size() > 1) {
        state_.matrix_stack.pop_back(); // 保留栈底单位阵
    }
}

void DLInterpreter::handleGeometryMode(const DecodedCommand &cmd, bool set) {
    const uint32_t bits = cmd.geometryMode();
    if (set) {
        geometry_mode_ |= bits;
    } else {
        geometry_mode_ &= ~bits;
    }
    updateTextured();
}

void DLInterpreter::updateTextured() {
    material_.lit = (geometry_mode_ & G_LIGHTING) != 0;
    material_.textured =
        (geometry_mode_ & G_TEXTURE_ENABLE) != 0 && material_.combine_uses_texel;
}

uint32_t DLInterpreter::materialId(uint32_t tex_image) {
    for (size_t i = 0; i < mesh_.materials.size(); i++) {
        if (mesh_.materials[i] == material_ && mesh_.material_images[i] == tex_image) {
            return static_cast<uint32_t>(i);
        }
    }
    mesh_.materials.push_back(material_);
    mesh_.material_images.push_back(tex_image);
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
        Vtx &v = state_.vertices[dest + i];
        v.position[0] = readInt<int16_t>(data, o + 0);
        v.position[1] = readInt<int16_t>(data, o + 2);
        v.position[2] = readInt<int16_t>(data, o + 4);
        v.flag = readInt<uint16_t>(data, o + 6);
        v.texture_coordinate[0] = readInt<int16_t>(data, o + 8);
        v.texture_coordinate[1] = readInt<int16_t>(data, o + 10);
        v.coordinate_or_normal[0] = data[o + 12];
        v.coordinate_or_normal[1] = data[o + 13];
        v.coordinate_or_normal[2] = data[o + 14];
        v.coordinate_or_normal[3] = data[o + 15];
    }
}

void DLInterpreter::drawTriangle(const DecodedCommand &cmd) {
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
    uint32_t base = static_cast<uint32_t>(mesh_.vertices.size());
    appendVertex(state_.vertices[v0]);
    appendVertex(state_.vertices[v1]);
    appendVertex(state_.vertices[v2]);
    mesh_.indices.push_back(base);
    mesh_.indices.push_back(base + 1);
    mesh_.indices.push_back(base + 2);
    mesh_.material_ids.push_back(materialId(tex_image));
}

void DLInterpreter::appendVertex(const Vtx &v) {
    MeshVertex mv;
    // 应用当前模型视图矩阵（平移在 m[3][0..2]，与 decomp 的 mtxf_mul_vec3s 一致）
    const Mtxf &m = state_.matrix_stack.back();
    const float x = v.position[0];
    const float y = v.position[1];
    const float z = v.position[2];
    mv.position[0] = m[0][0] * x + m[1][0] * y + m[2][0] * z + m[3][0];
    mv.position[1] = m[0][1] * x + m[1][1] * y + m[2][1] * z + m[3][1];
    mv.position[2] = m[0][2] * x + m[1][2] * y + m[2][2] * z + m[3][2];
    // coordinate_or_normal 为有符号法线（-128..127）
    mv.normal[0] = static_cast<int8_t>(v.coordinate_or_normal[0]) / 127.0f;
    mv.normal[1] = static_cast<int8_t>(v.coordinate_or_normal[1]) / 127.0f;
    mv.normal[2] = static_cast<int8_t>(v.coordinate_or_normal[2]) / 127.0f;
    // 纹理坐标：原始 16 位 → 纹素（16-bit 纹理每纹素 32 个原始单位，8-bit 为 16；
    // 见 movtex_make_quad_vertex：scale=1（平铺 1 次）四边形 = 992 原始单位 =
    // 31 纹素），再乘 G_TEXTURE 的 S/T 缩放（RSP 在 dma_VTX 里执行
    // raw×scale>>16，见 fast3d.s 的 vmudm）→ 按纹理尺寸归一化（1 次平铺 = 1.0）
    mv.uv[0] = v.texture_coordinate[0] / 32.0f * (state_.tex_scale_s / 65536.0f);
    mv.uv[1] = v.texture_coordinate[1] / 32.0f * (state_.tex_scale_t / 65536.0f);
    if (material_.textured && material_.tex_width() > 0 && material_.tex_height() > 0) {
        mv.uv[0] /= material_.tex_width();
        mv.uv[1] /= material_.tex_height();
    }
    // 注意：不翻转 v 轴。Godot 的 ArrayMesh ARRAY_TEX_UV 用 v=0 为顶部（与
    // 图像第 0 行一致），N64 的 t=0 也是顶部，直接映射即可；翻转会导致所有
    // 纹理上下颠倒（树木最明显，见 docs/engine-notes.md）。
    mv.color[0] = v.coordinate_or_normal[0];
    mv.color[1] = v.coordinate_or_normal[1];
    mv.color[2] = v.coordinate_or_normal[2];
    mv.color[3] = v.coordinate_or_normal[3];
    mesh_.vertices.push_back(mv);
}

} // namespace GBI
