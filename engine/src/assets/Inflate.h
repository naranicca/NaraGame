#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nara::detail {

// Decompress a raw DEFLATE stream (RFC 1951). Returns false on malformed input.
bool Inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out);

// Decompress a zlib stream (RFC 1950): 2-byte header + DEFLATE + Adler-32.
// The trailing checksum is not verified.
bool ZlibInflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out);

} // namespace nara::detail
