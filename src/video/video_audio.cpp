#include "video/video_audio.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <cstring>
#include <cmath>
#include <cstdio>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace ab {
template <class T> static void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

bool DecodeVideoAudio(const std::string& path, std::vector<int16_t>& pcm, int& rate, int& channels, std::string* error) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool coInit = SUCCEEDED(hr);
    if (FAILED(MFStartup(MF_VERSION))) { if (coInit) CoUninitialize(); if (error) *error = "Media Foundation did not start"; return false; }
    bool ok = false;
    IMFSourceReader* reader = nullptr;
    IMFMediaType* type = nullptr;
    IMFMediaType* actual = nullptr;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wide((size_t)(wlen > 0 ? wlen : 1), 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), wlen);
    hr = MFCreateSourceReaderFromURL(wide.data(), nullptr, &reader);
    if (SUCCEEDED(hr)) {
        const DWORD audio = (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM;
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        reader->SetStreamSelection(audio, TRUE);
        hr = MFCreateMediaType(&type);
        if (SUCCEEDED(hr)) {
            type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
            type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
            type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);                // v3.0: all of the PCM format spelled out, so the reader cannot pick another one
            type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 44100 * 4);
            type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
            hr = reader->SetCurrentMediaType(audio, nullptr, type);
        }
        if (SUCCEEDED(hr) && SUCCEEDED(reader->GetCurrentMediaType(audio, &actual))) {
            UINT32 r = 44100, c = 2;
            actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &r);
            actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &c);
            UINT32 bits = 16; GUID sub = {};
            actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
            actual->GetGUID(MF_MT_SUBTYPE, &sub);
            const bool isFloat = sub == MFAudioFormat_Float;
            rate = (int)r; channels = (int)c;
            LONGLONG stampSum = 0;                                          // the length the reader itself gives the sound (100 ns units)
            for (;;) {
                DWORD flags = 0;
                IMFSample* sample = nullptr;
                hr = reader->ReadSample(audio, 0, nullptr, &flags, nullptr, &sample);
                if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) { Release(sample); break; }
                if (sample) {
                    LONGLONG dur = 0; if (SUCCEEDED(sample->GetSampleDuration(&dur))) stampSum += dur;
                    IMFMediaBuffer* buffer = nullptr;
                    if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                        BYTE* data = nullptr; DWORD len = 0;
                        if (SUCCEEDED(buffer->Lock(&data, nullptr, &len))) {
                            size_t at = pcm.size();
                            if (isFloat) {                                  // 32 bit float samples: made 16 bit
                                size_t n = len / 4; pcm.resize(at + n);
                                const float* f = (const float*)data;
                                for (size_t i = 0; i < n; i++) { float v = f[i] * 32767.0f; pcm[at + i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
                            } else if (bits == 8) {
                                pcm.resize(at + len);
                                for (DWORD i = 0; i < len; i++) pcm[at + i] = (int16_t)((data[i] - 128) * 256);
                            } else {
                                pcm.resize(at + len / 2);
                                std::memcpy(pcm.data() + at, data, (len / 2) * 2);
                            }
                            buffer->Unlock();
                        }
                    }
                    Release(buffer);
                    Release(sample);
                }
            }
            ok = !pcm.empty();
            // the reader's own timing against the sample count: when they disagree the sample rate it reported is wrong, and the timing is believed
            double seconds = (double)stampSum / 1e7, frames = (double)pcm.size() / (channels > 0 ? channels : 2);
            std::printf("video sound: %u Hz, %u channels, %u bits%s, %.0f frames, the reader says %.2f s (%.0f Hz)\n", r, c, bits, isFloat ? " float" : "", frames, seconds, seconds > 0 ? frames / seconds : 0.0);
            std::fflush(stdout);
            if (ok && seconds > 1.0) { double eff = frames / seconds; if (std::fabs(eff - rate) > rate * 0.02) rate = (int)(eff + 0.5); }
            if (!ok && error) *error = "the file has no sound";
        } else if (error) *error = "the sound could not be decoded (no AAC decoder?)";
    } else if (error) *error = "Media Foundation could not open the file";
    Release(actual); Release(type); Release(reader);
    MFShutdown();
    if (coInit) CoUninitialize();
    return ok;
}
}  // namespace ab
#else
namespace ab {
bool DecodeVideoAudio(const std::string&, std::vector<int16_t>&, int&, int&, std::string* error) {
    if (error) *error = "this system has no AAC decoder in the engine: the video plays without sound";
    return false;
}
}  // namespace ab
#endif
