#include "mesh_import.h"

// Single-header library, implementation compiled exactly here (STB-style
// convention) -- see mesh_import.h's own header comment for why this reuses
// bgfx's already-vendored copy instead of a second one under this port's own
// third_party/.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <utility>

namespace {

// Reads up to `n` components of accessor element `idx` as doubles.
// cgltf_accessor_read_float already handles normalized-int unpacking and
// zero-pads past the accessor's real component count, so this is a thin
// float->double widen, not new numeric logic.
void readVec(const cgltf_accessor* acc, cgltf_size idx, double* out, int n) {
    float tmp[16] = {};
    cgltf_accessor_read_float(acc, idx, tmp, (cgltf_size)n);
    for (int i = 0; i < n; ++i) out[i] = tmp[i];
}

} // namespace

MeshImportResult importGltfFromMemory(const void* data, size_t size) {
    MeshImportResult result;

    cgltf_options options{};
    cgltf_data* gltf = nullptr;
    if (cgltf_parse(&options, data, (cgltf_size)size, &gltf) != cgltf_result_success) {
        result.error = "cgltf_parse failed (malformed glTF/GLB)";
        return result;
    }
    // gltf_path=nullptr: this pipeline only resolves buffers embedded via
    // data: URIs or GLB's binary chunk (see mesh_import.h's scope note) --
    // no external .bin sibling file needs resolving relative to a path.
    if (cgltf_load_buffers(&options, gltf, nullptr) != cgltf_result_success) {
        cgltf_free(gltf);
        result.error = "cgltf_load_buffers failed";
        return result;
    }

    if (gltf->meshes_count == 0 || gltf->meshes[0].primitives_count == 0) {
        cgltf_free(gltf);
        result.error = "glTF has no mesh/primitive to import";
        return result;
    }
    const cgltf_mesh& srcMesh = gltf->meshes[0];
    const cgltf_primitive& prim = srcMesh.primitives[0];
    if (!prim.indices) {
        cgltf_free(gltf);
        result.error = "primitive has no indices (non-indexed primitives not supported)";
        return result;
    }

    const cgltf_accessor* posAcc = nullptr;
    const cgltf_accessor* normAcc = nullptr;
    const cgltf_accessor* uvAcc = nullptr;
    const cgltf_accessor* jointsAcc = nullptr;
    const cgltf_accessor* weightsAcc = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        const cgltf_attribute& a = prim.attributes[i];
        if (a.index != 0) continue; // only TEXCOORD_0/JOINTS_0/WEIGHTS_0
        switch (a.type) {
        case cgltf_attribute_type_position: posAcc = a.data; break;
        case cgltf_attribute_type_normal: normAcc = a.data; break;
        case cgltf_attribute_type_texcoord: uvAcc = a.data; break;
        case cgltf_attribute_type_joints: jointsAcc = a.data; break;
        case cgltf_attribute_type_weights: weightsAcc = a.data; break;
        default: break;
        }
    }
    if (!posAcc) {
        cgltf_free(gltf);
        result.error = "primitive has no POSITION attribute";
        return result;
    }

    ImportedMesh mesh;
    mesh.vertices.resize(posAcc->count);
    for (cgltf_size i = 0; i < posAcc->count; ++i) {
        ImportedVertex& v = mesh.vertices[i];
        double p[3];
        readVec(posAcc, i, p, 3);
        v.px = p[0];
        v.py = p[1];
        v.pz = p[2];
        if (normAcc) {
            double n[3];
            readVec(normAcc, i, n, 3);
            v.nx = n[0];
            v.ny = n[1];
            v.nz = n[2];
        }
        if (uvAcc) {
            double uv[2];
            readVec(uvAcc, i, uv, 2);
            v.u = uv[0];
            v.v = uv[1];
        }
        if (jointsAcc) {
            double j[4];
            readVec(jointsAcc, i, j, 4);
            // JOINTS_0 components are raw (non-normalized) indices --
            // readVec already returns their exact integer value as a
            // double via cgltf's un-normalized read path.
            for (int k = 0; k < 4; ++k) v.joints[k] = (uint16_t)(j[k] + 0.5);
        }
        if (weightsAcc) {
            double w[4];
            readVec(weightsAcc, i, w, 4);
            for (int k = 0; k < 4; ++k) v.weights[k] = w[k];
        }
    }

    mesh.indices.resize(prim.indices->count);
    for (cgltf_size i = 0; i < prim.indices->count; ++i) {
        mesh.indices[i] = (uint32_t)cgltf_accessor_read_index(prim.indices, i);
    }

    if (prim.material && prim.material->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness& pbr = prim.material->pbr_metallic_roughness;
        for (int i = 0; i < 4; ++i) mesh.baseColorFactor[i] = pbr.base_color_factor[i];
        const cgltf_texture_view& tv = pbr.base_color_texture;
        if (tv.texture && tv.texture->image && tv.texture->image->buffer_view) {
            const cgltf_buffer_view* bv = tv.texture->image->buffer_view;
            const uint8_t* bytes =
                bv->data ? (const uint8_t*)bv->data : (const uint8_t*)bv->buffer->data + bv->offset;
            mesh.baseColorImageBytes.assign(bytes, bytes + bv->size);
        }
    }

    // Skin: find whichever node in the scene references this mesh and has a
    // skin attached (the standard single-rig case this pipeline targets).
    cgltf_skin* skin = nullptr;
    for (cgltf_size i = 0; i < gltf->nodes_count && !skin; ++i) {
        if (gltf->nodes[i].mesh == &srcMesh && gltf->nodes[i].skin) skin = gltf->nodes[i].skin;
    }
    if (skin) {
        if (!skin->inverse_bind_matrices) {
            cgltf_free(gltf);
            result.error = "skin has no inverse bind matrices (required by this pipeline)";
            return result;
        }
        mesh.joints.resize(skin->joints_count);
        for (cgltf_size i = 0; i < skin->joints_count; ++i) {
            ImportedJoint& j = mesh.joints[i];
            cgltf_node* node = skin->joints[i];
            j.name = node->name ? node->name : "";

            float local[16];
            cgltf_node_transform_local(node, local);
            for (int k = 0; k < 16; ++k) j.localBindMatrix[k] = local[k];

            j.parent = -1;
            if (node->parent) {
                for (cgltf_size p = 0; p < skin->joints_count; ++p) {
                    if (skin->joints[p] == node->parent) {
                        j.parent = (int)p;
                        break;
                    }
                }
            }

            double ibm[16];
            readVec(skin->inverse_bind_matrices, i, ibm, 16);
            for (int k = 0; k < 16; ++k) j.inverseBindMatrix[k] = ibm[k];
        }
    }

    cgltf_free(gltf);
    result.ok = true;
    result.mesh = std::move(mesh);
    return result;
}
