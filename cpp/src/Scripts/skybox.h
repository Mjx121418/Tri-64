#ifndef SKYBOX_H
#define SKYBOX_H

#include "Memory/segment.h"
#include <cstdint>
#include <string>
#include <vector>

// 天空盒/背景（镜像 decomp 的 skybox.c + geo_layout_cmd_node_background）。
namespace Skybox {

// 每区域的背景信息（geo 命令 0x19 GEO_BACKGROUND 节点）：背景 id（0-9，
// func 非空 = 天空盒贴图）或 RGBA5551 填充色（func 为空，geo_process_background
// 画纯色填充）。func 是背景生成函数（段地址，不执行；vanilla 只有
// geo_skybox_main）。
struct Background {
    int16_t background {0}; // GEO_BACKGROUND 的原始 s16（id 或 RGBA5551 颜色）
    uint32_t func {0};      // 背景生成函数（段地址；0 = 纯色填充）

    bool is_skybox() const { return func != 0; }
};

// 天空盒贴图集：10 列 × 8 行 32×32 图块（镜像 skybox.c 的 SkyboxTexture：
// 8 行 × 10 列，前 2 列重复用于环绕）。pixels 为 RGBA8（320×256），
// 解码失败时为空。
struct SkyboxImage {
    uint16_t width {320};
    uint16_t height {256};
    std::vector<uint8_t> pixels; // RGBA8，width*height*4
};

// 从段 0x0A（关卡脚本 LOAD_MIO0 已载入的天空盒段）解码贴图集：
// 指针表 = 段末 320 字节（skyconv.c 生成的 *_skybox_ptrlist 是段内最后一个
// 符号：80 个 seg-0x0A 指针，每个指向 32×32 RGBA16 图块）。
// 段缺失/结构不符时 ok() = false（image() 为空），error() 给出原因。
class SkyboxDecoder {
    const SegmentTable &seg_table_;
    SkyboxImage image_;
    bool ok_ {false};
    std::string error_;

public:
    explicit SkyboxDecoder(const SegmentTable &seg_table) : seg_table_(seg_table) {}

    void run(); // 从段 0x0A 解码（每次 run 重置内部结果）
    bool ok() const { return ok_; }
    const std::string &error() const { return error_; }
    const SkyboxImage &image() const { return image_; }
};

} // namespace Skybox

#endif /* SKYBOX_H */
