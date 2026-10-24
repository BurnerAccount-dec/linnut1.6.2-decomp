#ifndef NOMINMAX
#define NOMINMAX            // windows.h (pulled in by minimp3_ex.h) would turn std::min / std::max into macros
#endif
#include "audio/audio_mixer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"

// the decoder only needs the plain C parts; keep windows.h macros out of the code below
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace ab {

namespace {

uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool EndsWith(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (std::tolower((unsigned char)s[s.size() - n + i]) != suffix[i]) return false;
    return true;
}

// RIFF / WAVE with a "fmt " chunk (PCM, 8 or 16 bit) and a "data" chunk.
bool LoadWav(const std::vector<uint8_t>& f, AudioClip& out, std::string* error) {
    if (f.size() < 12 || std::memcmp(f.data(), "RIFF", 4) != 0 || std::memcmp(f.data() + 8, "WAVE", 4) != 0) {
        if (error) *error = "not a RIFF WAVE file";
        return false;
    }
    int format = 0, bits = 0;
    size_t pos = 12;
    while (pos + 8 <= f.size()) {
        uint32_t size = U32(&f[pos + 4]);
        const uint8_t* body = &f[pos + 8];
        size_t avail = std::min<size_t>(size, f.size() - pos - 8);
        if (!std::memcmp(&f[pos], "fmt ", 4) && avail >= 16) {
            format = U16(body);
            out.channels = U16(body + 2);
            out.rate = (int)U32(body + 4);
            bits = U16(body + 14);
        } else if (!std::memcmp(&f[pos], "data", 4)) {
            if (format != 1 || (bits != 8 && bits != 16) || out.channels < 1 || out.channels > 2) {
                if (error) *error = "WAV was not PCM (8 or 16 bit, mono or stereo)";
                return false;
            }
            if (bits == 16) {
                out.samples.resize(avail / 2);
                for (size_t i = 0; i < out.samples.size(); i++) out.samples[i] = (int16_t)U16(body + i * 2);
            } else {
                out.samples.resize(avail);
                for (size_t i = 0; i < avail; i++) out.samples[i] = (int16_t)((body[i] - 128) << 8);
            }
            return true;
        }
        pos += 8 + size + (size & 1);
    }
    if (error) *error = "WAV has no data chunk";
    return false;
}

}  // namespace

bool LoadAudioClip(const std::string& path, AudioClip& out, std::string* error) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) { if (error) *error = "cannot open " + path; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (EndsWith(path, ".mp3")) {
        mp3dec_t dec;
        mp3dec_file_info_t info;
        std::memset(&info, 0, sizeof(info));
        if (mp3dec_load_buf(&dec, bytes.data(), bytes.size(), &info, nullptr, nullptr) != 0 || !info.buffer || info.samples == 0) {
            if (error) *error = "cannot decode " + path;
            std::free(info.buffer);
            return false;
        }
        out.channels = info.channels;
        out.rate = info.hz;
        out.samples.assign(info.buffer, info.buffer + info.samples);
        std::free(info.buffer);
        return true;
    }
    if (!LoadWav(bytes, out, error)) {
        if (error) *error = path + ": " + *error;
        return false;
    }
    return true;
}

void AudioMixer::CreateOutput(int channels, int bits, int rate) {
    std::lock_guard<std::mutex> l(m_);
    channels_ = channels; bits_ = bits; rate_ = rate;
    created_ = true;
}

void AudioMixer::AddClip(const std::string& name, const std::string& path) {
    std::lock_guard<std::mutex> l(m_);
    Entry e;
    e.path = path;
    clips_[name] = e;
}

std::shared_ptr<AudioClip> AudioMixer::ClipFor(Entry& e) {
    if (e.clip || e.failed) return e.clip;
    auto clip = std::make_shared<AudioClip>();
    std::string error;
    if (!LoadAudioClip(e.path, *clip, &error)) { e.failed = true; lastError_ = error; return nullptr; }
    // The exe plays a clip as it is. A clip whose rate differs from the output's is resampled here (linear).
    if (clip->rate != rate_ && clip->rate > 0) {
        int ch = clip->channels;
        size_t inFrames = clip->samples.size() / ch;
        size_t outFrames = (size_t)((double)inFrames * rate_ / clip->rate);
        std::vector<int16_t> r(outFrames * ch);
        for (size_t i = 0; i < outFrames; i++) {
            double src = (double)i * clip->rate / rate_;
            size_t a = (size_t)src, b = std::min(a + 1, inFrames - 1);
            float t = (float)(src - a);
            for (int c = 0; c < ch; c++)
                r[i * ch + c] = (int16_t)(clip->samples[a * ch + c] * (1 - t) + clip->samples[b * ch + c] * t);
        }
        clip->samples.swap(r);
        clip->rate = rate_;
    }
    e.clip = clip;
    return clip;
}

int AudioMixer::VoicesOnTrack(int track) const {
    int n = 0;
    for (const Voice& v : active_) if (v.track == track && !v.stopped) n++;
    for (const Voice& v : waiting_) if (v.track == track && !v.stopped) n++;
    return n;
}

int AudioMixer::Play(const std::string& name, float volume, bool loop, int track) {
    std::lock_guard<std::mutex> l(m_);
    playsAsked++;
    if (!started_ || track < 0 || track >= kAudioTracks) { playsRefused++; return -1; }
    auto it = clips_.find(name);
    if (it == clips_.end()) { playsRefused++; return -1; }      // the exe ignores a name that was never registered
    if (trackLimit_[track] >= 0 && VoicesOnTrack(track) >= trackLimit_[track]) return -1;
    std::shared_ptr<AudioClip> clip = ClipFor(it->second);
    if (!clip) { playsRefused++; return -1; }
    Voice v;
    v.clip = clip; v.name = name; v.volume = volume; v.loop = loop; v.track = track;
    v.id = nextId_++;
    waiting_.push_back(v);
    return v.id;
}

void AudioMixer::Stop(const std::string& name) {
    std::lock_guard<std::mutex> l(m_);
    for (Voice& v : active_) if (v.name == name) v.stopped = true;
    for (Voice& v : waiting_) if (v.name == name) v.stopped = true;
}

void AudioMixer::StopAll() {
    std::lock_guard<std::mutex> l(m_);
    for (Voice& v : active_) v.stopped = true;
    for (Voice& v : waiting_) v.stopped = true;
}

bool AudioMixer::IsActive(const std::string& name) {
    std::lock_guard<std::mutex> l(m_);
    for (const Voice& v : active_) if (v.name == name && !v.stopped) return true;
    for (const Voice& v : waiting_) if (v.name == name && !v.stopped) return true;
    return false;
}

void AudioMixer::SetTrackVolume(float volume, int track) {
    std::lock_guard<std::mutex> l(m_);
    if (track < 0 || track >= kAudioTracks) return;
    trackVolume_[track] = std::min(1.0f, std::max(0.0f, volume));
}

void AudioMixer::Mix(int16_t* out, int frames) {
    std::lock_guard<std::mutex> l(m_);
    int ch = channels_;
    size_t count = (size_t)frames * ch;
    sum_.assign(count, 0);
    // processTracks: the waiting voices join, stopped ones leave
    for (const Voice& v : waiting_) active_.push_back(v);
    waiting_.clear();
    active_.erase(std::remove_if(active_.begin(), active_.end(), [](const Voice& v) { return v.stopped; }), active_.end());

    for (Voice& v : active_) {
        int gain = (int)(trackVolume_[v.track] * v.volume * 4096.0);
        const std::vector<int16_t>& s = v.clip->samples;
        bool mono = ch == 2 && v.clip->channels == 1;
        size_t need = mono ? count / 2 : count;           // samples to read from the clip
        size_t got = 0;
        size_t outPos = 0;
        while (got < need) {
            size_t left = s.size() - v.pos;
            if (left == 0) {
                if (!v.loop) { v.stopped = true; break; }       // read nothing: the voice ends
                v.pos = 0;
                if (s.empty()) break;
                continue;
            }
            size_t n = std::min(left, need - got);
            for (size_t i = 0; i < n; i++) {
                int x = (s[v.pos + i] * gain) >> 12;
                if (mono) { sum_[outPos++] += x; sum_[outPos++] += x; }
                else sum_[outPos++] += x;
            }
            v.pos += n; got += n;
            if (!v.loop) break;                                  // a one-shot reads once per block
        }
    }
    for (size_t i = 0; i < count; i++) out[i] = (int16_t)std::min(32767, std::max(-32768, sum_[i]));
}

}  // namespace ab
