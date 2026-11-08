// Opening an Android .apk: an .apk is a zip file, the game's data is in assets/data. The data is unpacked once into a cache folder
// (the engine's own data layout, so the normal start-up runs on it); the textures the game keeps as .pvr.zip inside it are unpacked too.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ab {

bool Inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out);      // raw deflate (png_decoder.cpp)

// Unpacks assets/data of the .apk into <outDir>/data. Returns false (and a message) when the file is not a readable zip or has no assets/data.
// A folder that already holds the marker file .extracted is not unpacked again.
bool ExtractApk(const std::string& apkPath, const std::string& outDir, std::string* error);

}  // namespace ab
