#pragma once

#include "NaraGame/renderer/Renderer3D.h"
#include "NaraGame/renderer/rhi/RHITypes.h"

#include <memory>
#include <string>
#include <vector>

namespace NaraGame {

class RHIBuffer;
class RHIDevice;

// A loaded static mesh ready to feed Renderer3D::DrawMesh. Holds its own
// vertex + index buffers and remembers the index format so the caller
// just plumbs (vb, ib, indexCount, indexFormat) through. Keep the Mesh
// alive for as long as you draw it; the buffers are owned here, not by
// the renderer.
struct Mesh {
    std::unique_ptr<RHIBuffer> vertexBuffer;
    std::unique_ptr<RHIBuffer> indexBuffer;
    uint32_t                   indexCount  = 0;
    IndexFormat                indexFormat = IndexFormat::Uint16;
    // The CPU-side data is retained too -- useful for bounding-box
    // queries, picking, or rebuilding the GPU buffers after a device
    // reset. Empty for a Mesh that failed to load.
    std::vector<Vertex3D>      vertices;
    std::vector<uint32_t>      indices;   // always promoted to 32-bit for storage
    // PBR material parsed from the source asset (glTF). `material` holds
    // raw RHITexture* pointers into `ownedTextures`, which owns those
    // textures' lifetimes for as long as the Mesh is alive. For an asset
    // with no material info the loader leaves Material at its defaults
    // (white albedo, no maps) and the renderer's white-fallback path
    // kicks in.
    Material                                  material;
    std::vector<std::unique_ptr<RHITexture>>  ownedTextures;
    bool                                      valid       = false;
};

// Parse a Wavefront .obj file from disk and upload it as a static mesh.
//
// Supported features:
//   v / vn / vt lines
//   f lines with v, v/vt, v//vn, or v/vt/vn indices
//   triangles + quads (quads triangulated as a fan)
//   negative (from-end) indices
//
// Not supported (silently ignored):
//   groups (g), materials (usemtl / mtllib), smoothing (s)
//   n-gons with more than 4 vertices (only the first triangle of the fan
//   is taken -- non-convex polys will look wrong; triangulate before
//   exporting)
//
// Missing normals are synthesized as the cross product of two edges of
// the first face that references each vertex, so the mesh still renders
// with sensible lighting. Missing UVs default to (0, 0). Returns a Mesh
// with valid = false on I/O or parse failure (the error goes to the log).
Mesh LoadObj(RHIDevice& device, const std::string& path);

// Same as LoadObj but reads from an in-memory string -- useful for tests
// or generated meshes.
Mesh LoadObjFromString(RHIDevice& device, const std::string& source,
                       const std::string& debugName = "<inline>");

// One node in the bind-pose hierarchy of a skinned mesh. Hierarchy is
// flat: `parent` indexes back into the same `nodes` vector (-1 for the
// root). TRS values describe the node's local transform relative to its
// parent, exactly as glTF stores them. Sampling overrides T/R/S from
// any animation channels that target the node, then composes them into
// the joint's world matrix.
struct SkinnedMeshNode {
    int   parent      = -1;
    Vec3  translation = {0.0f, 0.0f, 0.0f};
    Vec4  rotation    = {0.0f, 0.0f, 0.0f, 1.0f};  // quaternion x,y,z,w
    Vec3  scale       = {1.0f, 1.0f, 1.0f};
};

// One animation channel: a stream of keyframes targeting one property
// (T/R/S) of one node. `times` holds the keyframe timestamps in seconds;
// `values` holds 3 floats per key for translation/scale or 4 floats per
// key for rotation. Linear interpolation only -- cubic spline /
// step interpolation are deferred to a follow-up.
struct SkinnedMeshAnimChannel {
    enum Property { Translation = 0, Rotation = 1, Scale = 2 };
    int     nodeIndex = -1;
    int     property  = Translation;
    std::vector<float> times;
    std::vector<float> values;
};

struct SkinnedMeshAnim {
    std::string                          name;
    float                                duration = 0.0f;
    std::vector<SkinnedMeshAnimChannel>  channels;
};

// Loaded skinned mesh: per-vertex skin data feeding Vertex3DSkin, a
// bind-pose hierarchy + inverse bind matrices ready for Renderer3D::
// DrawSkinnedMesh, and any animation clips the glTF carried.
struct SkinnedMesh {
    std::unique_ptr<RHIBuffer>       vertexBuffer;
    std::unique_ptr<RHIBuffer>       indexBuffer;
    uint32_t                         indexCount  = 0;
    IndexFormat                      indexFormat = IndexFormat::Uint16;
    std::vector<Vertex3DSkin>        vertices;
    std::vector<uint32_t>            indices;

    // Bind-pose skeleton. `nodes` is the flat node array; `joints`
    // remaps from joint index (matches Vertex3DSkin::jointIndices) to
    // node index. inverseBindMatrices[i] applies in joint space for
    // joint i and is the matrix the GPU shader uses as `bindInv`.
    std::vector<SkinnedMeshNode>     nodes;
    std::vector<int>                 joints;
    std::vector<Mat4>                inverseBindMatrices;

    // All animation clips the file carried. Each is independent; the
    // caller picks one and sets a time via SampleSkinnedMeshAnim.
    std::vector<SkinnedMeshAnim>     animations;

    // Material + owned textures parsed from the glTF, same shape as
    // Mesh::material / Mesh::ownedTextures above.
    Material                                  material;
    std::vector<std::unique_ptr<RHITexture>>  ownedTextures;

    bool                             valid = false;
};

// Sample animation `animIndex` of `mesh` at time `t` (seconds, wrapped
// to the clip's duration). Fills `outJointMatrices` with one float4x4
// per joint -- ready to hand to Renderer3D::DrawSkinnedMesh. Caller
// allocates the array to at least `mesh.joints.size()` entries.
void SampleSkinnedMeshAnim(const SkinnedMesh& mesh, int animIndex,
                           float t, Mat4* outJointMatrices);

// Load a glTF 2.0 static mesh from a `.gltf` file. The companion `.bin`
// buffer is resolved relative to the .gltf's directory.
//
// Supported features:
//   meshes[0].primitives[0] attributes: POSITION, NORMAL, TEXCOORD_0
//   indices: SCALAR USHORT or UINT
//   accessor componentType FLOAT for vertex attributes
//
// Not supported (silently warned + skipped):
//   embedded buffers (data: URIs) -- needs an external .bin
//   skins, animations (Phase 7.5)
//   morph targets, multiple primitives, materials, .glb container
//
// Missing NORMAL is synthesised from face geometry like LoadObj does;
// missing TEXCOORD_0 defaults to (0, 0). Multiple meshes in the file
// are warned about and only the first is loaded.
Mesh LoadGltf(RHIDevice& device, const std::string& path);

// Same as LoadGltf but for the skinned-mesh path: parses skin + joints
// + inverseBindMatrices + animation channels alongside the geometry.
// The vertex layout is Vertex3DSkin (Vertex3D + JOINTS_0 + WEIGHTS_0).
// Inputs that lack a skin or animation come back with `joints` /
// `animations` empty -- the caller can still draw it as a rigid mesh.
SkinnedMesh LoadGltfSkinned(RHIDevice& device, const std::string& path);

} // namespace NaraGame
