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
            hr = reader->SetCurrentMediaType(audio, nullptr, type);
        }
        if (SUCCEEDED(hr) && SUCCEEDED(reader->GetCurrentMediaType(audio, &actual))) {
            UINT32 r = 44100, c = 2;
            actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &r);
            actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &c);
            rate = (int)r; channels = (int)c;
            for (;;) {
                DWORD flags = 0;
                IMFSample* sample = nullptr;
                hr = reader->ReadSample(audio, 0, nullptr, &flags, nullptr, &sample);
                if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) { Release(sample); break; }
                if (sample) {
                    IMFMediaBuffer* buffer = nullptr;
                    if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                        BYTE* data = nullptr; DWORD len = 0;
                        if (SUCCEEDED(buffer->Lock(&data, nullptr, &len))) {
                            size_t at = pcm.size();
                            pcm.resize(at + len / 2);
                            std::memcpy(pcm.data() + at, data, (len / 2) * 2);
                            buffer->Unlock();
                        }
                    }
                    Release(buffer);
                    Release(sample);
                }
            }
            ok = !pcm.empty();
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
