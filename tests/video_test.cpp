// v2.2 video: the MP4 reader and the H.264 decoder. The iOS videos are not part of the repository, so the test runs on a file named by
// the environment variable AB_TEST_VIDEO (the .m4v from the .ipa) and passes without it. AB_TEST_VIDEO_OUT=<file.ppm> saves a frame.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "video/video_player.h"

int main() {
    const char* path = std::getenv("AB_TEST_VIDEO");
    if (!path) { std::printf("video test skipped (set AB_TEST_VIDEO to an .m4v file)\n"); return 0; }
    ab::VideoPlayer v;
    std::string error;
    if (!v.Open(path, &error)) { std::printf("FAIL open: %s\n", error.c_str()); return 1; }
    std::printf("video %dx%d, %.2f s\n", v.Width(), v.Height(), v.Duration());
    int changes = 0;
    double t = 0;
    double stop = std::getenv("AB_TEST_VIDEO_STOP") ? std::atof(std::getenv("AB_TEST_VIDEO_STOP")) : 600;     // seconds: stop there (to look at a frame in the middle)
    while (!v.Finished() && t < stop) { if (v.Advance(1.0 / 30.0)) changes++; t += 1.0 / 30.0; }
    std::printf("%d pictures, %d decoded, finished %d\n", changes, v.FramesDecoded(), (int)v.Finished());
    if (v.Rgba().size() != (size_t)v.Width() * v.Height() * 4 || v.FramesDecoded() < 10) { std::printf("FAIL: no pictures\n"); return 1; }
    if (const char* out = std::getenv("AB_TEST_VIDEO_OUT")) {
        if (FILE* f = std::fopen(out, "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", v.Width(), v.Height());
            for (size_t i = 0; i < v.Rgba().size(); i += 4) std::fwrite(&v.Rgba()[i], 1, 3, f);
            std::fclose(f);
        }
    }
    std::printf("video test passed\n");
    return 0;
}
