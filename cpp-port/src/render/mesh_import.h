#pragma once

// Step 2 (PORT_PROGRESS.md, glTF skinned-mesh import pipeline): parses a
// glTF/GLB asset into plain, bgfx-free data -- geometry, an optional skin
// (joint hierarchy + inverse bind matrices), and material base-color info.
// Deliberately mirrors this port's existing "pure logic vs GPU" split (see
// stadium_mesh.h/pylon_mesh.h): this file has zero bgfx dependency and is
// fully ctest-covered; skinned_mesh.h/.cpp (GPU upload/draw) consumes its
// output the same way renderer.cpp consumes stadium_mesh.h's MeshVertex
// lists.
//
// Uses cgltf (MIT, single-header) for the actual glTF/JSON parsing --
// already vendored in this repo at
// third_party/bgfx.cmake/bgfx/3rdparty/cgltf/cgltf.h (bgfx's own geometryc
// tool depends on it, even though that tool itself is disabled in this
// build via BGFX_BUILD_TOOLS_GEOMETRY=OFF) -- reused directly rather than
// vendoring a second copy of the same library.
//
// Scope, deliberately bounded for this first pass (documented rather than
// silently assumed): only the first primitive of the first mesh in the
// glTF is imported (a single skinned rig, not a multi-mesh scene); indices
// must be present (no non-indexed-primitive fallback); a skin's inverse
// bind matrices must be provided (no "assume identity" fallback); a joint
// node's parent is only resolved to another entry in this same joint list
// (a joint whose real cgltf parent lies outside the skin's own joint array
// -- e.g. an un-skinned skeleton root -- is treated as a root, parent -1).
// A material's base color texture, if present, must be embedded via a
// bufferView (the standard .glb convention) -- a bare external-file or
// data-URI `image.uri` is not resolved (cgltf's own buffer loading only
// decodes the top-level "buffers" array, not "images"; this keeps this
// pass's scope to the self-contained .glb case actually needed here).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// One imported vertex. When the source mesh has no skin (a plain static
// mesh import), joints stays all-zero and weights defaults to full weight
// on joint 0 -- SkinnedMesh always uploads bone 0 = identity, so an
// unskinned import still renders correctly through the same skinning
// shader rather than needing a separate unskinned code path.
struct ImportedVertex {
    double px = 0, py = 0, pz = 0;
    double nx = 0, ny = 0, nz = 1;
    double u = 0, v = 0;
    std::array<uint16_t, 4> joints{0, 0, 0, 0};
    std::array<double, 4> weights{1.0, 0, 0, 0};
};

// One joint in the skin's hierarchy. `localBindMatrix` is the node's own
// bind-pose local transform (translation/rotation/scale as authored in the
// glTF, composed by cgltf_node_transform_local -- NOT a glTF animation
// sample; this project drives joints procedurally from physics, not baked
// clips, per the combined tire-model/animation plan's Step 3). Both
// matrices are column-major 4x4, matching glTF's own convention.
struct ImportedJoint {
    std::string name;
    int parent = -1; // index into ImportedMesh::joints, -1 = root
    std::array<double, 16> inverseBindMatrix{};
    std::array<double, 16> localBindMatrix{};
};

struct ImportedMesh {
    std::vector<ImportedVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<ImportedJoint> joints; // empty if the mesh has no skin

    // Material base color (glTF's pbrMetallicRoughness.baseColorFactor,
    // defaults to opaque white per spec when the glTF supplies no material
    // at all). If the material has an embedded base color texture, its raw
    // encoded bytes (PNG/JPEG) are copied here for texture_import.cpp to
    // decode -- empty when there's no texture (a flat-color material).
    std::array<double, 4> baseColorFactor{1.0, 1.0, 1.0, 1.0};
    std::vector<uint8_t> baseColorImageBytes;
};

struct MeshImportResult {
    bool ok = false;
    std::string error; // human-readable, only meaningful when !ok
    ImportedMesh mesh;
};

// Parses glTF (JSON) or GLB (binary) data already loaded into memory --
// the caller owns `data`'s lifetime only for the duration of this call.
MeshImportResult importGltfFromMemory(const void* data, size_t size);
