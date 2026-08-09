#include "Level/texture.h"
#include "Math/math.h"
#include <utility>

namespace GBI {

// 16 位纹素 → RGBA8（fmt 0 RGBA16 与 CI 调色板共用）。
static void rgba16ToRgba8(uint8_t *p, uint16_t v) {
    const uint8_t r5 = (v >> 11) & 0x1F;
    const uint8_t g5 = (v >> 6) & 0x1F;
    const uint8_t b5 = (v >> 1) & 0x1F;
    p[0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    p[1] = static_cast<uint8_t>((g5 << 3) | (g5 >> 2));
    p[2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    p[3] = (v & 1) ? 255 : 0;
}

static void ia16ToRgba8(uint8_t *p, uint16_t v) {
    const uint8_t i = static_cast<uint8_t>(v >> 8);
    p[0] = p[1] = p[2] = i;
    p[3] = v & 0xFF;
}

// 读 CI 调色板条目（16 位）并解码成 RGBA8；lut_type 决定 RGBA16 / IA16 解释。
static bool paletteEntryToRgba8(uint8_t *p, const SegmentTable &seg, SegmentedAddress tlut,
                                uint32_t entry, uint8_t lut_type) {
    std::span<const uint8_t> d;
    try {
        d = seg.data({ tlut.seg, tlut.offset + entry * 2 }, 2);
    } catch (const std::out_of_range &) {
        return false;
    }
    const uint16_t v = readInt<uint16_t>(d, 0);
    if (lut_type == G_TT_IA16) {
        ia16ToRgba8(p, v);
    } else {
        rgba16ToRgba8(p, v);
    }
    return true;
}

bool TextureDecoder::run(const Material &m, SegmentedAddress tex_image, uint32_t tlut_image) {
    texture_ = {};
    error_.clear();

    if (!m.textured) {
        error_ = "material is not textured";
        return false;
    }
    if (m.tile_fmt == 1) { // YUV：RDP 很少用，不实现
        error_ = "YUV texture format unsupported";
        return false;
    }
    if (m.tile_fmt == 2 /* CI */ && tlut_image == 0) {
        error_ = "CI texture without a bound TLUT";
        return false;
    }

    const uint16_t w = m.tex_width();
    const uint16_t h = m.tex_height();
    const uint32_t x0 = m.tex_sl / 4; // 区域起点（纹素）
    const uint32_t y0 = m.tex_tl / 4;

    // 源图像行宽（纹素）：优先用 G_LOADBLOCK 的 DXT 反推行字节宽，再按位深换算。
    // dxt = ceil(2^11/words_per_line)（CALC_DXT 约定），行字节宽 = words*8。
    uint32_t texel_stride = w; // 兜底：按图块宽（无 DXT / G_LOADTILE）
    if (m.tex_dxt != 0) {
        const uint32_t words = (2048 + m.tex_dxt - 1) / m.tex_dxt; // ceil(2^11/dxt)
        const uint32_t byte_stride = words * 8;
        switch (m.tile_siz) {
            case 0: texel_stride = byte_stride * 2; break; // 4-bit：2 纹素/字节
            case 1: texel_stride = byte_stride; break;     // 8-bit：1 纹素/字节
            case 2: texel_stride = byte_stride / 2; break; // 16-bit
            case 3: texel_stride = byte_stride / 4; break; // 32-bit
            default: texel_stride = w; break;
        }
        if (texel_stride < x0 + w) { // 图块必在图像内，行宽不可能小于图块宽
            texel_stride = w;
        }
    }

    texture_.width = w;
    texture_.height = h;
    texture_.fmt = m.tile_fmt;
    texture_.siz = m.tile_siz;
    texture_.pixels.resize(size_t(w) * h * 4);

    // 覆盖区域最后一个纹素实际读到的字节偏移 + 每纹素字节数（不再多读：
    // 段尾的纹理图像刚好到段边界时，多读会让 SegmentTable::data 的边界检查抛
    // 异常，导致纹理解码失败）。
    const size_t last_row = y0 + h - 1;
    const size_t last_col = x0 + w - 1;
    size_t needed = 0;
    switch (m.tile_siz) {
        case 0: needed = last_row * texel_stride + last_col / 2 + 1; break; // 4-bit
        case 1: needed = last_row * texel_stride + last_col + 1; break;     // 8-bit
        case 2: needed = (last_row * texel_stride + last_col) * 2 + 2; break; // 16-bit
        case 3: needed = (last_row * texel_stride + last_col) * 4 + 4; break; // 32-bit
        default: needed = (last_row * texel_stride + last_col) * 2 + 2; break;
    }
    std::span<const uint8_t> d;
    try {
        d = seg_table_.data(tex_image, static_cast<uint32_t>(needed));
    } catch (const std::out_of_range &) {
        error_ = "texture image out of range";
        return false;
    }

    // CI 调色板源（段地址）；CI4 用 palette*16 切片，CI8 全表。
    SegmentedAddress tlut = (m.tile_fmt == 2) ? segAddress(tlut_image) : SegmentedAddress {};

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            const size_t oy = y0 + y;
            const size_t ox = x0 + x;
            uint8_t *p = &texture_.pixels[size_t(y * w + x) * 4];
            switch (m.tile_fmt) {
            case 0: { // RGBA
                if (m.tile_siz == 2) {
                    const size_t off = (oy * texel_stride + ox) * 2;
                    if (off + 2 > d.size()) { error_ = "texture image out of range"; return false; }
                    rgba16ToRgba8(p, readInt<uint16_t>(d, off));
                } else if (m.tile_siz == 3) {
                    const size_t off = (oy * texel_stride + ox) * 4;
                    if (off + 4 > d.size()) { error_ = "texture image out of range"; return false; }
                    p[0] = d[off];
                    p[1] = d[off + 1];
                    p[2] = d[off + 2];
                    p[3] = d[off + 3];
                } else {
                    error_ = "RGBA requires 16/32-bit";
                    return false;
                }
                break;
            }
            case 2: { // CI：索引 → 调色板
                uint32_t index = 0;
                if (m.tile_siz == 1) {
                    const size_t off = oy * texel_stride + ox;
                    if (off + 1 > d.size()) { error_ = "texture image out of range"; return false; }
                    index = d[off];
                } else if (m.tile_siz == 0) {
                    const size_t byte = oy * texel_stride + ox / 2;
                    if (byte + 1 > d.size()) { error_ = "texture image out of range"; return false; }
                    index = (ox & 1) ? (d[byte] & 0x0F) : (d[byte] >> 4);
                    index += static_cast<uint32_t>(m.tex_palette) * 16;
                } else {
                    error_ = "CI requires 4/8-bit";
                    return false;
                }
                if (!paletteEntryToRgba8(p, seg_table_, tlut, index, m.lut_type)) {
                    error_ = "palette entry out of range";
                    return false;
                }
                break;
            }
            case 3: { // IA
                if (m.tile_siz == 2) {
                    const size_t off = (oy * texel_stride + ox) * 2;
                    if (off + 2 > d.size()) { error_ = "texture image out of range"; return false; }
                    ia16ToRgba8(p, readInt<uint16_t>(d, off));
                } else if (m.tile_siz == 1) {
                    const size_t off = oy * texel_stride + ox;
                    if (off + 1 > d.size()) { error_ = "texture image out of range"; return false; }
                    const uint8_t v = d[off];
                    p[0] = p[1] = p[2] = static_cast<uint8_t>((v >> 4) << 4);
                    p[3] = static_cast<uint8_t>((v & 0x0F) << 4);
                } else if (m.tile_siz == 0) {
                    const size_t byte = oy * texel_stride + ox / 2;
                    if (byte + 1 > d.size()) { error_ = "texture image out of range"; return false; }
                    const uint8_t n = (ox & 1) ? (d[byte] & 0x0F) : (d[byte] >> 4);
                    p[0] = p[1] = p[2] = static_cast<uint8_t>(((n >> 1) << 5));
                    p[3] = (n & 1) ? 255 : 0;
                } else {
                    error_ = "IA requires 4/8/16-bit";
                    return false;
                }
                break;
            }
            case 4: { // I（灰度）
                if (m.tile_siz == 1) {
                    const size_t off = oy * texel_stride + ox;
                    if (off + 1 > d.size()) { error_ = "texture image out of range"; return false; }
                    p[0] = p[1] = p[2] = d[off];
                    p[3] = 255;
                } else if (m.tile_siz == 0) {
                    const size_t byte = oy * texel_stride + ox / 2;
                    if (byte + 1 > d.size()) { error_ = "texture image out of range"; return false; }
                    const uint8_t n = (ox & 1) ? (d[byte] & 0x0F) : (d[byte] >> 4);
                    p[0] = p[1] = p[2] = static_cast<uint8_t>(n << 4);
                    p[3] = 255;
                } else {
                    error_ = "I requires 4/8-bit";
                    return false;
                }
                break;
            }
            default:
                error_ = "unsupported texture format";
                return false;
            }
        }
    }
    return true;
}

Texture TextureDecoder::takeTexture() {
    return std::move(texture_);
}

} // namespace GBI
