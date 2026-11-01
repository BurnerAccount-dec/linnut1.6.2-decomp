#include "video/video_player.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include "h264bsd_decoder.h"
}

namespace ab {

namespace {

uint32_t U32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
uint64_t U64(const uint8_t* p) { return (uint64_t)U32(p) << 32 | U32(p + 4); }

struct Box { size_t start, dataStart, end; char type[5]; };

// the box that starts at `at` inside [at, limit)
bool ReadBox(const std::vector<uint8_t>& f, size_t at, size_t limit, Box& b) {
    if (at + 8 > limit) return false;
    uint64_t size = U32(&f[at]);
    b.start = at;
    std::memcpy(b.type, &f[at + 4], 4);
    b.type[4] = 0;
    b.dataStart = at + 8;
    if (size == 1) {
        if (at + 16 > limit) return false;
        size = U64(&f[at + 8]);
        b.dataStart = at + 16;
    } else if (size == 0) {
        size = limit - at;
    }
    if (size < (uint64_t)(b.dataStart - at) || at + size > limit) return false;
    b.end = (size_t)(at + size);
    return true;
}

bool Find(const std::vector<uint8_t>& f, size_t from, size_t to, const char* type, Box& out) {
    size_t at = from;
    Box b;
    while (ReadBox(f, at, to, b)) {
        if (!std::strcmp(b.type, type)) { out = b; return true; }
        at = b.end;
    }
    return false;
}

}  // namespace

VideoPlayer::~VideoPlayer() {
    CloseSystem();
    if (decoder_) { h264bsdShutdown((storage_t*)decoder_); h264bsdFree((storage_t*)decoder_); }
}

bool VideoPlayer::Open(const std::string& path, std::string* error) {
    {   // the system decoder first (Windows); when it cannot open the file, h264bsd below tries the baseline profile
        std::string sysError;
        if (OpenSystem(path, &sysError)) return true;
        if (!sysError.empty()) std::printf("playVideo: system decoder: %s; using the built-in baseline decoder\n", sysError.c_str());
    }
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) { if (error) *error = "cannot open " + path; return false; }
    std::fseek(fp, 0, SEEK_END);
    long n = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    file_.resize(n > 0 ? (size_t)n : 0);
    if (n > 0 && std::fread(file_.data(), 1, (size_t)n, fp) != (size_t)n) { std::fclose(fp); if (error) *error = "cannot read " + path; return false; }
    std::fclose(fp);
    if (!Parse(error)) return false;
    decoder_ = h264bsdAlloc();
    if (!decoder_ || h264bsdInit((storage_t*)decoder_, 1) != 0) { if (error) *error = "the H.264 decoder did not start"; return false; }
    return true;
}

bool VideoPlayer::Parse(std::string* error) {
    Box moov;
    if (!Find(file_, 0, file_.size(), "moov", moov)) { if (error) *error = "not an MP4 file (no moov box)"; return false; }
    size_t at = moov.dataStart;
    Box trak;
    while (at < moov.end && ReadBox(file_, at, moov.end, trak)) {
        at = trak.end;
        if (std::strcmp(trak.type, "trak")) continue;
        Box mdia, hdlr, mdhd, minf, stbl, stsd;
        if (!Find(file_, trak.dataStart, trak.end, "mdia", mdia)) continue;
        if (!Find(file_, mdia.dataStart, mdia.end, "hdlr", hdlr) || hdlr.end - hdlr.dataStart < 12) continue;
        if (std::memcmp(&file_[hdlr.dataStart + 8], "vide", 4) != 0) continue;
        if (!Find(file_, mdia.dataStart, mdia.end, "mdhd", mdhd)) continue;
        const uint8_t* m = &file_[mdhd.dataStart];
        uint32_t timescale = m[0] == 1 ? U32(m + 20) : U32(m + 12);
        if (!timescale) timescale = 1000;
        if (!Find(file_, mdia.dataStart, mdia.end, "minf", minf) || !Find(file_, minf.dataStart, minf.end, "stbl", stbl)) continue;

        // the sample description: the avc1 entry holds the avcC box with the parameter sets
        if (!Find(file_, stbl.dataStart, stbl.end, "stsd", stsd)) continue;
        Box entry;
        if (!ReadBox(file_, stsd.dataStart + 8, stsd.end, entry)) continue;
        if (std::strcmp(entry.type, "avc1") != 0 && std::strcmp(entry.type, "avc3") != 0) { if (error) *error = "the video is not H.264"; return false; }
        width_ = U16(&file_[entry.dataStart + 24]);
        height_ = U16(&file_[entry.dataStart + 26]);
        Box avcC;
        if (!Find(file_, entry.dataStart + 78, entry.end, "avcC", avcC) || avcC.end - avcC.dataStart < 7) { if (error) *error = "no avcC box"; return false; }
        const uint8_t* a = &file_[avcC.dataStart];
        nalLength_ = (a[4] & 3) + 1;
        size_t p = avcC.dataStart + 6;
        int count = a[5] & 0x1f;
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < count && p + 2 <= avcC.end; i++) {
                size_t len = U16(&file_[p]);
                p += 2;
                if (p + len > avcC.end) break;
                parameterSets_.emplace_back(file_.begin() + p, file_.begin() + p + len);
                p += len;
            }
            if (pass == 0 && p < avcC.end) count = file_[p++];
        }

        // the sample tables
        Box stts, stsz, stsc, stco, stss;
        bool wide = false;
        if (!Find(file_, stbl.dataStart, stbl.end, "stts", stts) || !Find(file_, stbl.dataStart, stbl.end, "stsz", stsz) ||
            !Find(file_, stbl.dataStart, stbl.end, "stsc", stsc)) { if (error) *error = "incomplete sample tables"; return false; }
        if (!Find(file_, stbl.dataStart, stbl.end, "stco", stco)) { if (!Find(file_, stbl.dataStart, stbl.end, "co64", stco)) { if (error) *error = "no chunk offsets"; return false; } wide = true; }
        bool haveSync = Find(file_, stbl.dataStart, stbl.end, "stss", stss);

        const uint8_t* sz = &file_[stsz.dataStart];
        uint32_t fixedSize = U32(sz + 4), sampleCount = U32(sz + 8);
        std::vector<uint32_t> sizes(sampleCount);
        for (uint32_t i = 0; i < sampleCount; i++) {
            if (fixedSize) sizes[i] = fixedSize;
            else if (stsz.dataStart + 12 + (size_t)i * 4 + 4 <= stsz.end) sizes[i] = U32(sz + 12 + (size_t)i * 4);
        }
        std::vector<uint64_t> chunks;
        {
            const uint8_t* c = &file_[stco.dataStart];
            uint32_t n = U32(c + 4);
            for (uint32_t i = 0; i < n; i++) {
                size_t at2 = stco.dataStart + 8 + (size_t)i * (wide ? 8 : 4);
                if (at2 + (wide ? 8 : 4) > stco.end) break;
                chunks.push_back(wide ? U64(&file_[at2]) : U32(&file_[at2]));
            }
        }
        struct Run { uint32_t first, perChunk; };
        std::vector<Run> runs;
        {
            const uint8_t* c = &file_[stsc.dataStart];
            uint32_t n = U32(c + 4);
            for (uint32_t i = 0; i < n && stsc.dataStart + 8 + (size_t)i * 12 + 12 <= stsc.end; i++)
                runs.push_back({U32(c + 8 + i * 12), U32(c + 8 + i * 12 + 4)});
        }
        std::vector<double> times(sampleCount, 0.0);
        {
            const uint8_t* c = &file_[stts.dataStart];
            uint32_t n = U32(c + 4);
            uint64_t t = 0; uint32_t s = 0;
            for (uint32_t i = 0; i < n && stts.dataStart + 8 + (size_t)i * 8 + 8 <= stts.end; i++) {
                uint32_t cnt = U32(c + 8 + i * 8), delta = U32(c + 8 + i * 8 + 4);
                for (uint32_t k = 0; k < cnt && s < sampleCount; k++, s++) { times[s] = (double)t / timescale; t += delta; }
            }
            duration_ = (double)t / timescale;
        }
        std::vector<bool> sync(sampleCount, !haveSync);
        if (haveSync) {
            const uint8_t* c = &file_[stss.dataStart];
            uint32_t n = U32(c + 4);
            for (uint32_t i = 0; i < n && stss.dataStart + 8 + (size_t)i * 4 + 4 <= stss.end; i++) {
                uint32_t idx = U32(c + 8 + i * 4);
                if (idx >= 1 && idx <= sampleCount) sync[idx - 1] = true;
            }
        }
        uint32_t s = 0;
        for (size_t ch = 0; ch < chunks.size() && s < sampleCount; ch++) {
            uint32_t per = 0;
            for (const Run& r : runs) if (r.first <= ch + 1) per = r.perChunk;
            uint64_t off = chunks[ch];
            for (uint32_t k = 0; k < per && s < sampleCount; k++, s++) {
                samples_.push_back({off, sizes[s], times[s], (bool)sync[s]});
                off += sizes[s];
            }
        }
        if (samples_.empty() || width_ <= 0 || height_ <= 0) { if (error) *error = "the video track is empty"; return false; }
        return true;
    }
    if (error) *error = "no video track";
    return false;
}

bool VideoPlayer::DecodeSample(size_t index) {
    const Sample& smp = samples_[index];
    if (smp.offset + smp.size > file_.size()) return false;
    std::vector<uint8_t> stream;
    static const uint8_t start[4] = {0, 0, 0, 1};
    if (!sentParameters_) {
        for (const auto& ps : parameterSets_) { stream.insert(stream.end(), start, start + 4); stream.insert(stream.end(), ps.begin(), ps.end()); }
        sentParameters_ = true;
    }
    size_t p = (size_t)smp.offset, end = p + smp.size;
    while (p + nalLength_ <= end) {
        size_t len = 0;
        for (int i = 0; i < nalLength_; i++) len = len << 8 | file_[p + i];
        p += nalLength_;
        if (p + len > end) break;
        stream.insert(stream.end(), start, start + 4);
        stream.insert(stream.end(), file_.begin() + p, file_.begin() + p + len);
        p += len;
    }
    uint8_t* data = stream.data();
    u32 left = (u32)stream.size();
    bool gotPicture = false;
    while (left > 0) {
        u32 read = 0;
        u32 r = h264bsdDecode((storage_t*)decoder_, data, left, 0, &read);
        if (r == H264BSD_PIC_RDY) {
            u32 picId, idr, errs;
            u32* pic = h264bsdNextOutputPictureRGBA((storage_t*)decoder_, &picId, &idr, &errs);
            if (pic) {
                // the picture is whole macroblocks wide and high; the cropping of the stream (1080p is 1088 coded) cuts it to size
                u32 crop = 0, cl = 0, cw = 0, ct = 0, chh = 0;
                h264bsdCroppingParams((storage_t*)decoder_, &crop, &cl, &cw, &ct, &chh);
                if (!crop) { cl = 0; ct = 0; cw = (u32)codedW_; chh = (u32)codedH_; }
                width_ = (int)cw; height_ = (int)chh;
                rgba_.resize((size_t)cw * chh * 4);
                const uint8_t* src = (const uint8_t*)pic;
                for (u32 y = 0; y < chh; y++) std::memcpy(&rgba_[(size_t)y * cw * 4], src + ((size_t)(y + ct) * codedW_ + cl) * 4, (size_t)cw * 4);
                gotPicture = true;
            }
        } else if (r == H264BSD_HDRS_RDY) {
            u32 w = h264bsdPicWidth((storage_t*)decoder_), h = h264bsdPicHeight((storage_t*)decoder_);
            if (w && h) { codedW_ = (int)w * 16; codedH_ = (int)h * 16; }
        } else if (r == H264BSD_ERROR || r == H264BSD_PARAM_SET_ERROR || r == H264BSD_MEMALLOC_ERROR) {
            if (read == 0) break;
        }
        if (read == 0) break;
        data += read;
        left -= read;
    }
    if (gotPicture) decoded_++;
    return gotPicture;
}

bool VideoPlayer::Advance(double dt) {
    if (finished_) return false;
    if (mf_) return AdvanceSystem(dt);
    clock_ += dt;
    bool changed = false;
    // decode every frame that is due, in order (P frames need the ones before them)
    while (next_ < samples_.size() && samples_[next_].time <= clock_) {
        if (DecodeSample(next_)) changed = true;
        next_++;
    }
    if (next_ >= samples_.size() && clock_ >= duration_) finished_ = true;
    return changed;
}

}  // namespace ab

// ---- the system decoder ----------------------------------------------------------------------------------------------
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")

namespace ab {
namespace {
struct MfState { IMFSourceReader* reader = nullptr; bool coInit = false; bool started = false; bool bottomUp = false; };
template <class T> void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }
}

void VideoPlayer::CloseSystem() {
    MfState* m = (MfState*)mf_;
    if (!m) return;
    Rel(m->reader);
    if (m->started) MFShutdown();
    if (m->coInit) CoUninitialize();
    delete m;
    mf_ = nullptr;
}

bool VideoPlayer::OpenSystem(const std::string& path, std::string* error) {
    MfState* m = new MfState;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    m->coInit = SUCCEEDED(hr);
    if (FAILED(MFStartup(MF_VERSION))) { if (error) *error = "Media Foundation did not start"; if (m->coInit) CoUninitialize(); delete m; return false; }
    m->started = true;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wide((size_t)(wlen > 0 ? wlen : 1), 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), wlen);
    IMFAttributes* attrs = nullptr;
    if (SUCCEEDED(MFCreateAttributes(&attrs, 1))) attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);   // lets the reader convert to RGB
    hr = MFCreateSourceReaderFromURL(wide.data(), attrs, &m->reader);
    Rel(attrs);
    if (FAILED(hr)) { if (error) *error = "Media Foundation could not open the file"; mf_ = m; CloseSystem(); return false; }
    const DWORD video = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;
    m->reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    m->reader->SetStreamSelection(video, TRUE);
    IMFMediaType* type = nullptr;
    hr = MFCreateMediaType(&type);
    if (SUCCEEDED(hr)) {
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        hr = m->reader->SetCurrentMediaType(video, nullptr, type);
    }
    Rel(type);
    IMFMediaType* actual = nullptr;
    if (FAILED(hr) || FAILED(m->reader->GetCurrentMediaType(video, &actual))) {
        if (error) *error = "the video cannot be decoded to pictures";
        Rel(actual); mf_ = m; CloseSystem(); return false;
    }
    UINT32 w = 0, h = 0;
    MFGetAttributeSize(actual, MF_MT_FRAME_SIZE, &w, &h);
    m->bottomUp = (INT32)MFGetAttributeUINT32(actual, MF_MT_DEFAULT_STRIDE, 0) < 0;      // a negative stride: the bottom row comes first
    Rel(actual);
    if (!w || !h) { if (error) *error = "the video has no size"; mf_ = m; CloseSystem(); return false; }
    width_ = (int)w; height_ = (int)h;
    PROPVARIANT var;
    PropVariantInit(&var);
    duration_ = 0;
    if (SUCCEEDED(m->reader->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var)) && var.vt == VT_UI8)
        duration_ = (double)var.uhVal.QuadPart / 1.0e7;
    PropVariantClear(&var);
    mf_ = m;
    rgba_.assign((size_t)w * h * 4, 0);
    for (size_t i = 3; i < rgba_.size(); i += 4) rgba_[i] = 255;
    return true;
}

// reads the next picture into pending_; false at the end of the stream
static bool ReadPicture(IMFSourceReader* reader, int w, int h, bool bottomUp, std::vector<uint8_t>& out, double& time) {
    for (;;) {
        DWORD flags = 0;
        LONGLONG stamp = 0;
        IMFSample* sample = nullptr;
        HRESULT hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &stamp, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) { Rel(sample); return false; }
        if (!sample) continue;                      // a gap in the stream: ask again
        IMFMediaBuffer* buffer = nullptr;
        bool got = false;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
            BYTE* locked = nullptr;
            DWORD maxLen = 0, curLen = 0;
            if (SUCCEEDED(buffer->Lock(&locked, &maxLen, &curLen)) && locked) {
                // the rows can be padded (270 pixels wide: 288 in memory), so the row length is the buffer's size over the height
                size_t pitch = curLen >= (DWORD)w * 4 * h ? (size_t)curLen / (size_t)h : (size_t)w * 4;
                out.resize((size_t)w * h * 4);
                for (int y = 0; y < h; y++) {
                    const BYTE* src = locked + (bottomUp ? (size_t)(h - 1 - y) : (size_t)y) * pitch;
                    uint8_t* dst = &out[(size_t)y * w * 4];
                    for (int x = 0; x < w; x++) { dst[x * 4] = src[x * 4 + 2]; dst[x * 4 + 1] = src[x * 4 + 1]; dst[x * 4 + 2] = src[x * 4]; dst[x * 4 + 3] = 255; }   // BGRA -> RGBA
                }
                buffer->Unlock();
                got = true;
            }
        }
        Rel(buffer);
        Rel(sample);
        if (got) { time = (double)stamp / 1.0e7; return true; }
    }
}

bool VideoPlayer::AdvanceSystem(double dt) {
    MfState* m = (MfState*)mf_;
    clock_ += dt;
    bool changed = false;
    for (;;) {
        if (!havePending_) {
            if (!ReadPicture(m->reader, width_, height_, m->bottomUp, pending_, pendingTime_)) { finished_ = true; break; }
            havePending_ = true;
        }
        if (pendingTime_ > clock_) break;
        rgba_.swap(pending_);
        havePending_ = false;
        changed = true;
        decoded_++;
    }
    return changed;
}
}  // namespace ab
#else
namespace ab {
void VideoPlayer::CloseSystem() {}
bool VideoPlayer::OpenSystem(const std::string&, std::string*) { return false; }
bool VideoPlayer::AdvanceSystem(double) { return false; }
}  // namespace ab
#endif
