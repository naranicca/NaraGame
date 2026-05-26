#include "assets/Inflate.h"

// A compact, from-scratch DEFLATE/zlib decompressor. The Huffman decode follows
// the canonical "puff" approach: build per-length symbol tables, then walk the
// bit stream one bit at a time. Optimized for clarity, not raw throughput.

namespace nara::detail {
namespace {

// Length codes 257..285: base length and number of extra bits.
const uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,
                               15, 17, 19, 23, 27, 31, 35, 43,  51,  59,
                               67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

// Distance codes 0..29: base distance and number of extra bits.
const uint16_t kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,
                                17,   25,   33,   49,   65,   97,    129,   193,
                                257,  385,  513,  769,  1025, 1537,  2049,  3073,
                                4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3,  4,  4,  5,  5, 6,
                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// Order in which code-length code lengths appear in a dynamic block header.
const uint8_t kCodeLenOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                   11, 4,  12, 3, 13, 2, 14, 1, 15};

struct BitReader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    uint32_t bitBuffer = 0;
    int bitCount = 0;
    bool ok = true;

    int GetBit() {
        if (bitCount == 0) {
            if (pos >= size) {
                ok = false;
                return 0;
            }
            bitBuffer = data[pos++];
            bitCount = 8;
        }
        const int bit = bitBuffer & 1;
        bitBuffer >>= 1;
        --bitCount;
        return bit;
    }

    uint32_t GetBits(int n) {
        uint32_t value = 0;
        for (int i = 0; i < n; ++i)
            value |= static_cast<uint32_t>(GetBit()) << i;
        return value;
    }

    void AlignToByte() {
        bitCount = 0;
        bitBuffer = 0;
    }

    int GetByte() {
        if (pos >= size) {
            ok = false;
            return 0;
        }
        return data[pos++];
    }
};

// Canonical Huffman table: count of codes per length + symbols sorted by code.
struct Huffman {
    int counts[16] = {};
    int symbols[288] = {};
};

void BuildHuffman(Huffman& h, const uint8_t* lengths, int n) {
    for (int i = 0; i < 16; ++i)
        h.counts[i] = 0;
    for (int i = 0; i < n; ++i)
        ++h.counts[lengths[i]];
    h.counts[0] = 0;

    int offsets[16];
    offsets[0] = 0;
    offsets[1] = 0;
    for (int len = 1; len < 15; ++len)
        offsets[len + 1] = offsets[len] + h.counts[len];

    for (int i = 0; i < n; ++i)
        if (lengths[i] != 0)
            h.symbols[offsets[lengths[i]]++] = i;
}

int DecodeSymbol(BitReader& br, const Huffman& h) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= 15; ++len) {
        code |= br.GetBit();
        const int count = h.counts[len];
        if (code - first < count)
            return h.symbols[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

bool InflateBlock(BitReader& br, const Huffman& litLen, const Huffman& dist,
                  std::vector<uint8_t>& out) {
    for (;;) {
        const int sym = DecodeSymbol(br, litLen);
        if (sym < 0 || !br.ok)
            return false;
        if (sym == 256)
            return true; // end of block
        if (sym < 256) {
            out.push_back(static_cast<uint8_t>(sym));
            continue;
        }

        const int lenIndex = sym - 257;
        if (lenIndex >= 29)
            return false;
        const int length = kLenBase[lenIndex] + static_cast<int>(br.GetBits(kLenExtra[lenIndex]));

        const int distSym = DecodeSymbol(br, dist);
        if (distSym < 0 || distSym >= 30)
            return false;
        const int distance =
            kDistBase[distSym] + static_cast<int>(br.GetBits(kDistExtra[distSym]));
        if (static_cast<size_t>(distance) > out.size())
            return false;

        const size_t start = out.size() - distance;
        for (int i = 0; i < length; ++i)
            out.push_back(out[start + i]); // handles overlapping copies
    }
}

void BuildFixedTables(Huffman& litLen, Huffman& dist) {
    uint8_t litLengths[288];
    for (int i = 0; i < 144; ++i) litLengths[i] = 8;
    for (int i = 144; i < 256; ++i) litLengths[i] = 9;
    for (int i = 256; i < 280; ++i) litLengths[i] = 7;
    for (int i = 280; i < 288; ++i) litLengths[i] = 8;
    BuildHuffman(litLen, litLengths, 288);

    uint8_t distLengths[30];
    for (int i = 0; i < 30; ++i) distLengths[i] = 5;
    BuildHuffman(dist, distLengths, 30);
}

bool BuildDynamicTables(BitReader& br, Huffman& litLen, Huffman& dist) {
    const int hlit = static_cast<int>(br.GetBits(5)) + 257;
    const int hdist = static_cast<int>(br.GetBits(5)) + 1;
    const int hclen = static_cast<int>(br.GetBits(4)) + 4;
    if (hlit > 286 || hdist > 30)
        return false;

    uint8_t clLengths[19] = {};
    for (int i = 0; i < hclen; ++i)
        clLengths[kCodeLenOrder[i]] = static_cast<uint8_t>(br.GetBits(3));

    Huffman clHuffman;
    BuildHuffman(clHuffman, clLengths, 19);

    uint8_t lengths[288 + 32] = {};
    const int total = hlit + hdist;
    int i = 0;
    while (i < total) {
        const int sym = DecodeSymbol(br, clHuffman);
        if (sym < 0 || !br.ok)
            return false;
        if (sym < 16) {
            lengths[i++] = static_cast<uint8_t>(sym);
        } else if (sym == 16) {
            if (i == 0)
                return false;
            int repeat = static_cast<int>(br.GetBits(2)) + 3;
            const uint8_t prev = lengths[i - 1];
            while (repeat-- > 0 && i < total)
                lengths[i++] = prev;
        } else if (sym == 17) {
            int repeat = static_cast<int>(br.GetBits(3)) + 3;
            while (repeat-- > 0 && i < total)
                lengths[i++] = 0;
        } else { // sym == 18
            int repeat = static_cast<int>(br.GetBits(7)) + 11;
            while (repeat-- > 0 && i < total)
                lengths[i++] = 0;
        }
    }

    BuildHuffman(litLen, lengths, hlit);
    BuildHuffman(dist, lengths + hlit, hdist);
    return true;
}

} // namespace

bool Inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    BitReader br{data, size};

    Huffman fixedLit;
    Huffman fixedDist;
    BuildFixedTables(fixedLit, fixedDist);

    for (;;) {
        const int bfinal = br.GetBit();
        const int btype = static_cast<int>(br.GetBits(2));
        if (!br.ok)
            return false;

        if (btype == 0) {
            br.AlignToByte();
            const int len = br.GetByte() | (br.GetByte() << 8);
            br.GetByte(); // NLEN (one's complement, not verified)
            br.GetByte();
            for (int i = 0; i < len; ++i)
                out.push_back(static_cast<uint8_t>(br.GetByte()));
            if (!br.ok)
                return false;
        } else if (btype == 1) {
            if (!InflateBlock(br, fixedLit, fixedDist, out))
                return false;
        } else if (btype == 2) {
            Huffman litLen;
            Huffman dist;
            if (!BuildDynamicTables(br, litLen, dist))
                return false;
            if (!InflateBlock(br, litLen, dist, out))
                return false;
        } else {
            return false; // reserved block type
        }

        if (bfinal)
            break;
    }
    return true;
}

bool ZlibInflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    if (size < 2)
        return false;
    const uint8_t cmf = data[0];
    const uint8_t flg = data[1];
    if ((cmf & 0x0F) != 8)   // compression method must be DEFLATE
        return false;
    if (flg & 0x20)          // preset dictionary not supported
        return false;
    return Inflate(data + 2, size - 2, out);
}

} // namespace nara::detail
