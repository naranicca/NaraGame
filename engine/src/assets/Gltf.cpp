#include "NaraGame/assets/MeshLoader.h"

#include "NaraGame/core/Log.h"
#include "NaraGame/renderer/rhi/RHIDevice.h"
#include "NaraGame/renderer/rhi/RHIResources.h"

#include "MiniJson.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace NaraGame {

namespace {

constexpr int kGltfByte   = 5120;
constexpr int kGltfUByte  = 5121;
constexpr int kGltfShort  = 5122;
constexpr int kGltfUShort = 5123;
constexpr int kGltfUInt   = 5125;
constexpr int kGltfFloat  = 5126;

std::string DirOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return {};
    return path.substr(0, slash + 1);
}

std::string ReadAllBytes(const std::string& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) { ok = false; return {}; }
    std::ostringstream ss;
    ss << in.rdbuf();
    ok = true;
    return ss.str();
}

// glTF container abstraction. `.gltf` carries JSON text and points at a
// sidecar `.bin`; `.glb` is one binary file holding (header + JSON chunk
// + optional BIN chunk). The caller just wants "JSON to parse" plus the
// "bytes the accessors index into" -- this struct hands both back the
// same way for either format.
struct GltfContainer {
    std::string json;  // JSON text to feed MiniJson::Parse
    std::string bin;   // .glb BIN chunk; empty for .gltf (caller resolves URI)
    bool        ok = false;
};

// Read u32 little-endian out of a byte string at offset. The .glb spec
// fixes byte order at LE which matches x86 / x64 native, but we go
// through the bits explicitly so a big-endian rebuild would still work.
uint32_t ReadU32LE(const std::string& src, size_t offset) {
    return  uint32_t(uint8_t(src[offset + 0]))
         | (uint32_t(uint8_t(src[offset + 1])) <<  8)
         | (uint32_t(uint8_t(src[offset + 2])) << 16)
         | (uint32_t(uint8_t(src[offset + 3])) << 24);
}

// Open a `.gltf` or `.glb` and return its JSON text + (for .glb) the
// embedded buffer. Magic bytes "glTF" trigger the binary decoder; any
// other content is treated as a plain JSON .gltf file and the caller is
// responsible for resolving buffers[0].uri to a sidecar .bin.
GltfContainer OpenGltfContainer(const std::string& path) {
    GltfContainer out;
    bool ok = false;
    std::string bytes = ReadAllBytes(path, ok);
    if (!ok) return out;

    // glTF binary magic is the ASCII bytes "glTF" -- 0x46546C67 LE u32.
    const bool isGlb = bytes.size() >= 12
                    && bytes[0] == 'g' && bytes[1] == 'l'
                    && bytes[2] == 'T' && bytes[3] == 'F';
    if (!isGlb) {
        out.json = std::move(bytes);
        out.ok   = true;
        return out;
    }

    const uint32_t version    = ReadU32LE(bytes, 4);
    const uint32_t totalBytes = ReadU32LE(bytes, 8);
    if (version != 2) {
        NARAGAME_ERROR("OpenGltfContainer: '%s' is glTF binary v%u (need 2)",
                       path.c_str(), version);
        return out;
    }
    if (totalBytes > bytes.size()) {
        NARAGAME_ERROR("OpenGltfContainer: '%s' header length %u > file %zu",
                       path.c_str(), totalBytes, bytes.size());
        return out;
    }

    // chunk-type magic constants:
    //   JSON: 'JSON' little-endian (0x4E4F534A) -- 'J','S','O','N'
    //   BIN : 'BIN\0' little-endian (0x004E4942) -- 'B','I','N','\0'
    constexpr uint32_t kChunkJson = 0x4E4F534Au;
    constexpr uint32_t kChunkBin  = 0x004E4942u;

    size_t cursor = 12;  // past the header
    bool sawJson = false;
    while (cursor + 8 <= size_t(totalBytes)) {
        const uint32_t chunkLen  = ReadU32LE(bytes, cursor + 0);
        const uint32_t chunkType = ReadU32LE(bytes, cursor + 4);
        const size_t dataStart = cursor + 8;
        if (dataStart + chunkLen > size_t(totalBytes)) {
            NARAGAME_ERROR("OpenGltfContainer: '%s' chunk len %u runs past EOF",
                           path.c_str(), chunkLen);
            return out;
        }
        if (!sawJson) {
            if (chunkType != kChunkJson) {
                NARAGAME_ERROR("OpenGltfContainer: '%s' first chunk isn't JSON (0x%08X)",
                               path.c_str(), chunkType);
                return out;
            }
            out.json.assign(bytes.data() + dataStart, chunkLen);
            sawJson = true;
        } else if (chunkType == kChunkBin && out.bin.empty()) {
            // First BIN chunk wins; any further chunks (e.g. extension
            // payloads) are silently skipped -- the spec allows them and
            // we don't need them yet.
            out.bin.assign(bytes.data() + dataStart, chunkLen);
        }
        cursor = dataStart + chunkLen;
    }

    if (!sawJson) {
        NARAGAME_ERROR("OpenGltfContainer: '%s' has no JSON chunk", path.c_str());
        return out;
    }
    out.ok = true;
    return out;
}

// Pull a tightly packed array of `count` * `components` floats out of the
// .bin at the accessor's view offset. The accessor must point at FLOAT
// components for us to read it; if it doesn't we log and return empty so
// the caller can fall back (synthesise normals, skip UVs).
std::vector<float> ReadFloatAccessor(const MiniJson::Value& accessor,
                                     const MiniJson::Value& bufferViews,
                                     const std::string& bin,
                                     int components,
                                     const char* attrName) {
    if (accessor.GetInt("componentType") != kGltfFloat) {
        NARAGAME_WARN("LoadGltf: %s accessor isn't FLOAT, skipping", attrName);
        return {};
    }
    const int viewIdx = accessor.GetInt("bufferView", -1);
    const int count   = accessor.GetInt("count");
    if (viewIdx < 0 || count <= 0) return {};
    if (size_t(viewIdx) >= bufferViews.arrayValue.size()) {
        NARAGAME_WARN("LoadGltf: %s bufferView OOR", attrName);
        return {};
    }
    const MiniJson::Value& bv = bufferViews.arrayValue[size_t(viewIdx)];
    const size_t byteOffset = size_t(bv.GetInt("byteOffset"))
                            + size_t(accessor.GetInt("byteOffset"));
    const size_t byteLength = size_t(count) * size_t(components) * sizeof(float);
    if (byteOffset + byteLength > bin.size()) {
        NARAGAME_WARN("LoadGltf: %s reads past buffer end", attrName);
        return {};
    }
    std::vector<float> out(size_t(count) * components);
    std::memcpy(out.data(), bin.data() + byteOffset, byteLength);
    return out;
}

// Indices come in either 16- or 32-bit unsigned. Promote 16 -> 32 here
// so the rest of the loader works on one type; the buffer writer below
// downgrades back to 16-bit when the vertex count allows.
std::vector<uint32_t> ReadIndexAccessor(const MiniJson::Value& accessor,
                                       const MiniJson::Value& bufferViews,
                                       const std::string& bin) {
    const int componentType = accessor.GetInt("componentType");
    const int count         = accessor.GetInt("count");
    const int viewIdx       = accessor.GetInt("bufferView", -1);
    if (viewIdx < 0 || count <= 0) return {};
    if (size_t(viewIdx) >= bufferViews.arrayValue.size()) return {};
    const MiniJson::Value& bv = bufferViews.arrayValue[size_t(viewIdx)];
    const size_t byteOffset = size_t(bv.GetInt("byteOffset"))
                            + size_t(accessor.GetInt("byteOffset"));
    std::vector<uint32_t> out;
    out.resize(size_t(count));
    if (componentType == kGltfUShort) {
        const size_t byteLen = size_t(count) * 2;
        if (byteOffset + byteLen > bin.size()) return {};
        const uint8_t* src = reinterpret_cast<const uint8_t*>(bin.data())
                             + byteOffset;
        for (int i = 0; i < count; ++i) {
            // Read little-endian uint16 -- the .bin layout is LE per the
            // glTF spec, matching x86 / x64 native byte order.
            const uint16_t lo = src[i * 2 + 0];
            const uint16_t hi = src[i * 2 + 1];
            out[size_t(i)] = uint32_t(lo | (hi << 8));
        }
    } else if (componentType == kGltfUInt) {
        const size_t byteLen = size_t(count) * 4;
        if (byteOffset + byteLen > bin.size()) return {};
        std::memcpy(out.data(), bin.data() + byteOffset, byteLen);
    } else {
        NARAGAME_WARN("LoadGltf: index accessor uses unsupported componentType %d",
                      componentType);
        return {};
    }
    return out;
}

} // namespace

Mesh LoadGltf(RHIDevice& device, const std::string& path) {
    Mesh mesh;

    const GltfContainer cont = OpenGltfContainer(path);
    if (!cont.ok) {
        NARAGAME_ERROR("LoadGltf: cannot open '%s'", path.c_str());
        return mesh;
    }

    std::string jsonErr;
    const MiniJson::Value root = MiniJson::Parse(cont.json, &jsonErr);
    if (!root.IsObject()) {
        NARAGAME_ERROR("LoadGltf: '%s' parse failed: %s",
                       path.c_str(), jsonErr.c_str());
        return mesh;
    }

    const MiniJson::Value* meshes      = root.Find("meshes");
    const MiniJson::Value* accessors   = root.Find("accessors");
    const MiniJson::Value* bufferViews = root.Find("bufferViews");
    const MiniJson::Value* buffers     = root.Find("buffers");
    if (!meshes || !meshes->IsArray() || meshes->arrayValue.empty()) {
        NARAGAME_ERROR("LoadGltf: '%s' has no meshes", path.c_str());
        return mesh;
    }
    if (!accessors || !accessors->IsArray() ||
        !bufferViews || !bufferViews->IsArray() ||
        !buffers || !buffers->IsArray() || buffers->arrayValue.empty()) {
        NARAGAME_ERROR("LoadGltf: '%s' missing accessors / bufferViews / buffers",
                       path.c_str());
        return mesh;
    }
    if (meshes->arrayValue.size() > 1) {
        NARAGAME_WARN("LoadGltf: '%s' has %zu meshes; only the first loads",
                      path.c_str(), meshes->arrayValue.size());
    }

    // .glb hands us the BIN chunk directly. For .gltf we still resolve
    // buffers[0].uri to a sidecar file. data: URIs remain unsupported --
    // most pipelines emit .glb when they want self-contained assets.
    std::string bin = cont.bin;
    if (bin.empty()) {
        const std::string& bufUri = buffers->arrayValue[0].GetString("uri");
        if (bufUri.empty() || bufUri.rfind("data:", 0) == 0) {
            NARAGAME_ERROR("LoadGltf: '%s' uses embedded / data: buffer (unsupported)",
                           path.c_str());
            return mesh;
        }
        const std::string binPath = DirOf(path) + bufUri;
        bool ok = false;
        bin = ReadAllBytes(binPath, ok);
        if (!ok) {
            NARAGAME_ERROR("LoadGltf: cannot open buffer '%s'", binPath.c_str());
            return mesh;
        }
    }

    // Walk meshes[0].primitives[0].
    const MiniJson::Value& m0 = meshes->arrayValue[0];
    const MiniJson::Value* prims = m0.Find("primitives");
    if (!prims || !prims->IsArray() || prims->arrayValue.empty()) {
        NARAGAME_ERROR("LoadGltf: '%s' mesh has no primitives", path.c_str());
        return mesh;
    }
    if (prims->arrayValue.size() > 1) {
        NARAGAME_WARN("LoadGltf: '%s' mesh has %zu primitives; only the first loads",
                      path.c_str(), prims->arrayValue.size());
    }
    const MiniJson::Value& prim = prims->arrayValue[0];
    const MiniJson::Value* attrs = prim.Find("attributes");
    if (!attrs || !attrs->IsObject()) {
        NARAGAME_ERROR("LoadGltf: '%s' primitive has no attributes", path.c_str());
        return mesh;
    }
    const int posIdx = attrs->GetInt("POSITION", -1);
    const int nrmIdx = attrs->GetInt("NORMAL", -1);
    const int uvIdx  = attrs->GetInt("TEXCOORD_0", -1);
    const int idxAccessorIdx = prim.GetInt("indices", -1);
    if (posIdx < 0 || idxAccessorIdx < 0) {
        NARAGAME_ERROR("LoadGltf: '%s' primitive missing POSITION or indices",
                       path.c_str());
        return mesh;
    }
    if (size_t(posIdx) >= accessors->arrayValue.size() ||
        size_t(idxAccessorIdx) >= accessors->arrayValue.size()) {
        NARAGAME_ERROR("LoadGltf: '%s' accessor OOR", path.c_str());
        return mesh;
    }

    const std::vector<float> positions = ReadFloatAccessor(
        accessors->arrayValue[size_t(posIdx)], *bufferViews, bin, 3, "POSITION");
    if (positions.empty()) return mesh;
    const int vertexCount = int(positions.size() / 3);

    std::vector<float> normals;
    if (nrmIdx >= 0 && size_t(nrmIdx) < accessors->arrayValue.size()) {
        normals = ReadFloatAccessor(
            accessors->arrayValue[size_t(nrmIdx)], *bufferViews, bin, 3, "NORMAL");
    }
    std::vector<float> uvs;
    if (uvIdx >= 0 && size_t(uvIdx) < accessors->arrayValue.size()) {
        uvs = ReadFloatAccessor(
            accessors->arrayValue[size_t(uvIdx)], *bufferViews, bin, 2, "TEXCOORD_0");
    }

    std::vector<uint32_t> indices = ReadIndexAccessor(
        accessors->arrayValue[size_t(idxAccessorIdx)], *bufferViews, bin);
    if (indices.empty() || indices.size() % 3 != 0) {
        NARAGAME_ERROR("LoadGltf: '%s' bad index data", path.c_str());
        return mesh;
    }

    // Build Vertex3D array. Synthesise normals from face geometry if the
    // glTF didn't ship them; same scheme as LoadObj uses for OBJs with
    // missing vn lines. Use a two-statement form (declare then resize)
    // instead of `verts(size_t(N))` so the compiler can't parse it as
    // a function declaration (most vexing parse).
    std::vector<Vertex3D> verts;
    verts.resize(size_t(vertexCount));
    for (int i = 0; i < vertexCount; ++i) {
        verts[size_t(i)].position = {
            positions[size_t(i) * 3 + 0],
            positions[size_t(i) * 3 + 1],
            positions[size_t(i) * 3 + 2],
        };
        if (!normals.empty()) {
            verts[size_t(i)].normal = {
                normals[size_t(i) * 3 + 0],
                normals[size_t(i) * 3 + 1],
                normals[size_t(i) * 3 + 2],
            };
        } else {
            verts[size_t(i)].normal = {0.0f, 0.0f, 0.0f};
        }
        if (!uvs.empty()) {
            verts[size_t(i)].uv = {
                uvs[size_t(i) * 2 + 0],
                uvs[size_t(i) * 2 + 1],
            };
        } else {
            verts[size_t(i)].uv = {0.0f, 0.0f};
        }
    }
    if (normals.empty()) {
        // Accumulate face normals; normalize at the end. The face winding
        // matches glTF's CCW-front convention so the cross product points
        // outward.
        for (size_t t = 0; t + 2 < indices.size(); t += 3) {
            const uint32_t i0 = indices[t + 0];
            const uint32_t i1 = indices[t + 1];
            const uint32_t i2 = indices[t + 2];
            const Vec3& p0 = verts[i0].position;
            const Vec3& p1 = verts[i1].position;
            const Vec3& p2 = verts[i2].position;
            const float ax = p1.x - p0.x, ay = p1.y - p0.y, az = p1.z - p0.z;
            const float bx = p2.x - p0.x, by = p2.y - p0.y, bz = p2.z - p0.z;
            const float nx = ay * bz - az * by;
            const float ny = az * bx - ax * bz;
            const float nz = ax * by - ay * bx;
            verts[i0].normal.x += nx; verts[i0].normal.y += ny; verts[i0].normal.z += nz;
            verts[i1].normal.x += nx; verts[i1].normal.y += ny; verts[i1].normal.z += nz;
            verts[i2].normal.x += nx; verts[i2].normal.y += ny; verts[i2].normal.z += nz;
        }
        for (Vertex3D& v : verts) {
            const float l = std::sqrt(v.normal.x * v.normal.x
                                     + v.normal.y * v.normal.y
                                     + v.normal.z * v.normal.z);
            if (l > 1e-8f) { v.normal.x /= l; v.normal.y /= l; v.normal.z /= l; }
            else            v.normal = {0.0f, 1.0f, 0.0f};
        }
    }

    BufferDesc vb;
    vb.type        = BufferType::Vertex;
    vb.usage       = BufferUsage::Static;
    vb.size        = verts.size() * sizeof(Vertex3D);
    vb.initialData = verts.data();
    mesh.vertexBuffer = device.CreateBuffer(vb);

    BufferDesc ib;
    ib.type  = BufferType::Index;
    ib.usage = BufferUsage::Static;
    if (vertexCount <= 0xFFFF) {
        std::vector<uint16_t> i16(indices.size());
        for (size_t i = 0; i < indices.size(); ++i) i16[i] = uint16_t(indices[i]);
        ib.size        = i16.size() * sizeof(uint16_t);
        ib.initialData = i16.data();
        mesh.indexBuffer = device.CreateBuffer(ib);
        mesh.indexFormat = IndexFormat::Uint16;
    } else {
        ib.size        = indices.size() * sizeof(uint32_t);
        ib.initialData = indices.data();
        mesh.indexBuffer = device.CreateBuffer(ib);
        mesh.indexFormat = IndexFormat::Uint32;
    }
    mesh.indexCount = uint32_t(indices.size());
    mesh.vertices   = std::move(verts);
    mesh.indices    = std::move(indices);
    mesh.valid      = (mesh.vertexBuffer && mesh.indexBuffer);
    if (mesh.valid)
        NARAGAME_INFO("LoadGltf: '%s' loaded (%d verts, %u indices)",
                      path.c_str(), vertexCount, mesh.indexCount);
    return mesh;
}

// --- Skinned + animated loader --------------------------------------------

namespace {

// Read JOINTS_0 indices as Float4. glTF can pack joint indices as UByte
// or UShort -- promote to float so the Vertex3DSkin layout (Float4) can
// hold them directly. Indices are typically tiny so float precision is
// fine; the shader casts back to int4 before lookup.
std::vector<float> ReadJointIndices(const MiniJson::Value& accessor,
                                    const MiniJson::Value& bufferViews,
                                    const std::string& bin) {
    const int componentType = accessor.GetInt("componentType");
    const int count         = accessor.GetInt("count");
    const int viewIdx       = accessor.GetInt("bufferView", -1);
    if (viewIdx < 0 || count <= 0) return {};
    if (size_t(viewIdx) >= bufferViews.arrayValue.size()) return {};
    const MiniJson::Value& bv = bufferViews.arrayValue[size_t(viewIdx)];
    const size_t byteOffset = size_t(bv.GetInt("byteOffset"))
                            + size_t(accessor.GetInt("byteOffset"));
    std::vector<float> out;
    out.resize(size_t(count) * 4);
    const uint8_t* src = reinterpret_cast<const uint8_t*>(bin.data())
                         + byteOffset;
    if (componentType == kGltfUByte) {
        if (byteOffset + size_t(count) * 4 > bin.size()) return {};
        for (int i = 0; i < count; ++i)
            for (int j = 0; j < 4; ++j)
                out[size_t(i) * 4 + j] = float(src[i * 4 + j]);
    } else if (componentType == kGltfUShort) {
        if (byteOffset + size_t(count) * 8 > bin.size()) return {};
        for (int i = 0; i < count; ++i) {
            for (int j = 0; j < 4; ++j) {
                const size_t o = (i * 4 + j) * 2;
                const uint16_t v = uint16_t(src[o + 0])
                                  | (uint16_t(src[o + 1]) << 8);
                out[size_t(i) * 4 + j] = float(v);
            }
        }
    } else {
        NARAGAME_WARN("LoadGltfSkinned: JOINTS_0 componentType %d unsupported",
                      componentType);
        return {};
    }
    return out;
}

// Same as ReadFloatAccessor but allowed any component count and asserted
// at the call site; helpful for inverseBindMatrices (16 floats per
// matrix) and animation sampler output (3 or 4 floats per key).
std::vector<float> ReadFloatBlob(const MiniJson::Value& accessor,
                                 const MiniJson::Value& bufferViews,
                                 const std::string& bin,
                                 int components,
                                 const char* what) {
    if (accessor.GetInt("componentType") != kGltfFloat) {
        NARAGAME_WARN("LoadGltfSkinned: %s isn't FLOAT, skipping", what);
        return {};
    }
    const int count = accessor.GetInt("count");
    const int viewIdx = accessor.GetInt("bufferView", -1);
    if (viewIdx < 0 || count <= 0) return {};
    if (size_t(viewIdx) >= bufferViews.arrayValue.size()) return {};
    const MiniJson::Value& bv = bufferViews.arrayValue[size_t(viewIdx)];
    const size_t byteOffset = size_t(bv.GetInt("byteOffset"))
                            + size_t(accessor.GetInt("byteOffset"));
    const size_t byteLength = size_t(count) * size_t(components) * sizeof(float);
    if (byteOffset + byteLength > bin.size()) return {};
    std::vector<float> out;
    out.resize(size_t(count) * components);
    std::memcpy(out.data(), bin.data() + byteOffset, byteLength);
    return out;
}

} // namespace

SkinnedMesh LoadGltfSkinned(RHIDevice& device, const std::string& path) {
    SkinnedMesh mesh;

    const GltfContainer cont = OpenGltfContainer(path);
    if (!cont.ok) {
        NARAGAME_ERROR("LoadGltfSkinned: cannot open '%s'", path.c_str());
        return mesh;
    }
    std::string jsonErr;
    const MiniJson::Value root = MiniJson::Parse(cont.json, &jsonErr);
    if (!root.IsObject()) {
        NARAGAME_ERROR("LoadGltfSkinned: '%s' parse failed: %s",
                       path.c_str(), jsonErr.c_str());
        return mesh;
    }

    const MiniJson::Value* meshes      = root.Find("meshes");
    const MiniJson::Value* accessors   = root.Find("accessors");
    const MiniJson::Value* bufferViews = root.Find("bufferViews");
    const MiniJson::Value* buffers     = root.Find("buffers");
    const MiniJson::Value* nodesV      = root.Find("nodes");
    const MiniJson::Value* skinsV      = root.Find("skins");
    const MiniJson::Value* animsV      = root.Find("animations");
    if (!meshes || !accessors || !bufferViews || !buffers) {
        NARAGAME_ERROR("LoadGltfSkinned: '%s' missing core arrays", path.c_str());
        return mesh;
    }

    std::string bin = cont.bin;
    if (bin.empty()) {
        const std::string& bufUri = buffers->arrayValue[0].GetString("uri");
        if (bufUri.empty() || bufUri.rfind("data:", 0) == 0) {
            NARAGAME_ERROR("LoadGltfSkinned: '%s' uses embedded buffer (unsupported)",
                           path.c_str());
            return mesh;
        }
        const std::string binPath = DirOf(path) + bufUri;
        bool ok = false;
        bin = ReadAllBytes(binPath, ok);
        if (!ok) {
            NARAGAME_ERROR("LoadGltfSkinned: cannot open buffer '%s'", binPath.c_str());
            return mesh;
        }
    }

    const MiniJson::Value& m0 = meshes->arrayValue[0];
    const MiniJson::Value* prims = m0.Find("primitives");
    if (!prims || prims->arrayValue.empty()) return mesh;
    const MiniJson::Value& prim = prims->arrayValue[0];
    const MiniJson::Value* attrs = prim.Find("attributes");
    if (!attrs) return mesh;

    const int posIdx = attrs->GetInt("POSITION", -1);
    const int nrmIdx = attrs->GetInt("NORMAL", -1);
    const int uvIdx  = attrs->GetInt("TEXCOORD_0", -1);
    const int jIdx   = attrs->GetInt("JOINTS_0", -1);
    const int wIdx   = attrs->GetInt("WEIGHTS_0", -1);
    const int idxAccessorIdx = prim.GetInt("indices", -1);
    if (posIdx < 0 || idxAccessorIdx < 0 || jIdx < 0 || wIdx < 0) {
        NARAGAME_ERROR("LoadGltfSkinned: '%s' missing POSITION / indices / JOINTS_0 / WEIGHTS_0",
                       path.c_str());
        return mesh;
    }

    const auto positions = ReadFloatAccessor(
        accessors->arrayValue[size_t(posIdx)], *bufferViews, bin, 3, "POSITION");
    if (positions.empty()) return mesh;
    const int vertexCount = int(positions.size() / 3);

    auto normals = (nrmIdx >= 0)
        ? ReadFloatAccessor(accessors->arrayValue[size_t(nrmIdx)],
                            *bufferViews, bin, 3, "NORMAL")
        : std::vector<float>{};
    auto uvs = (uvIdx >= 0)
        ? ReadFloatAccessor(accessors->arrayValue[size_t(uvIdx)],
                            *bufferViews, bin, 2, "TEXCOORD_0")
        : std::vector<float>{};
    const auto jointIndices = ReadJointIndices(
        accessors->arrayValue[size_t(jIdx)], *bufferViews, bin);
    auto jointWeights = ReadFloatAccessor(
        accessors->arrayValue[size_t(wIdx)], *bufferViews, bin, 4, "WEIGHTS_0");
    if (jointIndices.empty() || jointWeights.empty()) {
        NARAGAME_ERROR("LoadGltfSkinned: '%s' JOINTS_0 / WEIGHTS_0 unreadable",
                       path.c_str());
        return mesh;
    }

    auto indices = ReadIndexAccessor(
        accessors->arrayValue[size_t(idxAccessorIdx)], *bufferViews, bin);
    if (indices.empty() || indices.size() % 3 != 0) return mesh;

    // Build the Vertex3DSkin array.
    std::vector<Vertex3DSkin> verts;
    verts.resize(size_t(vertexCount));
    for (int i = 0; i < vertexCount; ++i) {
        Vertex3DSkin& v = verts[size_t(i)];
        v.position = {positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]};
        if (!normals.empty())
            v.normal = {normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]};
        else
            v.normal = {0.0f, 1.0f, 0.0f};
        if (!uvs.empty())
            v.uv = {uvs[i * 2 + 0], uvs[i * 2 + 1]};
        else
            v.uv = {0.0f, 0.0f};
        for (int j = 0; j < 4; ++j) {
            v.jointIndices[j] = jointIndices[size_t(i) * 4 + j];
            v.jointWeights[j] = jointWeights[size_t(i) * 4 + j];
        }
    }

    // Parse nodes (TRS hierarchy) -- needed even before the skin lookup
    // so animation channels can reference any node by index.
    if (nodesV && nodesV->IsArray()) {
        mesh.nodes.resize(nodesV->arrayValue.size());
        for (size_t i = 0; i < nodesV->arrayValue.size(); ++i) {
            const MiniJson::Value& n = nodesV->arrayValue[i];
            SkinnedMeshNode& d = mesh.nodes[i];
            if (const auto* t = n.Find("translation")) {
                if (t->IsArray() && t->arrayValue.size() >= 3) {
                    d.translation = {float(t->arrayValue[0].numberValue),
                                     float(t->arrayValue[1].numberValue),
                                     float(t->arrayValue[2].numberValue)};
                }
            }
            if (const auto* r = n.Find("rotation")) {
                if (r->IsArray() && r->arrayValue.size() >= 4) {
                    d.rotation = {float(r->arrayValue[0].numberValue),
                                  float(r->arrayValue[1].numberValue),
                                  float(r->arrayValue[2].numberValue),
                                  float(r->arrayValue[3].numberValue)};
                }
            }
            if (const auto* s = n.Find("scale")) {
                if (s->IsArray() && s->arrayValue.size() >= 3) {
                    d.scale = {float(s->arrayValue[0].numberValue),
                               float(s->arrayValue[1].numberValue),
                               float(s->arrayValue[2].numberValue)};
                }
            }
        }
        // Walk children arrays to populate parent indices.
        for (size_t i = 0; i < nodesV->arrayValue.size(); ++i) {
            const auto* kids = nodesV->arrayValue[i].Find("children");
            if (!kids || !kids->IsArray()) continue;
            for (const auto& c : kids->arrayValue) {
                const int ci = int(c.numberValue);
                if (ci >= 0 && size_t(ci) < mesh.nodes.size()) {
                    mesh.nodes[size_t(ci)].parent = int(i);
                }
            }
        }
    }

    // Skin: joints[] + inverseBindMatrices (16 floats per).
    if (skinsV && skinsV->IsArray() && !skinsV->arrayValue.empty()) {
        const MiniJson::Value& skin0 = skinsV->arrayValue[0];
        if (const auto* j = skin0.Find("joints")) {
            if (j->IsArray()) {
                mesh.joints.reserve(j->arrayValue.size());
                for (const auto& v : j->arrayValue) {
                    mesh.joints.push_back(int(v.numberValue));
                }
            }
        }
        if (const auto* ibm = skin0.Find("inverseBindMatrices")) {
            const int ibmIdx = int(ibm->numberValue);
            if (ibmIdx >= 0 && size_t(ibmIdx) < accessors->arrayValue.size()) {
                auto floats = ReadFloatBlob(
                    accessors->arrayValue[size_t(ibmIdx)],
                    *bufferViews, bin, 16, "inverseBindMatrices");
                const size_t matrixCount = floats.size() / 16;
                mesh.inverseBindMatrices.resize(matrixCount);
                for (size_t i = 0; i < matrixCount; ++i) {
                    std::memcpy(mesh.inverseBindMatrices[i].data,
                                floats.data() + i * 16, 16 * sizeof(float));
                }
            }
        }
    }

    // Animations: walk channels + samplers and pull keyframe streams.
    // glTF channel.target.path is "translation" / "rotation" / "scale";
    // we drop morph-target weights for the MVP.
    if (animsV && animsV->IsArray()) {
        for (const auto& a : animsV->arrayValue) {
            SkinnedMeshAnim clip;
            clip.name = a.GetString("name", "<anim>");
            const auto* channels = a.Find("channels");
            const auto* samplers = a.Find("samplers");
            if (!channels || !samplers) continue;
            for (const auto& chV : channels->arrayValue) {
                const int sIdx = chV.GetInt("sampler", -1);
                const auto* targetV = chV.Find("target");
                if (sIdx < 0 || !targetV) continue;
                if (size_t(sIdx) >= samplers->arrayValue.size()) continue;
                const int nodeIdx = targetV->GetInt("node", -1);
                const std::string& propPath = targetV->GetString("path");
                SkinnedMeshAnimChannel ch;
                ch.nodeIndex = nodeIdx;
                if      (propPath == "translation") ch.property = SkinnedMeshAnimChannel::Translation;
                else if (propPath == "rotation")    ch.property = SkinnedMeshAnimChannel::Rotation;
                else if (propPath == "scale")       ch.property = SkinnedMeshAnimChannel::Scale;
                else continue;

                const auto& smp = samplers->arrayValue[size_t(sIdx)];
                const int inAcc  = smp.GetInt("input", -1);
                const int outAcc = smp.GetInt("output", -1);
                if (inAcc < 0 || outAcc < 0) continue;
                if (size_t(inAcc)  >= accessors->arrayValue.size()) continue;
                if (size_t(outAcc) >= accessors->arrayValue.size()) continue;
                ch.times = ReadFloatBlob(accessors->arrayValue[size_t(inAcc)],
                                         *bufferViews, bin, 1, "anim times");
                const int comps = (ch.property == SkinnedMeshAnimChannel::Rotation) ? 4 : 3;
                ch.values = ReadFloatBlob(accessors->arrayValue[size_t(outAcc)],
                                          *bufferViews, bin, comps, "anim values");
                if (ch.times.empty() || ch.values.empty()) continue;
                clip.duration = std::max(clip.duration, ch.times.back());
                clip.channels.push_back(std::move(ch));
            }
            mesh.animations.push_back(std::move(clip));
        }
    }

    // Upload to GPU.
    BufferDesc vb;
    vb.type        = BufferType::Vertex;
    vb.usage       = BufferUsage::Static;
    vb.size        = verts.size() * sizeof(Vertex3DSkin);
    vb.initialData = verts.data();
    mesh.vertexBuffer = device.CreateBuffer(vb);

    BufferDesc ib;
    ib.type  = BufferType::Index;
    ib.usage = BufferUsage::Static;
    if (vertexCount <= 0xFFFF) {
        std::vector<uint16_t> i16(indices.size());
        for (size_t i = 0; i < indices.size(); ++i) i16[i] = uint16_t(indices[i]);
        ib.size        = i16.size() * sizeof(uint16_t);
        ib.initialData = i16.data();
        mesh.indexBuffer = device.CreateBuffer(ib);
        mesh.indexFormat = IndexFormat::Uint16;
    } else {
        ib.size        = indices.size() * sizeof(uint32_t);
        ib.initialData = indices.data();
        mesh.indexBuffer = device.CreateBuffer(ib);
        mesh.indexFormat = IndexFormat::Uint32;
    }
    mesh.indexCount = uint32_t(indices.size());
    mesh.vertices   = std::move(verts);
    mesh.indices    = std::move(indices);
    mesh.valid      = (mesh.vertexBuffer && mesh.indexBuffer);
    if (mesh.valid)
        NARAGAME_INFO("LoadGltfSkinned: '%s' loaded (%d verts, %u indices, %zu joints, %zu anims)",
                      path.c_str(), vertexCount, mesh.indexCount,
                      mesh.joints.size(), mesh.animations.size());
    return mesh;
}

} // namespace NaraGame
