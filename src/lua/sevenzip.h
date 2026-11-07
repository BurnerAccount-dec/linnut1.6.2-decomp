// A small 7z reader: Angry Birds Star Wars scripts are AES-encrypted 7z archives holding one compiled script (LZMA).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ab {

// Unpacks the first file of a 7z archive made with the LZMA coder (the header may itself be LZMA-packed). Returns false for anything else.
bool SevenZipExtractFirst(const std::vector<uint8_t>& archive, std::vector<uint8_t>& out);

// LZMA ("lzma alone" without the 13 byte header): props = the 5 byte properties, output size known.
bool LzmaDecode(const uint8_t* props, const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize);

}  // namespace ab
