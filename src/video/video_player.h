// v2.2: the video of playVideo, played inside the engine. An MP4 / M4V file is read here (the boxes of the video track), its H.264
// (baseline, as the iOS Angry Birds Free / Lite videos are) is decoded by h264bsd (third_party/h264bsd, Apache-2.0 / MIT) and the
// frames come out as RGBA. The sound of the file is decoded separately (video_audio.h).
#pragma once
#include <cstdint>
#include <string>
#include <vector>


namespace ab {

class VideoPlayer {
public:
    VideoPlayer() = default;
    ~VideoPlayer();
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    bool Open(const std::string& path, std::string* error);
    int Width() const { return width_; }
    int Height() const { return height_; }
    double Duration() const { return duration_; }
    double Time() const { return clock_; }
    // Moves the clock on by dt seconds (frames are decoded up to there). True when the picture changed.
    bool Advance(double dt);
    bool Finished() const { return finished_; }
    const std::vector<uint8_t>& Rgba() const { return rgba_; }       // width * height * 4, top row first
    int FramesDecoded() const { return decoded_; }
    bool UsesSystemDecoder() const { return mf_ != nullptr; }

private:
    struct Sample { uint64_t offset; uint32_t size; double time; bool sync; };
    bool Parse(std::string* error);
    bool DecodeSample(size_t index);
    // Windows: Media Foundation (the decoder that comes with the system) decodes every H.264 profile, h264bsd only the baseline one
    bool OpenSystem(const std::string& path, std::string* error);
    bool AdvanceSystem(double dt);
    void CloseSystem();
    void* mf_ = nullptr;
    std::vector<uint8_t> pending_;
    double pendingTime_ = 0;
    bool havePending_ = false;
    std::vector<uint8_t> file_;
    std::vector<Sample> samples_;
    std::vector<std::vector<uint8_t>> parameterSets_;     // SPS and PPS from the avcC box
    int nalLength_ = 4;
    int width_ = 0, height_ = 0;
    int codedW_ = 0, codedH_ = 0;             // the decoded picture: whole macroblocks
    double duration_ = 0, clock_ = 0;
    size_t next_ = 0;
    int decoded_ = 0;
    bool finished_ = false, sentParameters_ = false;
    void* decoder_ = nullptr;                // h264bsd's storage_t
    std::vector<uint8_t> rgba_;
};

}  // namespace ab
