#include "Pak.h"

#include <NaraGame/core/Log.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace SceneEditor {

namespace fs = std::filesystem;

namespace {

constexpr char     kMagic[4] = { 'N', 'P', 'A', 'K' };
constexpr uint32_t kVersion  = 1;

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

    // First pass: collect every file under assetsRoot + record
    // its relative path + size. We need the offset of each
    // file in the data section, so size has to be known before
    // the TOC is written.
    struct Entry {
        std::string relPath;
        uint32_t    size;
        uint32_t    offset;  // filled after size pass
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
                "WritePak: '%s' exceeds 4 GB cap -- pak v1 uses "
                "32-bit sizes.",
                de.path().string().c_str());
            return false;
        }
        Entry e;
        e.relPath = Normalise(rel);
        e.size    = uint32_t(fsize);
        e.offset  = 0;
        entries.push_back(std::move(e));
    }

    // Fill offsets so the data section is laid out in TOC
    // order with no gaps.
    uint32_t running = 0;
    for (Entry& e : entries) {
        e.offset = running;
        running += e.size;
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
        Write32(headerToc, e.offset);
    }
    const uint32_t dataOffset = uint32_t(headerToc.size());
    headerToc[dataOffsetPos + 0] = uint8_t( dataOffset        & 0xFF);
    headerToc[dataOffsetPos + 1] = uint8_t((dataOffset >>  8) & 0xFF);
    headerToc[dataOffsetPos + 2] = uint8_t((dataOffset >> 16) & 0xFF);
    headerToc[dataOffsetPos + 3] = uint8_t((dataOffset >> 24) & 0xFF);

    // Stream the result. Open binary write, write the header +
    // TOC, then re-open each source file + stream its bytes.
    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        NARAGAME_ERROR("WritePak: cannot open '%s' for write",
                       outPath.c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(&headerToc[0]),
              std::streamsize(headerToc.size()));
    for (const Entry& e : entries) {
        std::ifstream in((root / e.relPath).string(),
                          std::ios::binary);
        if (!in) {
            NARAGAME_ERROR("WritePak: cannot read '%s'",
                           e.relPath.c_str());
            return false;
        }
        constexpr std::streamsize kChunk = 64 * 1024;
        std::vector<char> chunkBuf;
        chunkBuf.resize(size_t(kChunk));
        char* chunkPtr = chunkBuf.empty() ? nullptr : &chunkBuf[0];
        std::streamsize remaining = std::streamsize(e.size);
        while (remaining > 0) {
            const std::streamsize want =
                (remaining < kChunk) ? remaining : kChunk;
            in.read(chunkPtr, want);
            const std::streamsize got = in.gcount();
            if (got <= 0) break;
            out.write(chunkPtr, got);
            remaining -= got;
        }
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
        uint32_t    offset;
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
        if (!Read32(f, e.size) || !Read32(f, e.offset)) {
            NARAGAME_ERROR("ExtractPak: TOC truncated at entry %u", i);
            return false;
        }
        toc.push_back(std::move(e));
    }

    std::error_code ec;
    fs::create_directories(destDir, ec);
    constexpr size_t kChunk = 64 * 1024;
    std::vector<char> chunkBuf;
    chunkBuf.resize(kChunk);
    char* chunkPtr = chunkBuf.empty() ? nullptr : &chunkBuf[0];
    for (const TocEntry& e : toc) {
        const fs::path full = fs::path(destDir) / e.path;
        fs::create_directories(full.parent_path(), ec);
        if (std::fseek(f, long(dataOffset + e.offset), SEEK_SET)
            != 0) {
            NARAGAME_ERROR(
                "ExtractPak: seek failed for '%s'", e.path.c_str());
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
        uint32_t remaining = e.size;
        while (remaining > 0) {
            const size_t want = remaining < kChunk
                                  ? remaining : kChunk;
            const size_t got = std::fread(chunkPtr, 1, want, f);
            if (got == 0) break;
            std::fwrite(chunkPtr, 1, got, of);
            remaining -= uint32_t(got);
        }
        std::fclose(of);
    }
    return true;
}

} // namespace SceneEditor
