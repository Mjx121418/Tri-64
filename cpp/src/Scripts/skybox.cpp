#include "Scripts/skybox.h"

#include "Math/math.h"
#include <span>

namespace Skybox {

namespace {
constexpr size_t kPtrListBytes = 80 * 4; // 80 个 u32 指针
constexpr size_t kTileBytes = 32 * 32 * 2; // 32×32 RGBA16
} // namespace

void SkyboxDecoder::run() {
    ok_ = false;
    image_ = {};
    error_.clear();

    // 段 0x0A（天空盒）：关卡脚本的 LOAD_MIO0 已把该关卡的天空盒段载入。
    // 注意用无长度重载：data(addr, length) 只返回 length 字节，这里要整段。
    std::span<const uint8_t> seg;
    try {
        seg = seg_table_.data(SegmentedAddress {0x0A, 0});
    } catch (const std::out_of_range &) {
        error_ = "segment 0x0A not loaded";
        return;
    }
    if (seg.size() < kPtrListBytes) {
        error_ = "segment 0x0A too small (" + std::to_string(seg.size()) + " bytes)";
        return;
    }
    const size_t ptrlist = seg.size() - kPtrListBytes;

    // 校验指针表：80 个指针都指向段内图块区（在指针表之前），然后解码。
    std::vector<uint32_t> tile_offsets(80);
    for (size_t i = 0; i < 80; i++) {
        const uint32_t p = readInt<uint32_t>(seg, ptrlist + i * 4);
        if ((p >> 24) != 0x0A) {
            error_ = "segment 0x0A tile pointer " + std::to_string(i) + " = " +
                     std::to_string(p) + " is not a seg-0x0A pointer";
            return;
        }
        const uint32_t off = p & 0x00FFFFFF;
        if (off + kTileBytes > ptrlist) {
            error_ = "segment 0x0A tile pointer " + std::to_string(i) + " offset 0x" +
                     std::to_string(off) + " out of range (pointer table at 0x" +
                     std::to_string(ptrlist) + ")";
            return;
        }
        tile_offsets[i] = off;
    }

    // 组装 10×8 贴图集：tile i → col = i % 10, row = i / 10。
    image_.pixels.resize(size_t(image_.width) * image_.height * 4);
    for (size_t i = 0; i < 80; i++) {
        const size_t col = i % 10;
        const size_t row = i / 10;
        for (size_t ty = 0; ty < 32; ty++) {
            for (size_t tx = 0; tx < 32; tx++) {
                const uint16_t v =
                    readInt<uint16_t>(seg, tile_offsets[i] + (ty * 32 + tx) * 2);
                uint8_t *p = &image_.pixels[(row * 32 + ty) * image_.width * 4
                                            + (col * 32 + tx) * 4];
                // RGBA16（r5 g5 b5 a1）→ RGBA8（5→8 位扩张）
                p[0] = uint8_t((v >> 11) & 0x1F);
                p[0] = uint8_t((p[0] << 3) | (p[0] >> 2));
                p[1] = uint8_t((v >> 6) & 0x1F);
                p[1] = uint8_t((p[1] << 3) | (p[1] >> 2));
                p[2] = uint8_t((v >> 1) & 0x1F);
                p[2] = uint8_t((p[2] << 3) | (p[2] >> 2));
                p[3] = (v & 1) ? 255 : 0;
            }
        }
    }
    ok_ = true;
}

} // namespace Skybox
