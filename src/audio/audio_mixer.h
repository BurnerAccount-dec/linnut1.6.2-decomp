// The exe's audio mixer (audio::AudioMixer, source\audio\AudioMixer.cpp), without DirectSound.
//
// The original mixes in software and hands the result to a DirectSound streaming buffer; this class
// is the same mixer, and whoever owns the sound device (the window layer, through SDL) calls Mix()
// to fetch the next block. What was read from the exe:
//   play        0x4ebba0   (clip, volume, loop, track) -> voice id, or -1 when the output is not started,
//                          the track is above 8, or the track already has `limit` voices (default -1 = none).
//                          The id counter (+0x80) starts at 0. The voice waits in a second list until the next mix.
//   mix16       0x4eb3c0   voices from the waiting list join the active list and stopped ones are dropped; for each
//                          voice gain = ftol(trackVolume[track] * volume * 4096), every sample is added as
//                          (sample * gain) >> 12, a mono clip goes to both channels, the sum is clamped to 16 bits.
//   readFully   0x4ec010   a looping voice restarts at its beginning until the block is full; a voice that
//                          reads 0 bytes without looping is marked stopped (so it lives one block longer than its data).
//   track volume 0x4eb770  0..8, clamped to 0..1, default 1.     track limit 0x4eb880 (default -1)
//   isActive    0x4ea670   any voice of the clip that is not stopped (active or waiting).
//   stop        0x4ea7f0   marks that clip's voices stopped; stopAll 0x4ea8d0 marks all.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

namespace ab {

constexpr int kAudioTracks = 9;

struct AudioClip {
    int channels = 0;
    int rate = 0;
    std::vector<int16_t> samples;     // interleaved
};

// WAV (PCM) or MP3, decided by the extension. The clip is converted to 16 bit; false with a message on failure.
bool LoadAudioClip(const std::string& path, AudioClip& out, std::string* error);

class AudioMixer {
public:
    // createAudioOutput(channels, bits, rate): the scripts always ask for 2, 16, 44100
    AudioMixer() = default;
    ~AudioMixer();
    void CreateOutput(int channels, int bits, int rate);
    void Start() { std::lock_guard<std::mutex> l(m_); started_ = true; }      // +0x84
    void Stop() { std::lock_guard<std::mutex> l(m_); started_ = false; }
    bool Created() const { std::lock_guard<std::mutex> l(m_); return created_; }
    bool Started() const { std::lock_guard<std::mutex> l(m_); return started_; }
    int Rate() const { return rate_; }
    int Channels() const { return channels_; }
    // Stereo at outRate for the sound device. The mixer itself runs at the rate and channel count the scripts
    // asked for with createAudioOutput (Seasons: mono, 16000 Hz); this converts with cubic interpolation.
    void MixStereo(int16_t* out, int frames, int outRate);

    // The clip is read from disk the first time it is played (the scripts register several hundred).
    void AddClip(const std::string& name, const std::string& path);
    int Play(const std::string& name, float volume, bool loop, int track);
    void Stop(const std::string& name);
    void StopAll();
    bool IsActive(const std::string& name);
    void SetTrackVolume(float volume, int track);
    float TrackVolume(int track) const { return track >= 0 && track < kAudioTracks ? trackVolume_[track] : 0; }

    // Fills `frames` frames of interleaved 16 bit samples (silence when nothing plays).
    void Mix(int16_t* out, int frames);

    // The message of the last clip that could not be loaded (and cleared); empty when there is none.
    std::string TakeError() { std::lock_guard<std::mutex> l(m_); std::string e; e.swap(lastError_); return e; }
    // Counts for the diagnostics: plays asked for, plays refused, voices mixed in the last block.
    int playsAsked = 0, playsRefused = 0;

private:
    struct Entry { std::string path; std::shared_ptr<AudioClip> clip; bool failed = false; };
    struct Voice {
        std::shared_ptr<AudioClip> clip;
        std::string name;
        size_t pos = 0;            // in samples
        float volume = 1;
        int id = 0, track = 0;
        bool loop = false, stopped = false;
    };
    std::shared_ptr<AudioClip> ClipFor(Entry& e);
    std::shared_ptr<AudioClip> Decode(const std::string& path, int rate, std::string* error);
    void PreloadLoop();                  // decodes the WAV clips in the background so that the first play does not stall the frame
    std::thread preload_;
    std::condition_variable wake_;
    std::deque<std::string> toLoad_;
    bool quit_ = false;
    int VoicesOnTrack(int track) const;

    std::string lastError_;
    mutable std::mutex m_;
    int channels_ = 2, bits_ = 16, rate_ = 44100;
    bool started_ = false, created_ = false;
    int nextId_ = 0;
    float trackVolume_[kAudioTracks] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
    int trackLimit_[kAudioTracks] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
    std::map<std::string, Entry> clips_;
    std::vector<Voice> active_, waiting_;
    std::vector<int> sum_;
    std::vector<float> feed_;            // MixStereo: mixed frames not yet used (interleaved, channels_ wide)
    double feedPos_ = 1;
    int feedCh_ = 0, feedRate_ = 0;
};

}  // namespace ab
