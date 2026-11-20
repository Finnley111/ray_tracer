#pragma once
#include "color.h"
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
