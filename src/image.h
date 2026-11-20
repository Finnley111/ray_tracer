#pragma once
#include "color.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// Linear-RGB float framebuffer. Encoding to sRGB happens only on write.
struct Image {
    int w, h;
    std::vector<RGB> px;
    Image(int w, int h) : w(w), h(h), px(size_t(w) * h, RGB{0, 0, 0}) {}
    RGB& at(int x, int y) { return px[size_t(y) * w + x]; }
};

// Binary PPM (P6). Returns false if the file could not be written.
inline bool write_ppm(const Image& img, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << img.w << ' ' << img.h << "\n255\n";
    for (const RGB& p : img.px)
        for (float c : p) f.put(char(int(srgb_encode(c) * 255.0f + 0.5f)));
    return bool(f);
}

// 8-bit sRGB PNG. Deflate uses stored (uncompressed) blocks, so no zlib is needed;
// files are ~3x larger than compressed ones.
// ponytail: no compression; vendor stb_image_write if file size ever matters.
inline bool write_png(const Image& img, const std::string& path) {
    static const auto crc_table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    auto be32 = [](std::string& s, uint32_t v) { for (int i = 3; i >= 0; --i) s += char((v >> (8 * i)) & 0xFF); };
    auto chunk = [&](std::string& out, const char* type, const std::string& data) {
        be32(out, uint32_t(data.size()));
        std::string td = std::string(type, 4) + data;
        uint32_t c = 0xFFFFFFFFu;
        for (unsigned char b : td) c = crc_table[(c ^ b) & 0xFF] ^ (c >> 8);
        out += td;
        be32(out, c ^ 0xFFFFFFFFu);
    };
    // Raw scanlines: filter byte 0, then RGB.
    std::string raw;
    raw.reserve(size_t(img.h) * (1 + 3 * size_t(img.w)));
    for (int y = 0; y < img.h; ++y) {
        raw += '\0';
        for (int x = 0; x < img.w; ++x)
            for (float c : img.px[size_t(y) * img.w + x]) raw += char(int(srgb_encode(c) * 255.0f + 0.5f));
    }
    // zlib stream: header, stored blocks of <= 65535 bytes, Adler-32.
    std::string z = "\x78\x01";
    for (size_t pos = 0; pos < raw.size() || pos == 0; pos += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        z += char(pos + n >= raw.size() ? 1 : 0);
        z += char(n & 0xFF); z += char(n >> 8);
        z += char(~n & 0xFF); z += char((~n >> 8) & 0xFF);
        z.append(raw, pos, n);
        if (raw.empty()) break;
    }
    uint32_t a = 1, b = 0;
    for (unsigned char ch : raw) { a = (a + ch) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);

    std::string ihdr;
    be32(ihdr, uint32_t(img.w));
    be32(ihdr, uint32_t(img.h));
    ihdr += std::string("\x08\x02\x00\x00\x00", 5);  // 8-bit, RGB, deflate, no filter, no interlace
    std::string out = "\x89PNG\r\n\x1a\n";
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", "");
    std::ofstream f(path, std::ios::binary);
    return f && f.write(out.data(), std::streamsize(out.size()));
}

// PNG if the path ends in .png, otherwise PPM.
inline bool write_image(const Image& img, const std::string& path) {
    bool png = path.size() >= 4 && path.compare(path.size() - 4, 4, ".png") == 0;
    return png ? write_png(img, path) : write_ppm(img, path);
}
