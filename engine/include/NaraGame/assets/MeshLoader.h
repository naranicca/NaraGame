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
    bool                       valid       = false;
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

} // namespace NaraGame
