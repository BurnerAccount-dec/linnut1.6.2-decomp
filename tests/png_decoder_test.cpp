// Decodes real game PNGs and compares a hash of the pixels with one computed by another decoder (Pillow).
// Usage: ab_png_tests <game images folder, e.g. ...\\data\\images\\pc_build> <tests/data/png_sums.txt>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/png_decoder.h"

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: %s <images folder> <png_sums.txt>\n", argv[0]); return 2; }
    FILE* sums = fopen(argv[2], "r");
    if (!sums) { std::printf("cannot open %s\n", argv[2]); return 2; }
    int failures = 0;
    char name[256];
    unsigned long long expected;
    unsigned long long size;
    while (fscanf(sums, "%255s %llx %llu", name, &expected, &size) == 3) {
        ab::Image img;
        std::string err;
        std::string path = std::string(argv[1]) + "/" + name + ".png";
        if (!ab::LoadPngFile(path, img, &err)) { std::printf("FAIL %s: %s\n", name, err.c_str()); failures++; continue; }
        unsigned long long h = 0xcbf29ce484222325ULL;       // FNV-1a over the RGBA bytes
        for (size_t i = 0; i < img.rgba.size(); i++) h = (h ^ img.rgba[i]) * 0x100000001b3ULL;
        bool same = h == expected && img.rgba.size() == size;
        std::printf("%s %dx%d %s\n", name, img.width, img.height, same ? "ok" : "DIFFERENT");
        if (!same) failures++;
    }
    fclose(sums);
    if (failures == 0) std::printf("png_decoder: all passed\n");
    return failures ? 1 : 0;
}
