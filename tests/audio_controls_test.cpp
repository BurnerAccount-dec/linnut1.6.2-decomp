// v2.2 audio controls: speed (resampling), pause and master volume of the mixer.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#include "audio/audio_mixer.h"

using namespace ab;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

// a mono 16 bit WAV whose sample n is n * 10 (a ramp), so the position that was read can be told from the value
static void WriteRamp(const char* path, int frames) {
    std::vector<int16_t> d(frames);
    for (int i = 0; i < frames; i++) d[i] = (int16_t)(i * 10);
    uint32_t bytes = frames * 2, riff = bytes + 36, rate = 44100, brate = rate * 2;
    unsigned char h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,2,0,16,0,'d','a','t','a'};
    std::memcpy(h + 4, &riff, 4); std::memcpy(h + 24, &rate, 4); std::memcpy(h + 28, &brate, 4); std::memcpy(h + 40, &bytes, 4);
    FILE* f = std::fopen(path, "wb");
    std::fwrite(h, 1, 44, f); std::fwrite(d.data(), 2, d.size(), f); std::fclose(f);
}

int main() {
    WriteRamp("ab_ramp_test.wav", 2000);
    AudioMixer m;
    m.CreateOutput(2, 16, 44100);
    m.Start();
    m.AddClip("ramp", "ab_ramp_test.wav");
    int16_t out[200];

    m.Play("ramp", 1.0f, false, 0);
    m.Mix(out, 100);                                   // normal speed: frame n is n * 10 on both channels
    CHECK(out[2 * 50] == 500 && out[2 * 50 + 1] == 500);

    m.StopAll(); m.Mix(out, 10);
    m.Play("ramp", 1.0f, false, 0);
    m.SetSpeed(2.0f);                                  // twice as fast: frame n shows source frame 2n
    m.Mix(out, 100);
    CHECK(out[2 * 50] == 1000);
    CHECK(m.Speed() == 2.0f);

    m.SetSpeed(1.0f);
    m.Pause(true);                                     // paused: silence, and the voice keeps its place
    m.Mix(out, 100);
    CHECK(out[2 * 50] == 0);
    CHECK(m.IsActive("ramp"));
    m.Pause(false);
    m.Mix(out, 10);
    CHECK(out[0] > 0 && out[0] < 2000 * 10);           // it continues from where it was left

    m.StopAll(); m.Mix(out, 10);
    m.Play("ramp", 1.0f, false, 0);
    m.SetMasterVolume(0.5f);
    m.Mix(out, 100);
    CHECK(out[2 * 50] == 250);
    std::remove("ab_ramp_test.wav");
    if (g_fail == 0) std::printf("audio control tests passed\n");
    return g_fail ? 1 : 0;
}
