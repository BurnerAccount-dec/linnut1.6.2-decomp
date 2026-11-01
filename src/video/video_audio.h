// v2.2: the sound track of a video file as 16 bit stereo PCM. On Windows the AAC decoder that comes with the system (Media Foundation)
// does it; other systems have no decoder here and the video plays silently.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ab {
bool DecodeVideoAudio(const std::string& path, std::vector<int16_t>& pcm, int& rate, int& channels, std::string* error);
}
