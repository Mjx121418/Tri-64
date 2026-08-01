#include "Level/dl_interpreter.h"
#include "Math/math.h"
#include <cstdio>

namespace GBI {

namespace {
constexpr uint64_t kMaxSteps = 10'000'000;
constexpr size_t kMaxDLDepth = 32;
} // namespace

Mesh &DLInterpreter::run(SegmentedAddress dl) {
    dl_stack_.clear();
    // 模型视图矩阵栈初始为单位阵（不使用矩阵的 DL 输出原始顶点）
    state_.matrix_stack.clear();
    state_.matrix_stack.push_back(mtxfIdentity());
    SegmentedAddress pc = dl;
    while (true) {
        if (++steps_ > kMaxSteps) {
            printf("DLInterpreter: exceeded step limit\n");
            break;
        }
        DecodedCommand cmd;
        try {
            cmd = decodeDLCommand(pc, seg_table_);
        } catch (const std::out_of_range &e) {
            printf("DLInterpreter: out_of_range at %02x:%06x (pc)\n", pc.seg, pc.offset);
            return mesh_;
        }

        switch (cmd.opcode) {
        case G_ENDDL:
            if (dl_stack_.empty()) {
                return mesh_;
            }
            pc = dl_stack_.back();
            dl_stack_.pop_back();
            continue;
        case G_DL:
            if (cmd.dlBranch()) {
                pc = cmd.dlAddress();
            } else {
                pc += 8;
                dl_stack_.push_back(pc);
                if (dl_stack_.size() > kMaxDLDepth) {
                    printf("DLInterpreter: DL stack overflow\n");
                    return mesh_;
                }
                pc = cmd.dlAddress();
            }
            continue;
        default:
            try {
                execute(cmd, pc);
            } catch (const std::out_of_range &e) {
                printf("DLInterpreter: out_of_range at %02x:%06x\n", cmd.addr.seg, cmd.addr.offset);
                return mesh_;
            }
            pc += 8;
        }
    }
    return mesh_;
}

void DLInterpreter::execute(const DecodedCommand &cmd, SegmentedAddress &pc) {
    switch (cmd.opcode) {
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
    // 已实现但不产生几何的命令：忽略
    // （fast3d 的 DMA 表里 0x02/0x05/0x07-0x09 也是 SP_NOOP）
    case 0x02:
    case 0x05:
    case 0x07:
    case 0x08:
    case 0x09:
    case G_MOVEMEM:
    case G_TEXTURE:
    case G_MOVEWORD:
    case G_SETGEOMETRYMODE:
    case G_CLEARGEOMETRYMODE:
    case G_SETOTHERMODE_L:
    case G_SETOTHERMODE_H:
    case G_CULLDL:
    case G_SPNOOP:
    case G_DPLOADSYNC:
    case G_DPPIPESYNC:
    case G_DPTILESYNC:
    case G_DPFULLSYNC:
    case G_SETTILESIZE:
    case G_LOADBLOCK:
    case G_LOADTILE:
    case G_SETTILE:
    case G_SETFILLCOLOR:
    case G_SETFOGCOLOR:
    case G_SETBLENDCOLOR:
    case G_SETPRIMCOLOR:
    case G_SETENVCOLOR:
    case G_SETCOMBINE:
    case G_SETTEXIMAGE:
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
    uint8_t n = cmd.popCount();
    while (n-- > 0 && state_.matrix_stack.size() > 1) {
        state_.matrix_stack.pop_back(); // 保留栈底单位阵
    }
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
    uint32_t base = static_cast<uint32_t>(mesh_.vertices.size());
    appendVertex(state_.vertices[v0]);
    appendVertex(state_.vertices[v1]);
    appendVertex(state_.vertices[v2]);
    mesh_.indices.push_back(base);
    mesh_.indices.push_back(base + 1);
    mesh_.indices.push_back(base + 2);
    mesh_.material_ids.push_back(0);
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
    // 纹理坐标 5.11 定点 → /32
    mv.uv[0] = v.texture_coordinate[0] / 32.0f;
    mv.uv[1] = v.texture_coordinate[1] / 32.0f;
    mv.color[0] = v.coordinate_or_normal[0];
    mv.color[1] = v.coordinate_or_normal[1];
    mv.color[2] = v.coordinate_or_normal[2];
    mv.color[3] = v.coordinate_or_normal[3];
    mesh_.vertices.push_back(mv);
}

} // namespace GBI
