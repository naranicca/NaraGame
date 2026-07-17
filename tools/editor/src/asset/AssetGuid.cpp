#include "AssetGuid.h"

#include <objbase.h>   // CoCreateGuid

#include <cctype>
#include <cstdio>
#include <cstring>

namespace SceneEditor {

std::string AssetGuid::ToString() const {
    // RFC-4122 hyphenated form: 8-4-4-4-12 hex, lowercase. We don't
    // tweak the version/variant bits at generation time, so the string
    // form follows the byte order produced by CoCreateGuid as-is.
    char buf[37];
    std::snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2],  bytes[3],
        bytes[4], bytes[5],
        bytes[6], bytes[7],
        bytes[8], bytes[9],
        bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    return std::string(buf);
}

namespace {

int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

} // namespace

AssetGuid AssetGuid::FromString(const std::string& s) {
    AssetGuid out;
    // Strict 36-char canonical form. Anything else fails -> all-zero GUID.
    if (s.size() != 36) return out;
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-')
        return out;
    int byteIdx = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        const int hi = HexNibble(c);
        const int lo = HexNibble(s[++i]);
        if (hi < 0 || lo < 0 || byteIdx >= 16) return AssetGuid{};
        out.bytes[size_t(byteIdx++)] = uint8_t((hi << 4) | lo);
    }
    return out;
}

AssetGuid AssetGuid::Generate() {
    AssetGuid out;
    GUID g{};
    if (FAILED(CoCreateGuid(&g))) return out;
    std::memcpy(out.bytes.data(), &g, 16);
    return out;
}

} // namespace SceneEditor
