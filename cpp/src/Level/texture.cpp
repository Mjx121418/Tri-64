#include "Level/texture.h"
#include "Math/math.h"

namespace GBI {

std::expected<Texture, std::string> decodeTexture(const Material &m,
                                                  SegmentedAddress tex_image,
                                                  const SegmentTable &seg_table) {
    if (!m.textured) {
        return std::unexpected("material is not textured");
    }
    if (m.tile_siz != 2) {
        return std::unexpected("only 16-bit textures supported so far");
    }

    const uint16_t w = m.tex_width();
    const uint16_t h = m.tex_height();
    const uint32_t x0 = m.tex_sl / 4; // 区域起点（纹素）
    const uint32_t y0 = m.tex_tl / 4;
    // 行跨度（源图像行宽）：优先用 G_LOADBLOCK 的 DXT 反推 ——
    // dxt = ceil(2^11/words_per_line)（CALC_DXT 约定），words_per_line =
    // 行宽(纹素) × 每纹素字节 / 8；无 DXT（G_LOADTILE）时按整图（区域宽）兜底。
    const uint32_t image_width = [&]() -> uint32_t {
        if (m.tex_dxt != 0) {
            const uint32_t words = (2048 + m.tex_dxt - 1) / m.tex_dxt; // ceil(2^11/dxt)
            const uint32_t bytes_per_texel = (m.tile_siz == 2) ? 2 : (m.tile_siz == 3) ? 4 : 1;
            const uint32_t stride = words * 8 / bytes_per_texel;
            if (stride >= x0 + w) { // 图块必在图像内，行宽不可能小于图块宽
                return stride;
            }
        }
        return w;
    }();

    Texture tex;
    tex.width = w;
    tex.height = h;
    tex.fmt = m.tile_fmt;
    tex.siz = m.tile_siz;
    tex.pixels.resize(size_t(w) * h * 4);

    // 一次读入覆盖区域所需的图像字节（16-bit 行主序）
    const size_t needed = (size_t(y0 + h) * image_width + (x0 + w)) * 2;
    std::span<const uint8_t> d = seg_table.data(tex_image, static_cast<uint32_t>(needed));

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            const size_t off = (size_t(y0 + y) * image_width + (x0 + x)) * 2;
            if (off + 2 > d.size()) {
                return std::unexpected("texture image data out of range");
            }
            const uint16_t v = readInt<uint16_t>(d, off);
            uint8_t *p = &tex.pixels[size_t(y * w + x) * 4];
            switch (m.tile_fmt) {
            case 0: { // RGBA16: rrrrr ggggg bbbbb a
                const uint8_t r5 = (v >> 11) & 0x1F;
                const uint8_t g5 = (v >> 6) & 0x1F;
                const uint8_t b5 = (v >> 1) & 0x1F;
                p[0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
                p[1] = static_cast<uint8_t>((g5 << 3) | (g5 >> 2));
                p[2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
                p[3] = (v & 1) ? 255 : 0;
                break;
            }
            case 3: { // IA16: 强度 8 位 + 透明 8 位
                const uint8_t i = v >> 8;
                p[0] = p[1] = p[2] = i;
                p[3] = v & 0xFF;
                break;
            }
            default:
                return std::unexpected(
                    "unsupported texture format (only RGBA16/IA16 so far)");
            }
        }
    }
    return tex;
}

} // namespace GBI
