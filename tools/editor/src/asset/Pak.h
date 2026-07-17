#pragma once

#include <cstdint>
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

// M9c17 in-memory mount. A second runtime path next to the
// extract-to-disk flow: Mount() reads the pak into RAM +
// builds a path -> (offset, size, flags) lookup; Read()
// returns a copy of the decoded bytes for one entry.
// Callers that already have an Assets/ tree on disk (the
// editor, extracted standalone) hit the file system; those
// that don't (in-memory standalone builds) can fall back to
// Read() before erroring out.
//
// Mount + IsMounted + Unmount are not thread-safe -- the
// runtime only touches them on the main thread (startup +
// shutdown), so a mutex would be wasted overhead.
bool MountPak(const std::string& pakPath);
void UnmountPak();
bool IsPakMounted();
// Returns true when the path is present in the currently
// mounted pak + writes the decoded bytes into `out`. False
// when nothing is mounted, the path isn't in the TOC, or
// decompression fails. `relPath` is the same forward-slash
// Assets/-relative key the pak's TOC stores.
bool ReadFromPak(const std::string& relPath,
                  std::vector<uint8_t>& out);

} // namespace SceneEditor
