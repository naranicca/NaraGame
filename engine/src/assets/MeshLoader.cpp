#include "NaraGame/assets/MeshLoader.h"

#include "NaraGame/core/Log.h"
#include "NaraGame/renderer/rhi/RHIDevice.h"
#include "NaraGame/renderer/rhi/RHIResources.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace NaraGame {

namespace {

// Parse one face-corner token of the form "v", "v/vt", "v//vn", or
// "v/vt/vn". Indices in .obj are 1-based; negative indices count from
// the end of the relevant array. Outputs are converted to 0-based; a
// missing component comes back as -1 so the caller can synthesize.
struct ObjFaceIdx { int v = -1; int vt = -1; int vn = -1; };

bool ParseFaceToken(const std::string& tok,
                    int vCount, int vtCount, int vnCount,
                    ObjFaceIdx& out) {
    auto resolve = [](int raw, int count) -> int {
        if (raw == 0) return -1;                 // "0" is malformed in .obj
        if (raw > 0)  return raw - 1;
        // Negative = from the end.
        return count + raw;
    };
    int v = 0, vt = 0, vn = 0;
    // Manually split on '/' so empty fields (v//vn) work; istringstream
    // skips whitespace but not slashes.
    const size_t s1 = tok.find('/');
    if (s1 == std::string::npos) {
        v = std::atoi(tok.c_str());
    } else {
        v = std::atoi(tok.substr(0, s1).c_str());
        const size_t s2 = tok.find('/', s1 + 1);
        if (s2 == std::string::npos) {
            // v/vt only
            vt = std::atoi(tok.substr(s1 + 1).c_str());
        } else {
            if (s2 > s1 + 1)
                vt = std::atoi(tok.substr(s1 + 1, s2 - s1 - 1).c_str());
            vn = std::atoi(tok.substr(s2 + 1).c_str());
        }
    }
    if (v == 0) return false;
    out.v  = resolve(v,  vCount);
    out.vt = resolve(vt, vtCount);
    out.vn = resolve(vn, vnCount);
    if (out.v  < 0 || out.v  >= vCount)  return false;
    if (out.vt >= vtCount) out.vt = -1;
    if (out.vn >= vnCount) out.vn = -1;
    return true;
}

// Key into the dedup map. Each unique (v, vt, vn) triple becomes one
// Vertex3D in the output buffer; reusing the same triple reuses the
// same final index.
struct ObjKey {
    int v, vt, vn;
    bool operator==(const ObjKey& o) const {
        return v == o.v && vt == o.vt && vn == o.vn;
    }
};
struct ObjKeyHash {
    size_t operator()(const ObjKey& k) const {
        // 21 bits per field is plenty for any practical OBJ; mix with
        // simple multiplicative hash. (size_t may be 32-bit on some
        // platforms but we never hit overflow at these scales.)
        const uint64_t a = uint32_t(k.v  + 1);
        const uint64_t b = uint32_t(k.vt + 1);
        const uint64_t c = uint32_t(k.vn + 1);
        return size_t(a * 73856093u ^ b * 19349663u ^ c * 83492791u);
    }
};

Mesh BuildMeshFromParse(RHIDevice& device,
                       const std::vector<float>& vPos,    // x,y,z triples
                       const std::vector<float>& vTex,    // u,v pairs
                       const std::vector<float>& vNrm,    // x,y,z triples
                       const std::vector<ObjFaceIdx>& corners,
                       const std::string& debugName) {
    Mesh mesh;
    if (corners.empty() || vPos.empty()) {
        NARAGAME_ERROR("LoadObj: '%s' has no faces", debugName.c_str());
        return mesh;
    }

    // First pass: dedupe (v, vt, vn) triples and emit Vertex3D + indices.
    // Missing normals get a sentinel that the second pass fills in.
    std::unordered_map<ObjKey, uint32_t, ObjKeyHash> dedup;
    std::vector<Vertex3D>  verts;
    std::vector<uint32_t>  indices;
    indices.reserve(corners.size());

    // Track which output verts need a synthesized normal so we can run
    // an accumulation pass after we know all faces. -1 = has a real
    // normal from the file already.
    std::vector<int> needNormal;

    for (size_t i = 0; i + 2 < corners.size(); i += 3) {
        const ObjFaceIdx tri[3] = {corners[i], corners[i + 1], corners[i + 2]};

        // Face normal computed from positions only (works even when the
        // file omitted normals). Cross of edges P1-P0 and P2-P0.
        const int p0 = tri[0].v, p1 = tri[1].v, p2 = tri[2].v;
        const float ax = vPos[p1 * 3 + 0] - vPos[p0 * 3 + 0];
        const float ay = vPos[p1 * 3 + 1] - vPos[p0 * 3 + 1];
        const float az = vPos[p1 * 3 + 2] - vPos[p0 * 3 + 2];
        const float bx = vPos[p2 * 3 + 0] - vPos[p0 * 3 + 0];
        const float by = vPos[p2 * 3 + 1] - vPos[p0 * 3 + 1];
        const float bz = vPos[p2 * 3 + 2] - vPos[p0 * 3 + 2];
        float nx = ay * bz - az * by;
        float ny = az * bx - ax * bz;
        float nz = ax * by - ay * bx;
        const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nl > 1e-8f) { nx /= nl; ny /= nl; nz /= nl; }

        for (int j = 0; j < 3; ++j) {
            const ObjKey key{tri[j].v, tri[j].vt, tri[j].vn};
            auto it = dedup.find(key);
            uint32_t idx;
            if (it != dedup.end()) {
                idx = it->second;
            } else {
                Vertex3D v;
                v.position = {vPos[tri[j].v * 3 + 0],
                              vPos[tri[j].v * 3 + 1],
                              vPos[tri[j].v * 3 + 2]};
                if (tri[j].vt >= 0 && size_t(tri[j].vt) * 2 + 1 < vTex.size()) {
                    // OBJ's V is bottom-origin, our textures are top-origin
                    // (matches TextureLoader's row layout), so flip.
                    v.uv = {vTex[tri[j].vt * 2 + 0],
                            1.0f - vTex[tri[j].vt * 2 + 1]};
                } else {
                    v.uv = {0.0f, 0.0f};
                }
                if (tri[j].vn >= 0 && size_t(tri[j].vn) * 3 + 2 < vNrm.size()) {
                    v.normal = {vNrm[tri[j].vn * 3 + 0],
                                vNrm[tri[j].vn * 3 + 1],
                                vNrm[tri[j].vn * 3 + 2]};
                    needNormal.push_back(-1);
                } else {
                    // Seed with this face's normal; if other faces also
                    // reference this vertex we'll accumulate them below.
                    v.normal = {0.0f, 0.0f, 0.0f};
                    needNormal.push_back(1);
                }
                idx = uint32_t(verts.size());
                verts.push_back(v);
                dedup.emplace(key, idx);
            }
            indices.push_back(idx);

            if (idx < needNormal.size() && needNormal[idx] != -1) {
                verts[idx].normal.x += nx;
                verts[idx].normal.y += ny;
                verts[idx].normal.z += nz;
            }
        }
    }

    // Normalize the accumulated synthetic normals.
    for (size_t i = 0; i < verts.size(); ++i) {
        if (i >= needNormal.size() || needNormal[i] == -1) continue;
        const float l = std::sqrt(verts[i].normal.x * verts[i].normal.x
                                 + verts[i].normal.y * verts[i].normal.y
                                 + verts[i].normal.z * verts[i].normal.z);
        if (l > 1e-8f) {
            verts[i].normal.x /= l;
            verts[i].normal.y /= l;
            verts[i].normal.z /= l;
        } else {
            verts[i].normal = {0.0f, 1.0f, 0.0f};
        }
    }

    if (verts.empty() || indices.empty()) {
        NARAGAME_ERROR("LoadObj: '%s' produced no geometry", debugName.c_str());
        return mesh;
    }

    BufferDesc vb;
    vb.type        = BufferType::Vertex;
    vb.usage       = BufferUsage::Static;
    vb.size        = verts.size() * sizeof(Vertex3D);
    vb.initialData = verts.data();
    mesh.vertexBuffer = device.CreateBuffer(vb);

    // Promote to 32-bit indices only when we have more than 64k unique
    // verts; the 16-bit path is tighter on bandwidth.
    BufferDesc ib;
    ib.type  = BufferType::Index;
    ib.usage = BufferUsage::Static;
    if (verts.size() <= 0xFFFFu) {
        std::vector<uint16_t> i16(indices.size());
        for (size_t i = 0; i < indices.size(); ++i)
            i16[i] = uint16_t(indices[i]);
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
    if (!mesh.valid)
        NARAGAME_ERROR("LoadObj: '%s' failed to create GPU buffers",
                       debugName.c_str());
    else
        NARAGAME_INFO("LoadObj: '%s' loaded (%zu verts, %u indices)",
                      debugName.c_str(), mesh.vertices.size(),
                      mesh.indexCount);
    return mesh;
}

Mesh ParseObjStream(RHIDevice& device, std::istream& in,
                    const std::string& debugName) {
    std::vector<float>       vPos;
    std::vector<float>       vTex;
    std::vector<float>       vNrm;
    std::vector<ObjFaceIdx>  corners;     // pre-triangulated face corners

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string tok;
        if (!(ls >> tok)) continue;
        if (tok == "v") {
            float x = 0, y = 0, z = 0;
            ls >> x >> y >> z;
            vPos.push_back(x); vPos.push_back(y); vPos.push_back(z);
        } else if (tok == "vt") {
            float u = 0, v = 0;
            ls >> u >> v;
            vTex.push_back(u); vTex.push_back(v);
        } else if (tok == "vn") {
            float x = 0, y = 0, z = 0;
            ls >> x >> y >> z;
            vNrm.push_back(x); vNrm.push_back(y); vNrm.push_back(z);
        } else if (tok == "f") {
            std::vector<ObjFaceIdx> face;
            std::string corner;
            while (ls >> corner) {
                ObjFaceIdx idx;
                if (ParseFaceToken(corner,
                                   int(vPos.size() / 3),
                                   int(vTex.size() / 2),
                                   int(vNrm.size() / 3),
                                   idx)) {
                    face.push_back(idx);
                }
            }
            // Fan-triangulate. Tris are the common case (one iteration),
            // quads cost two; n-gons get split into n-2 fans.
            for (size_t i = 1; i + 1 < face.size(); ++i) {
                corners.push_back(face[0]);
                corners.push_back(face[i]);
                corners.push_back(face[i + 1]);
            }
        }
        // g / s / o / usemtl / mtllib / etc.: silently skip.
    }
    return BuildMeshFromParse(device, vPos, vTex, vNrm, corners, debugName);
}

} // namespace

Mesh LoadObj(RHIDevice& device, const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        NARAGAME_ERROR("LoadObj: cannot open '%s'", path.c_str());
        return {};
    }
    return ParseObjStream(device, in, path);
}

Mesh LoadObjFromString(RHIDevice& device, const std::string& source,
                       const std::string& debugName) {
    std::istringstream in(source);
    return ParseObjStream(device, in, debugName);
}

} // namespace NaraGame
