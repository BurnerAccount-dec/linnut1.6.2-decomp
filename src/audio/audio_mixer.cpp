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
    wake_.notify_all();
}

AudioMixer::~AudioMixer() {
    { std::lock_guard<std::mutex> l(m_); quit_ = true; }
    wake_.notify_all();
    if (preload_.joinable()) preload_.join();
}

void AudioMixer::AddClip(const std::string& name, const std::string& path) {
    std::lock_guard<std::mutex> l(m_);
    Entry e;
    e.path = path;
    clips_[name] = e;
    if (EndsWith(path, ".wav")) {                    // short sounds are decoded in the background (music is decoded when it is played)
        toLoad_.push_back(name);
        if (!preload_.joinable()) preload_ = std::thread(&AudioMixer::PreloadLoop, this);
        wake_.notify_one();
    }
}


// v2.2: a clip that was decoded elsewhere (the sound of a video): resampled to the output rate like any other clip
void AudioMixer::AddPcmClip(const std::string& name, int rate, int channels, const std::vector<int16_t>& samples) {
    if (channels < 1 || rate < 1) return;
    auto clip = std::make_shared<AudioClip>();
    clip->channels = channels; clip->rate = rate; clip->samples = samples;
    int target;
    { std::lock_guard<std::mutex> l(m_); target = rate_; }
    if (rate != target) {
        size_t inFrames = samples.size() / channels, outFrames = (size_t)((double)inFrames * target / rate);
        std::vector<int16_t> r(outFrames * channels);
        for (size_t i = 0; i < outFrames && inFrames > 0; i++) {
            double src = (double)i * rate / target;
            size_t a = (size_t)src, b = std::min(a + 1, inFrames - 1);
            float t = (float)(src - a);
            for (int c = 0; c < channels; c++) r[i * channels + c] = (int16_t)(samples[a * channels + c] * (1 - t) + samples[b * channels + c] * t);
        }
        clip->samples.swap(r); clip->rate = target;
    }
    std::lock_guard<std::mutex> l(m_);
    Entry e;
    e.clip = clip;
    clips_[name] = e;
}

void AudioMixer::PreloadLoop() {
    std::unique_lock<std::mutex> l(m_);
    for (;;) {
        wake_.wait(l, [this] { return quit_ || (created_ && !toLoad_.empty()); });
        if (quit_) return;
        std::string name = toLoad_.front();
        toLoad_.pop_front();
        auto it = clips_.find(name);
        if (it == clips_.end() || it->second.clip || it->second.failed) continue;
        std::string path = it->second.path, error;
        int rate = rate_;
        l.unlock();
        std::shared_ptr<AudioClip> clip = Decode(path, rate, &error);       // the slow part runs without the lock
        l.lock();
        it = clips_.find(name);
        if (it == clips_.end() || it->second.clip || it->second.path != path) continue;
        if (clip) it->second.clip = clip;
        else { it->second.failed = true; lastError_ = error; }
    }
}

std::shared_ptr<AudioClip> AudioMixer::Decode(const std::string& path, int rate, std::string* error) {
    auto clip = std::make_shared<AudioClip>();
    if (!LoadAudioClip(path, *clip, error)) return nullptr;
    // The exe plays a clip as it is. A clip whose rate differs from the output's is resampled here (linear).
    if (clip->rate != rate && clip->rate > 0) {
        int ch = clip->channels;
        size_t inFrames = clip->samples.size() / ch;
        size_t outFrames = (size_t)((double)inFrames * rate / clip->rate);
        std::vector<int16_t> r(outFrames * ch);
        for (size_t i = 0; i < outFrames; i++) {
            double src = (double)i * clip->rate / rate;
            size_t a = (size_t)src, b = std::min(a + 1, inFrames - 1);
            float t = (float)(src - a);
            for (int c = 0; c < ch; c++)
                r[i * ch + c] = (int16_t)(clip->samples[a * ch + c] * (1 - t) + clip->samples[b * ch + c] * t);
        }
        clip->samples.swap(r);
        clip->rate = rate;
    }
    return clip;
}

std::shared_ptr<AudioClip> AudioMixer::ClipFor(Entry& e) {
    if (e.clip || e.failed) return e.clip;
    std::string error;
    std::shared_ptr<AudioClip> clip = Decode(e.path, rate_, &error);
    if (!clip) { e.failed = true; lastError_ = error; return nullptr; }
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
    auto fx = fx_.find(name);
    if (fx != fx_.end()) { v.pitch = fx->second.pitch; v.tempo = fx->second.tempo; v.pan = fx->second.pan; }
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


// ---- v2.2 playback controls
void AudioMixer::SetSpeed(float speed, int track) {
    std::lock_guard<std::mutex> l(m_);
    speed = std::min(8.0f, std::max(0.05f, speed));
    if (track < 0) masterSpeed_ = speed;
    else if (track < kAudioTracks) trackSpeed_[track] = speed;
}
float AudioMixer::Speed(int track) const {
    std::lock_guard<std::mutex> l(m_);
    return track < 0 ? masterSpeed_ : track < kAudioTracks ? trackSpeed_[track] : 1.0f;
}
void AudioMixer::Pause(bool paused, int track) {
    std::lock_guard<std::mutex> l(m_);
    if (track < 0) paused_ = paused;
    else if (track < kAudioTracks) trackPaused_[track] = paused;
}
bool AudioMixer::Paused(int track) const {
    std::lock_guard<std::mutex> l(m_);
    return track < 0 ? paused_ : track < kAudioTracks ? trackPaused_[track] : false;
}
void AudioMixer::SetMasterVolume(float v) {
    std::lock_guard<std::mutex> l(m_);
    masterVolume_ = std::min(4.0f, std::max(0.0f, v));
}
void AudioMixer::SetClipVolume(const std::string& name, float volume) {
    std::lock_guard<std::mutex> l(m_);
    for (Voice& v : active_) if (v.name == name) v.volume = volume;
    for (Voice& v : waiting_) if (v.name == name) v.volume = volume;
}
void AudioMixer::SetClipSpeed(const std::string& name, float speed) {
    std::lock_guard<std::mutex> l(m_);
    speed = std::min(8.0f, std::max(0.05f, speed));
    for (Voice& v : active_) if (v.name == name) v.speed = speed;
    for (Voice& v : waiting_) if (v.name == name) v.speed = speed;
}

template <class F> void AudioMixer::EachVoice(const std::string& name, F f) {
    for (Voice& v : active_) if (v.name == name) f(v);
    for (Voice& v : waiting_) if (v.name == name) f(v);
}
void AudioMixer::SetClipPitch(const std::string& name, float p) {
    std::lock_guard<std::mutex> l(m_);
    p = std::min(8.0f, std::max(0.05f, p));
    fx_[name].pitch = p; EachVoice(name, [&](Voice& v) { v.pitch = p; });
}
void AudioMixer::SetClipTempo(const std::string& name, float t) {
    std::lock_guard<std::mutex> l(m_);
    t = std::min(8.0f, std::max(0.05f, t));
    fx_[name].tempo = t; EachVoice(name, [&](Voice& v) { v.tempo = t; });
}
void AudioMixer::SetClipPan(const std::string& name, float p) {
    std::lock_guard<std::mutex> l(m_);
    p = std::min(1.0f, std::max(-1.0f, p));
    fx_[name].pan = p; EachVoice(name, [&](Voice& v) { v.pan = p; });
}
void AudioMixer::ResetClipEffects(const std::string& name) {
    std::lock_guard<std::mutex> l(m_);
    fx_.erase(name); EachVoice(name, [&](Voice& v) { v.pitch = 1; v.tempo = 1; v.pan = 0; });
}
bool AudioMixer::SeekClip(const std::string& name, double seconds) {
    std::lock_guard<std::mutex> l(m_);
    bool any = false;
    EachVoice(name, [&](Voice& v) {
        double frame = std::max(0.0, seconds * v.clip->rate);
        int cc = std::max(1, v.clip->channels);
        size_t frames = v.clip->samples.size() / cc;
        if (frame >= (double)frames) frame = frames ? (double)frames - 1 : 0;
        v.pos = (size_t)frame * cc; v.frac = 0; v.posD = v.base = frame; v.outCount = 0; any = true;
    });
    return any;
}
double AudioMixer::ClipTime(const std::string& name) {
    std::lock_guard<std::mutex> l(m_);
    double t = -1;
    EachVoice(name, [&](Voice& v) {
        if (t >= 0) return;
        int cc = std::max(1, v.clip->channels);
        double frame = v.inFx ? (v.lastTf != 0 ? v.base + (double)v.outCount * v.lastTf : v.posD) : (double)(v.pos / cc) + v.frac;
        t = frame / std::max(1, v.clip->rate);
    });
    return t;
}
double AudioMixer::ClipLength(const std::string& name) {
    std::lock_guard<std::mutex> l(m_);
    double t = -1;
    EachVoice(name, [&](Voice& v) { if (t < 0) t = (double)(v.clip->samples.size() / std::max(1, v.clip->channels)) / std::max(1, v.clip->rate); });
    if (t >= 0) return t;
    auto it = clips_.find(name);
    if (it != clips_.end() && it->second.clip) return (double)(it->second.clip->samples.size() / std::max(1, it->second.clip->channels)) / std::max(1, it->second.clip->rate);
    return -1;
}


// v2.4: the voice with a pitch, tempo or pan set. Same pitch and tempo: plain resampling. Different: two overlapping Hann grains (overlap-add), which
// changes the length without changing the pitch.
void AudioMixer::MixFx(Voice& v, int frames, float speed) {
    const int ch = channels_;
    const int cc = std::max(1, v.clip->channels);
    const std::vector<int16_t>& s = v.clip->samples;
    const long long clipFrames = (long long)(s.size() / cc);
    if (clipFrames == 0) { v.stopped = true; return; }
    if (!v.inFx) { v.posD = (double)(v.pos / cc) + v.frac; v.base = v.posD; v.outCount = 0; v.inFx = true; }
    const double pf = speed * v.pitch, tf = speed * v.tempo;
    const bool stretch = std::fabs(pf - tf) > 1e-4;
    const float gain = trackVolume_[v.track] * v.volume * masterVolume_;
    const float gl = v.pan > 0 ? 1.0f - v.pan : 1.0f, gr = v.pan < 0 ? 1.0f + v.pan : 1.0f;
    auto at = [&](double p, int c) -> float {
        if (p < 0) return 0;
        if (v.loop) p = std::fmod(p, (double)clipFrames);
        long long i0 = (long long)p;
        if (i0 >= clipFrames) return 0;
        long long i1 = i0 + 1; if (i1 >= clipFrames) i1 = v.loop ? 0 : i0;
        float t = (float)(p - (double)i0);
        float a = s[(size_t)i0 * cc + (cc == 1 ? 0 : c)], b = s[(size_t)i1 * cc + (cc == 1 ? 0 : c)];
        return a + (b - a) * t;
    };
    const int N = 2048, H = 1024;
    v.lastTf = stretch ? tf : 0;
    for (int i = 0; i < frames; i++) {
        double anchor = stretch ? v.base + (double)v.outCount * tf : v.posD;
        if (!v.loop && anchor >= (double)clipFrames) { v.stopped = true; break; }
        for (int k = 0; k < ch; k++) {
            int c = ch == 1 ? 0 : k;
            float val;
            if (!stretch) val = at(v.posD, c);
            else {
                long long g = (long long)(v.outCount / (unsigned long long)H);
                val = 0;
                for (long long gi = g - 1; gi <= g; gi++) {
                    long long j = (long long)v.outCount - gi * H;
                    float w = std::sin(3.14159265f * ((float)j + 0.5f) / (float)N); w *= w;
                    val += w * at(v.base + (double)(gi * H) * tf + (double)j * pf, c);
                }
            }
            float side = ch == 2 ? (k == 0 ? gl : gr) : 1.0f;
            sum_[(size_t)i * ch + k] += (int)(val * gain * side);
        }
        v.outCount++;
        v.posD += pf;
        if (v.loop && v.posD >= (double)clipFrames) v.posD = std::fmod(v.posD, (double)clipFrames);
    }
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
        int gain = (int)(trackVolume_[v.track] * v.volume * masterVolume_ * 4096.0);
        const std::vector<int16_t>& s = v.clip->samples;
        float speed = masterSpeed_ * trackSpeed_[v.track] * v.speed;
        bool paused = paused_ || trackPaused_[v.track];
        if (paused) continue;
        if (v.pitch != 1.0f || v.tempo != 1.0f || v.pan != 0.0f) { MixFx(v, frames, speed); continue; }
        if (v.inFx) {      // the effects were reset: hand the position back to the plain paths
            int cc0 = std::max(1, v.clip->channels);
            double p = v.lastTf != 0 ? v.base + (double)v.outCount * v.lastTf : v.posD;
            v.pos = (size_t)p * cc0; v.frac = p - std::floor(p); v.inFx = false;
        }
        if (speed != 1.0f || v.frac != 0) {
            // v2.2: resampled voice (linear interpolation), one output frame at a time
            int cc = v.clip->channels;
            size_t clipFrames = cc > 0 ? s.size() / cc : 0;
            int g = (int)(trackVolume_[v.track] * v.volume * masterVolume_ * 4096.0);
            for (int i = 0; i < frames; i++) {
                size_t fi = cc > 0 ? v.pos / cc : 0;
                if (fi >= clipFrames) {
                    if (!v.loop || clipFrames == 0) { v.stopped = true; break; }
                    fi %= clipFrames; v.pos = fi * cc;
                }
                size_t ni = fi + 1 < clipFrames ? fi + 1 : (v.loop ? 0 : fi);
                float t = (float)v.frac;
                for (int k = 0; k < ch; k++) {
                    int c = cc == 1 ? 0 : k;
                    float a = (float)s[fi * cc + c], b = (float)s[ni * cc + c];
                    sum_[(size_t)i * ch + k] += (int)((a + (b - a) * t) * g) >> 12;
                }
                v.frac += speed;
                size_t adv = (size_t)v.frac;
                v.frac -= (double)adv;
                v.pos += adv * cc;
            }
            continue;
        }
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

void AudioMixer::MixStereo(int16_t* out, int frames, int outRate) {
    int ch = channels_, rate = rate_;
    if (ch == 2 && rate == outRate) { Mix(out, frames); return; }
    if (ch != feedCh_ || rate != feedRate_) { feed_.assign((size_t)ch, 0.0f); feedPos_ = 1; feedCh_ = ch; feedRate_ = rate; }
    double step = (double)rate / outRate;
    for (int i = 0; i < frames; i++) {
        // the four source frames around the position: floor(pos) - 1 .. floor(pos) + 2
        size_t base = (size_t)feedPos_;
        while ((base + 3) * ch >= feed_.size()) {
            int16_t block[512];
            int n = 512 / ch;
            Mix(block, n);
            for (int k = 0; k < n * ch; k++) feed_.push_back(block[k] / 32768.0f);
        }
        float t = (float)(feedPos_ - base);
        for (int side = 0; side < 2; side++) {
            int c = ch == 2 ? side : 0;
            float p0 = feed_[(base - 1) * ch + c], p1 = feed_[base * ch + c], p2 = feed_[(base + 1) * ch + c], p3 = feed_[(base + 2) * ch + c];
            float v = p1 + 0.5f * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));   // Catmull-Rom
            out[i * 2 + side] = (int16_t)(std::min(1.0f, std::max(-1.0f, v)) * 32767.0f);
        }
        feedPos_ += step;
    }
    size_t drop = (size_t)feedPos_ - 1;                  // keep one frame before the position
    if (drop > 0) { feed_.erase(feed_.begin(), feed_.begin() + (std::ptrdiff_t)(drop * ch)); feedPos_ -= (double)drop; }
}

}  // namespace ab
