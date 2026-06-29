#pragma once

#include <string>
#include <vector>

namespace SceneEditor {

// M9c13 NaraGame Pak archive (single-file asset bundle).
//
// Layout (little-endian throughout):
//   Header (16 bytes):
//     - magic "NPAK"             (4 bytes)
//     - version = 1              (uint32)
//     - num_entries              (uint32)
//     - data_offset              (uint32, byte offset to the
//                                 start of the data blob from
//                                 the file's start)
//   TOC (num_entries records):
//     - path_len                 (uint16)
//     - path bytes (utf-8)       (path_len)
//     - size                     (uint32)
//     - offset                   (uint32, from start of data
//                                 section, not file start)
//   Data section:
//     - raw file bytes concatenated in TOC order; no headers,
//       no compression (the M9c13 baseline -- compression is
//       future-work).
//
// Paths are stored with forward slashes + relative to the
// project's Assets/ root, so the extractor reconstructs the
// same tree structure the asset DB expects.

bool WritePak(const std::string& outPath,
              const std::string& assetsRoot);

bool ExtractPak(const std::string& pakPath,
                 const std::string& destDir);

} // namespace SceneEditor
