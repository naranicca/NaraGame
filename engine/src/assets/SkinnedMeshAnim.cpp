#include "NaraGame/assets/MeshLoader.h"

#include "NaraGame/math/Mat4.h"
#include "NaraGame/math/Vec3.h"
#include "NaraGame/math/Vec4.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace NaraGame {

namespace {

// Quaternion helpers. glTF stores quats as (x, y, z, w) with w the
// scalar -- same convention as the engine, so the wire layout matches
// the Vec4 it lands in. We only need the bits sampling needs: slerp
// and the 3x3 matrix it composes into the joint local transform.
Vec4 QuatNormalize(const Vec4& q) {
    const float l2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (l2 <= 1e-12f) return {0.0f, 0.0f, 0.0f, 1.0f};
    const float l = std::sqrt(l2);
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}

float QuatDot(const Vec4& a, const Vec4& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

// Spherical-linear interpolation. Picks the shorter arc by flipping
// the second quat when their dot is negative, falls back to normalized
// linear interpolation when the two quats are nearly colinear so the
// 1 / sin(theta) doesn't blow up.
Vec4 QuatSlerp(Vec4 a, Vec4 b, float t) {
    float d = QuatDot(a, b);
    if (d < 0.0f) {
        b = Vec4{-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    constexpr float kThreshold = 0.9995f;
    if (d > kThreshold) {
        Vec4 r{a.x + (b.x - a.x) * t,
               a.y + (b.y - a.y) * t,
               a.z + (b.z - a.z) * t,
               a.w + (b.w - a.w) * t};
        return QuatNormalize(r);
    }
    const float theta0 = std::acos(d);
    const float theta  = theta0 * t;
    const float sin0   = std::sin(theta0);
    const float s1     = std::sin(theta0 - theta) / sin0;
    const float s2     = std::sin(theta)          / sin0;
    return {s1 * a.x + s2 * b.x,
            s1 * a.y + s2 * b.y,
            s1 * a.z + s2 * b.z,
            s1 * a.w + s2 * b.w};
}

// Build a rotation matrix from a normalized quaternion. Uses the
// standard column-major layout the engine's Mat4 expects.
Mat4 QuatToMat4(const Vec4& q) {
    const Vec4 n = QuatNormalize(q);
    const float xx = n.x * n.x, yy = n.y * n.y, zz = n.z * n.z;
    const float xy = n.x * n.y, xz = n.x * n.z, yz = n.y * n.z;
    const float wx = n.w * n.x, wy = n.w * n.y, wz = n.w * n.z;
    Mat4 m;
    m.At(0, 0) = 1.0f - 2.0f * (yy + zz);
    m.At(0, 1) =        2.0f * (xy - wz);
    m.At(0, 2) =        2.0f * (xz + wy);
    m.At(1, 0) =        2.0f * (xy + wz);
    m.At(1, 1) = 1.0f - 2.0f * (xx + zz);
    m.At(1, 2) =        2.0f * (yz - wx);
    m.At(2, 0) =        2.0f * (xz - wy);
    m.At(2, 1) =        2.0f * (yz + wx);
    m.At(2, 2) = 1.0f - 2.0f * (xx + yy);
    return m;
}

// Find the keyframe index `i` such that times[i] <= t < times[i+1]. With
// linear interpolation that's where we lerp from. Returns -1 when the
// stream is empty; returns (size - 1) when t exceeds the last key so
// the caller can clamp without wrapping.
int FindKeyIndex(const std::vector<float>& times, float t) {
    if (times.empty()) return -1;
    if (t <= times.front()) return 0;
    if (t >= times.back())  return int(times.size()) - 1;
    // Binary search for the upper bound, then step back one.
    auto it = std::upper_bound(times.begin(), times.end(), t);
    return int(it - times.begin()) - 1;
}

// Interpolate a translation/scale (3-float) channel at time t.
Vec3 SampleVec3(const std::vector<float>& times,
                const std::vector<float>& values, float t,
                const Vec3& fallback) {
    const int i = FindKeyIndex(times, t);
    if (i < 0) return fallback;
    const int last = int(times.size()) - 1;
    if (i >= last) {
        return {values[size_t(last) * 3 + 0],
                values[size_t(last) * 3 + 1],
                values[size_t(last) * 3 + 2]};
    }
    const float ta = times[size_t(i)];
    const float tb = times[size_t(i) + 1];
    const float u  = (tb > ta) ? (t - ta) / (tb - ta) : 0.0f;
    const Vec3 a{values[size_t(i) * 3 + 0],
                 values[size_t(i) * 3 + 1],
                 values[size_t(i) * 3 + 2]};
    const Vec3 b{values[(size_t(i) + 1) * 3 + 0],
                 values[(size_t(i) + 1) * 3 + 1],
                 values[(size_t(i) + 1) * 3 + 2]};
    return {a.x + (b.x - a.x) * u,
            a.y + (b.y - a.y) * u,
            a.z + (b.z - a.z) * u};
}

Vec4 SampleQuat(const std::vector<float>& times,
                const std::vector<float>& values, float t,
                const Vec4& fallback) {
    const int i = FindKeyIndex(times, t);
    if (i < 0) return fallback;
    const int last = int(times.size()) - 1;
    if (i >= last) {
        return {values[size_t(last) * 4 + 0],
                values[size_t(last) * 4 + 1],
                values[size_t(last) * 4 + 2],
                values[size_t(last) * 4 + 3]};
    }
    const float ta = times[size_t(i)];
    const float tb = times[size_t(i) + 1];
    const float u  = (tb > ta) ? (t - ta) / (tb - ta) : 0.0f;
    const Vec4 a{values[size_t(i) * 4 + 0],
                 values[size_t(i) * 4 + 1],
                 values[size_t(i) * 4 + 2],
                 values[size_t(i) * 4 + 3]};
    const Vec4 b{values[(size_t(i) + 1) * 4 + 0],
                 values[(size_t(i) + 1) * 4 + 1],
                 values[(size_t(i) + 1) * 4 + 2],
                 values[(size_t(i) + 1) * 4 + 3]};
    return QuatSlerp(a, b, u);
}

Mat4 LocalTRS(const Vec3& t, const Vec4& r, const Vec3& s) {
    // model = T * R * S in standard column-major land (the same
    // convention Mat4 uses).
    return mat4::Translation(t) * QuatToMat4(r) * mat4::Scale(s);
}

} // namespace

void SampleSkinnedMeshAnim(const SkinnedMesh& mesh, int animIndex,
                           float t, Mat4* outJointMatrices) {
    if (!outJointMatrices || mesh.joints.empty()) return;
    // Copy the node TRS into a writable scratch and overlay channel
    // samples on top of it.
    std::vector<Vec3> nodeT(mesh.nodes.size());
    std::vector<Vec4> nodeR(mesh.nodes.size());
    std::vector<Vec3> nodeS(mesh.nodes.size());
    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        nodeT[i] = mesh.nodes[i].translation;
        nodeR[i] = mesh.nodes[i].rotation;
        nodeS[i] = mesh.nodes[i].scale;
    }

    if (animIndex >= 0 && size_t(animIndex) < mesh.animations.size()) {
        const SkinnedMeshAnim& clip = mesh.animations[size_t(animIndex)];
        // Wrap to clip duration so a free-running app time keeps the
        // animation looping. A zero duration means it's a single pose.
        float tt = t;
        if (clip.duration > 0.0f) {
            tt = std::fmod(t, clip.duration);
            if (tt < 0.0f) tt += clip.duration;
        }
        for (const SkinnedMeshAnimChannel& ch : clip.channels) {
            if (ch.nodeIndex < 0 || size_t(ch.nodeIndex) >= mesh.nodes.size())
                continue;
            const size_t n = size_t(ch.nodeIndex);
            switch (ch.property) {
                case SkinnedMeshAnimChannel::Translation:
                    nodeT[n] = SampleVec3(ch.times, ch.values, tt, nodeT[n]);
                    break;
                case SkinnedMeshAnimChannel::Rotation:
                    nodeR[n] = SampleQuat(ch.times, ch.values, tt, nodeR[n]);
                    break;
                case SkinnedMeshAnimChannel::Scale:
                    nodeS[n] = SampleVec3(ch.times, ch.values, tt, nodeS[n]);
                    break;
            }
        }
    }

    // Hierarchical compose: world[i] = world[parent[i]] * local[i]. The
    // glTF spec doesn't promise nodes are in topological order, but in
    // practice they are -- and the assets we generate are. A robust
    // implementation would topo-sort or memoize; this MVP just walks
    // forward and assumes parent indices precede children.
    std::vector<Mat4> nodeWorld(mesh.nodes.size());
    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        const Mat4 local = LocalTRS(nodeT[i], nodeR[i], nodeS[i]);
        const int p = mesh.nodes[i].parent;
        if (p < 0) nodeWorld[i] = local;
        else       nodeWorld[i] = nodeWorld[size_t(p)] * local;
    }

    // Final skin matrix per joint: jointWorld * inverseBind.
    for (size_t j = 0; j < mesh.joints.size(); ++j) {
        const int nodeIdx = mesh.joints[j];
        if (nodeIdx < 0 || size_t(nodeIdx) >= nodeWorld.size()) {
            outJointMatrices[j] = Mat4::Identity();
            continue;
        }
        const Mat4& world = nodeWorld[size_t(nodeIdx)];
        if (j < mesh.inverseBindMatrices.size()) {
            outJointMatrices[j] = world * mesh.inverseBindMatrices[j];
        } else {
            outJointMatrices[j] = world;
        }
    }
}

} // namespace NaraGame
