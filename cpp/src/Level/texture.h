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
    uint8_t fmt {0};  // 源格式（G_SETTILE）
    uint8_t siz {0};  // 源位深（G_SETTILE）
    std::vector<uint8_t> pixels; // width*height*4, R,G,B,A
};

// 从材质解码纹理像素。
//
// tex_image = 三角形实际采样的图像（Mesh.material_images 里解析出的段地址）。
// 数据源 = 该图像的 DRAM 数据（行主序），区域 = G_SETTILESIZE 的 4 坐标
// （1/4 纹素），格式 = G_SETTILE。不需要模拟 RDP 的 TMEM 拷贝：对线性布局
// 而言 "DRAM → TMEM" 的结果就是直接读 DRAM。
//
// 当前支持 RGBA16（fmt=0, siz=2）与 IA16（fmt=3, siz=2）。
std::expected<Texture, std::string> decodeTexture(const Material &material,
                                                  SegmentedAddress tex_image,
                                                  const SegmentTable &seg_table);

} // namespace GBI

#endif /* TEXTURE_H */
