#include "Pak.h"

#include <NaraGame/core/Log.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <unordered_map>

#if defined(NARAGAME_HAS_MINIZ) && NARAGAME_HAS_MINIZ
#include "miniz.h"
#endif

namespace SceneEditor {

namespace fs = std::filesystem;

namespace {

// M9c17 mount state. One pak at a time -- swapping projects in
// the editor is rare + a packed game has exactly one bundle.
struct MountEntry {
    uint32_t offset;
    uint32_t size;
    uint32_t compressedSize;
    uint8_t  flags;
};
std::vector<uint8_t>                          g_mountBlob;
std::unordered_map<std::string, MountEntry>   g_mountMap;

} // namespace

namespace {

constexpr char     kMagic[4] = { 'N', 'P', 'A', 'K' };
// M9c14 bumped to 2: TOC entries grew a compressedSize +
// flags byte so each blob can be either stored (flags=0) or
// deflated (flags=1). Readers must check the version + reject
// older paks rather than mis-parse them.
constexpr uint32_t kVersion  = 2;

constexpr uint8_t  kFlagStored   = 0;
constexpr uint8_t  kFlagDeflated = 1;

// Files below this size + extensions that are already
// compressed get stored as-is. Skipping tiny entries removes
// the ~5 byte deflate header overhead; skipping pre-compressed
// formats avoids paying CPU for negligible (often negative)
// gains.
constexpr uint32_t kCompressMinBytes = 256;
bool ExtensionAlreadyCompressed(const std::string& path) {
    auto endsWith = [&](const char* suffix) {
        const size_t n = std::strlen(suffix);
        if (path.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            char a = path[path.size() - n + i];
            char b = suffix[i];
            if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    };
    return endsWith(".png") || endsWith(".jpg") ||
           endsWith(".jpeg") || endsWith(".ogg") ||
           endsWith(".mp3") || endsWith(".wav") ||
           endsWith(".pak");
}

void Write32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(uint8_t( v        & 0xFF));
    buf.push_back(uint8_t((v >>  8) & 0xFF));
    buf.push_back(uint8_t((v >> 16) & 0xFF));
    buf.push_back(uint8_t((v >> 24) & 0xFF));
}

void Write16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(uint8_t( v       & 0xFF));
    buf.push_back(uint8_t((v >> 8) & 0xFF));
}

bool Read32(FILE* f, uint32_t& v) {
    uint8_t b[4];
    if (std::fread(b, 1, 4, f) != 4) return false;
    v = uint32_t(b[0]) |
        (uint32_t(b[1]) <<  8) |
        (uint32_t(b[2]) << 16) |
        (uint32_t(b[3]) << 24);
    return true;
}

bool Read16(FILE* f, uint16_t& v) {
    uint8_t b[2];
    if (std::fread(b, 1, 2, f) != 2) return false;
    v = uint16_t(b[0]) | (uint16_t(b[1]) << 8);
    return true;
}

std::string Normalise(const fs::path& rel) {
    std::string s = rel.generic_string();
    return s;
}

} // namespace

bool WritePak(const std::string& outPath,
              const std::string& assetsRoot) {
    std::error_code ec;
    const fs::path root(assetsRoot);
    if (!fs::is_directory(root, ec)) {
        NARAGAME_ERROR("WritePak: '%s' is not a directory",
                       assetsRoot.c_str());
        return false;
    }

    // M9c14: load every source file in full + (optionally)
    // compress so the TOC knows each blob's compressed size
    // before the header is written. This forces O(total
    // Assets) RAM during pack, but a typical 2D project's
    // Assets/ is well under 1 GB. Stream-compressing into a
    // length-then-data layout would lift that cap; future
    // work.
    struct Entry {
        std::string          relPath;
        uint32_t             size;            // uncompressed
        uint32_t             compressedSize;  // == size when stored
        uint32_t             offset;          // in data section
        uint8_t              flags;
        std::vector<uint8_t> blob;            // payload as written
    };
    std::vector<Entry> entries;
    for (const auto& de :
         fs::recursive_directory_iterator(root, ec)) {
        if (ec) {
            NARAGAME_ERROR("WritePak: iteration failed (%s)",
                           ec.message().c_str());
            return false;
        }
        if (!de.is_regular_file()) continue;
        const fs::path rel = fs::relative(de.path(), root, ec);
        if (ec || rel.empty()) continue;
        const auto fsize = de.file_size(ec);
        if (ec) {
            NARAGAME_ERROR("WritePak: cannot size '%s' (%s)",
                           de.path().string().c_str(),
                           ec.message().c_str());
            return false;
        }
        if (fsize > UINT32_MAX) {
            NARAGAME_ERROR(
                "WritePak: '%s' exceeds 4 GB cap -- pak v2 "
                "uses 32-bit sizes.",
                de.path().string().c_str());
            return false;
        }
        Entry e;
        e.relPath        = Normalise(rel);
        e.size           = uint32_t(fsize);
        e.compressedSize = e.size;
        e.offset         = 0;
        e.flags          = kFlagStored;
        std::ifstream in(de.path().string(), std::ios::binary);
        if (!in) {
            NARAGAME_ERROR("WritePak: cannot read '%s'",
                           e.relPath.c_str());
            return false;
        }
        e.blob.resize(e.size);
        if (e.size > 0) {
            in.read(reinterpret_cast<char*>(&e.blob[0]),
                    std::streamsize(e.size));
            if (std::streamsize(in.gcount()) !=
                std::streamsize(e.size)) {
                NARAGAME_ERROR(
                    "WritePak: short read on '%s'",
                    e.relPath.c_str());
                return false;
            }
        }

        // M9c14 compression decision. Skip tiny entries +
        // already-compressed extensions; everything else
        // tries deflate + keeps the result only if it
        // actually shrunk the payload.
#if defined(NARAGAME_HAS_MINIZ) && NARAGAME_HAS_MINIZ
        if (e.size >= kCompressMinBytes &&
            !ExtensionAlreadyCompressed(e.relPath)) {
            mz_ulong bound = mz_compressBound(e.size);
            std::vector<uint8_t> compressed;
            compressed.resize(size_t(bound));
            mz_ulong destLen = bound;
            const int rc = mz_compress2(
                &compressed[0], &destLen,
                &e.blob[0], e.size,
                MZ_DEFAULT_LEVEL);
            if (rc == MZ_OK &&
                destLen > 0 &&
                destLen < e.size) {
                compressed.resize(size_t(destLen));
                e.blob           = std::move(compressed);
                e.compressedSize = uint32_t(destLen);
                e.flags          = kFlagDeflated;
            }
        }
#endif
        entries.push_back(std::move(e));
    }

    // Fill offsets so the data section is laid out in TOC
    // order with no gaps.
    uint32_t running = 0;
    for (Entry& e : entries) {
        e.offset = running;
        running += e.compressedSize;
    }

    // Header + TOC into an in-memory buffer first; only when
    // everything's computed do we touch the output file.
    std::vector<uint8_t> headerToc;
    headerToc.reserve(16 + entries.size() * 32);
    headerToc.insert(headerToc.end(), kMagic, kMagic + 4);
    Write32(headerToc, kVersion);
    Write32(headerToc, uint32_t(entries.size()));
    // data_offset placeholder -- patched after the TOC is built.
    const size_t dataOffsetPos = headerToc.size();
    Write32(headerToc, 0);
    for (const Entry& e : entries) {
        if (e.relPath.size() > UINT16_MAX) {
            NARAGAME_ERROR(
                "WritePak: path '%s' exceeds 64k bound.",
                e.relPath.c_str());
            return false;
        }
        Write16(headerToc, uint16_t(e.relPath.size()));
        headerToc.insert(headerToc.end(),
                          e.relPath.begin(), e.relPath.end());
        Write32(headerToc, e.size);
        Write32(headerToc, e.compressedSize);
        Write32(headerToc, e.offset);
        headerToc.push_back(e.flags);
    }
    const uint32_t dataOffset = uint32_t(headerToc.size());
    headerToc[dataOffsetPos + 0] = uint8_t( dataOffset        & 0xFF);
    headerToc[dataOffsetPos + 1] = uint8_t((dataOffset >>  8) & 0xFF);
    headerToc[dataOffsetPos + 2] = uint8_t((dataOffset >> 16) & 0xFF);
    headerToc[dataOffsetPos + 3] = uint8_t((dataOffset >> 24) & 0xFF);

    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        NARAGAME_ERROR("WritePak: cannot open '%s' for write",
                       outPath.c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(&headerToc[0]),
              std::streamsize(headerToc.size()));
    for (const Entry& e : entries) {
        if (e.compressedSize == 0) continue;
        out.write(reinterpret_cast<const char*>(&e.blob[0]),
                  std::streamsize(e.compressedSize));
    }
    return out.good();
}

bool ExtractPak(const std::string& pakPath,
                 const std::string& destDir) {
    FILE* f = nullptr;
#if defined(_MSC_VER)
    fopen_s(&f, pakPath.c_str(), "rb");
#else
    f = std::fopen(pakPath.c_str(), "rb");
#endif
    if (!f) {
        NARAGAME_ERROR("ExtractPak: cannot open '%s'",
                       pakPath.c_str());
        return false;
    }
    struct Closer {
        FILE* f;
        ~Closer() { if (f) std::fclose(f); }
    } closer{f};

    char magic[4];
    if (std::fread(magic, 1, 4, f) != 4 ||
        std::memcmp(magic, kMagic, 4) != 0) {
        NARAGAME_ERROR("ExtractPak: '%s' is not a NPAK file",
                       pakPath.c_str());
        return false;
    }
    uint32_t version = 0, numEntries = 0, dataOffset = 0;
    if (!Read32(f, version) || !Read32(f, numEntries) ||
        !Read32(f, dataOffset)) {
        NARAGAME_ERROR("ExtractPak: short header");
        return false;
    }
    if (version != kVersion) {
        NARAGAME_ERROR(
            "ExtractPak: version %u not supported (this build "
            "knows version %u).", version, kVersion);
        return false;
    }

    struct TocEntry {
        std::string path;
        uint32_t    size;
        uint32_t    compressedSize;
        uint32_t    offset;
        uint8_t     flags;
    };
    std::vector<TocEntry> toc;
    toc.reserve(numEntries);
    for (uint32_t i = 0; i < numEntries; ++i) {
        uint16_t plen = 0;
        if (!Read16(f, plen)) {
            NARAGAME_ERROR("ExtractPak: TOC truncated at entry %u", i);
            return false;
        }
        std::vector<char> pathBytes;
        pathBytes.resize(plen);
        char* pathPtr = pathBytes.empty() ? nullptr : &pathBytes[0];
        if (plen > 0 &&
            std::fread(pathPtr, 1, plen, f) != plen) {
            NARAGAME_ERROR("ExtractPak: TOC truncated at entry %u", i);
            return false;
        }
        TocEntry e;
        e.path.assign(pathBytes.begin(), pathBytes.end());
        if (!Read32(f, e.size) ||
            !Read32(f, e.compressedSize) ||
            !Read32(f, e.offset)) {
            NARAGAME_ERROR("ExtractPak: TOC truncated at entry %u", i);
            return false;
        }
        uint8_t flagsByte = 0;
        if (std::fread(&flagsByte, 1, 1, f) != 1) {
            NARAGAME_ERROR("ExtractPak: TOC truncated at entry %u", i);
            return false;
        }
        e.flags = flagsByte;
        toc.push_back(std::move(e));
    }

    std::error_code ec;
    fs::create_directories(destDir, ec);
    for (const TocEntry& e : toc) {
        const fs::path full = fs::path(destDir) / e.path;
        fs::create_directories(full.parent_path(), ec);
        if (std::fseek(f, long(dataOffset + e.offset), SEEK_SET)
            != 0) {
            NARAGAME_ERROR(
                "ExtractPak: seek failed for '%s'", e.path.c_str());
            return false;
        }
        std::vector<uint8_t> srcBuf;
        srcBuf.resize(e.compressedSize);
        if (e.compressedSize > 0 &&
            std::fread(&srcBuf[0], 1, e.compressedSize, f) !=
                e.compressedSize) {
            NARAGAME_ERROR(
                "ExtractPak: short read on '%s'",
                e.path.c_str());
            return false;
        }
        std::vector<uint8_t> outBuf;
        const uint8_t* writePtr = nullptr;
        size_t         writeLen = 0;
        if (e.flags == kFlagStored) {
            writePtr = srcBuf.empty() ? nullptr : &srcBuf[0];
            writeLen = srcBuf.size();
        } else if (e.flags == kFlagDeflated) {
#if defined(NARAGAME_HAS_MINIZ) && NARAGAME_HAS_MINIZ
            outBuf.resize(e.size);
            mz_ulong destLen = e.size;
            const int rc = mz_uncompress(
                outBuf.empty() ? nullptr : &outBuf[0], &destLen,
                srcBuf.empty() ? nullptr : &srcBuf[0],
                e.compressedSize);
            if (rc != MZ_OK || destLen != e.size) {
                NARAGAME_ERROR(
                    "ExtractPak: inflate failed for '%s' "
                    "(rc=%d, len=%lu/%u)",
                    e.path.c_str(), rc,
                    (unsigned long)destLen, e.size);
                return false;
            }
            writePtr = outBuf.empty() ? nullptr : &outBuf[0];
            writeLen = outBuf.size();
#else
            NARAGAME_ERROR(
                "ExtractPak: entry '%s' is deflated but this "
                "build of the editor lacks miniz support. "
                "Re-pack with a stored (uncompressed) writer "
                "or rebuild with third_party/miniz/.",
                e.path.c_str());
            return false;
#endif
        } else {
            NARAGAME_ERROR(
                "ExtractPak: unknown flags 0x%02X on '%s'",
                unsigned(e.flags), e.path.c_str());
            return false;
        }
        FILE* of = nullptr;
#if defined(_MSC_VER)
        fopen_s(&of, full.string().c_str(), "wb");
#else
        of = std::fopen(full.string().c_str(), "wb");
#endif
        if (!of) {
            NARAGAME_ERROR(
                "ExtractPak: cannot create '%s'",
                full.string().c_str());
            return false;
        }
        if (writeLen > 0 && writePtr) {
            std::fwrite(writePtr, 1, writeLen, of);
        }
        std::fclose(of);
    }
    return true;
}

// ---------------------------------------------------------
// M9c17 in-memory mount.
// ---------------------------------------------------------

bool MountPak(const std::string& pakPath) {
    UnmountPak();

    FILE* f = nullptr;
#if defined(_MSC_VER)
    fopen_s(&f, pakPath.c_str(), "rb");
#else
    f = std::fopen(pakPath.c_str(), "rb");
#endif
    if (!f) {
        NARAGAME_ERROR("MountPak: cannot open '%s'",
                       pakPath.c_str());
        return false;
    }
    struct Closer {
        FILE* f;
        ~Closer() { if (f) std::fclose(f); }
    } closer{f};

    // Determine the file size up front so the entire blob
    // ( header + TOC + data ) lands in g_mountBlob in one
    // read. A multi-MB pak's RAM footprint is acceptable;
    // streaming reads on demand would force a per-asset seek
    // which is what we just avoided by mounting.
    std::fseek(f, 0, SEEK_END);
    const long fsize = std::ftell(f);
    if (fsize < 16) {
        NARAGAME_ERROR("MountPak: '%s' too small to be NPAK",
                       pakPath.c_str());
        return false;
    }
    std::fseek(f, 0, SEEK_SET);
    g_mountBlob.resize(size_t(fsize));
    if (std::fread(&g_mountBlob[0], 1, size_t(fsize), f) !=
        size_t(fsize)) {
        NARAGAME_ERROR("MountPak: short read '%s'",
                       pakPath.c_str());
        g_mountBlob.clear();
        return false;
    }

    const uint8_t* p = &g_mountBlob[0];
    if (p[0] != 'N' || p[1] != 'P' || p[2] != 'A' || p[3] != 'K') {
        NARAGAME_ERROR("MountPak: '%s' is not NPAK",
                       pakPath.c_str());
        g_mountBlob.clear();
        return false;
    }
    auto rd32 = [&](size_t off) -> uint32_t {
        return uint32_t(p[off]) |
               (uint32_t(p[off + 1]) <<  8) |
               (uint32_t(p[off + 2]) << 16) |
               (uint32_t(p[off + 3]) << 24);
    };
    auto rd16 = [&](size_t off) -> uint16_t {
        return uint16_t(p[off]) |
               (uint16_t(p[off + 1]) << 8);
    };
    const uint32_t version    = rd32(4);
    const uint32_t numEntries = rd32(8);
    const uint32_t dataOffset = rd32(12);
    if (version != kVersion) {
        NARAGAME_ERROR(
            "MountPak: pak version %u not supported (build "
            "expects %u)",
            version, kVersion);
        g_mountBlob.clear();
        return false;
    }
    g_mountMap.reserve(numEntries);
    size_t pos = 16;
    for (uint32_t i = 0; i < numEntries; ++i) {
        if (pos + 2 > g_mountBlob.size()) {
            NARAGAME_ERROR("MountPak: TOC truncated entry %u", i);
            g_mountBlob.clear();
            g_mountMap.clear();
            return false;
        }
        const uint16_t plen = rd16(pos); pos += 2;
        if (pos + plen + 13 > g_mountBlob.size()) {
            NARAGAME_ERROR("MountPak: TOC truncated entry %u", i);
            g_mountBlob.clear();
            g_mountMap.clear();
            return false;
        }
        std::string key(reinterpret_cast<const char*>(p + pos),
                         plen);
        pos += plen;
        MountEntry e;
        e.size           = rd32(pos); pos += 4;
        e.compressedSize = rd32(pos); pos += 4;
        e.offset         = rd32(pos); pos += 4;
        e.flags          = p[pos];    pos += 1;
        // Translate the per-section offset to a blob offset so
        // Read can index g_mountBlob directly.
        e.offset += dataOffset;
        g_mountMap.emplace(std::move(key), e);
    }
    return true;
}

void UnmountPak() {
    g_mountBlob.clear();
    g_mountBlob.shrink_to_fit();
    g_mountMap.clear();
}

bool IsPakMounted() {
    return !g_mountBlob.empty();
}

bool ReadFromPak(const std::string& relPath,
                  std::vector<uint8_t>& out) {
    if (g_mountBlob.empty()) return false;
    // Normalise back-slashes so callers passing OS-native
    // paths still hit the TOC's forward-slash keys.
    std::string key = relPath;
    for (char& c : key) if (c == '\\') c = '/';
    auto it = g_mountMap.find(key);
    if (it == g_mountMap.end()) return false;
    const MountEntry& e = it->second;
    if (e.offset + e.compressedSize > g_mountBlob.size()) {
        NARAGAME_ERROR("ReadFromPak: '%s' offset out of range",
                       relPath.c_str());
        return false;
    }
    const uint8_t* src = &g_mountBlob[e.offset];
    if (e.flags == kFlagStored) {
        out.assign(src, src + e.size);
        return true;
    }
    if (e.flags == kFlagDeflated) {
#if defined(NARAGAME_HAS_MINIZ) && NARAGAME_HAS_MINIZ
        out.resize(e.size);
        mz_ulong destLen = e.size;
        const int rc = mz_uncompress(
            out.empty() ? nullptr : &out[0], &destLen,
            src, e.compressedSize);
        if (rc != MZ_OK || destLen != e.size) {
            NARAGAME_ERROR(
                "ReadFromPak: inflate failed for '%s'",
                relPath.c_str());
            out.clear();
            return false;
        }
        return true;
#else
        NARAGAME_ERROR(
            "ReadFromPak: '%s' is deflated but this build "
            "lacks miniz support.",
            relPath.c_str());
        return false;
#endif
    }
    NARAGAME_ERROR(
        "ReadFromPak: unknown flags 0x%02X on '%s'",
        unsigned(e.flags), relPath.c_str());
    return false;
}

} // namespace SceneEditor
