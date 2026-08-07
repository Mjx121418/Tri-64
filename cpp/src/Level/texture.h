#ifndef TEXTURE_H
#define TEXTURE_H

#include "Level/dl_interpreter.h"
#include "Memory/segment.h"
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace GBI {

// 解码后的纹理：RGBA8 像素（每纹素 4 字节，可直接用于 Godot 纹理）
struct Texture {
    uint16_t width {0};
    uint16_t height {0};
    // 源格式/位深（G_SETTILE 的 fmt/siz）元数据。解码像素后不再被读取，
    // 保留用于诊断与未来的格式转换。
    uint8_t fmt {0};
    uint8_t siz {0};
    std::vector<uint8_t> pixels; // width*height*4, R,G,B,A
};

// 从材质解码纹理像素的解码器。
//
// tex_image = 三角形实际采样的图像（Mesh.material_images 里解析出的段地址）。
// tlut_image = CI 纹理的调色板源图像（Mesh.material_tlut，G_LOADTLUT 绑定的段
// 地址；非 CI 纹理为 0）。数据源 = 该图像的 DRAM 数据（行主序），区域 =
// G_SETTILESIZE 的 4 坐标（1/4 纹素），格式 = G_SETTILE。不需要模拟 RDP 的
// TMEM 拷贝：对线性布局而言 "DRAM → TMEM" 的结果就是直接读 DRAM。
//
// 支持：RGBA16/RGBA32（fmt 0），CI4/CI8（fmt 2，需 TLUT + OTHERMODE 的
// TEXTLUT 类型），IA16/IA8/IA4（fmt 3），I8/I4（fmt 4）。
class TextureDecoder {
    const SegmentTable &seg_table_;
    Texture texture_;
    std::string error_;

public:
    explicit TextureDecoder(const SegmentTable &seg_table) : seg_table_(seg_table) {}

    // 解码 material 引用的 tex_image 纹理（CI 纹理用 tlut_image 的调色板）。
    // 成功返回 true，texture() 可用；失败返回 false，error() 给出原因。
    // 每次 run 重置内部结果。
    bool run(const Material &material, SegmentedAddress tex_image, uint32_t tlut_image = 0);

    bool ok() const { return error_.empty(); }
    const Texture &texture() const { return texture_; }
    const std::string &error() const { return error_; }
};

} // namespace GBI

#endif /* TEXTURE_H */
