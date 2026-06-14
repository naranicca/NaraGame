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

constexpr int kGltfFloat  = 5126;
constexpr int kGltfUShort = 5123;
constexpr int kGltfUInt   = 5125;

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

    bool ok = false;
    const std::string source = ReadAllBytes(path, ok);
    if (!ok) {
        NARAGAME_ERROR("LoadGltf: cannot open '%s'", path.c_str());
        return mesh;
    }

    std::string jsonErr;
    const MiniJson::Value root = MiniJson::Parse(source, &jsonErr);
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

    // External .bin only (no data: URIs, no .glb yet).
    const std::string& bufUri = buffers->arrayValue[0].GetString("uri");
    if (bufUri.empty() || bufUri.rfind("data:", 0) == 0) {
        NARAGAME_ERROR("LoadGltf: '%s' uses embedded / data: buffer (unsupported)",
                       path.c_str());
        return mesh;
    }
    const std::string binPath = DirOf(path) + bufUri;
    const std::string bin = ReadAllBytes(binPath, ok);
    if (!ok) {
        NARAGAME_ERROR("LoadGltf: cannot open buffer '%s'", binPath.c_str());
        return mesh;
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

} // namespace NaraGame
