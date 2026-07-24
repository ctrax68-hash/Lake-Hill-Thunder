import struct, json, base64, math

# Step 3 (PORT_PROGRESS.md, physics-driven car rig animation): generates a
# placeholder car rig -- a box chassis + 4 box "wheels", each wheel a child
# joint of the chassis root -- for wiring SkinnedMesh into the real renderer
# before a real Unreal-authored rig exists (Step 2's mesh_import.h can only
# import the first primitive of the first mesh, so chassis+wheels share one
# vertex stream/material -- wheels sample the same livery texture as the
# body, at a fixed UV point, rather than a separate material).
#
# Deliberate simplification: wheel joints are translation-only (no axle-
# orientation rotation), so computeBonePalette()'s rotateX(spin) rotates
# each wheel box around the chassis's own nose-tail axis, not the true
# lateral axle axis. There's no strongly "correct" rolling axis for a box
# to preserve visually anyway (a real Unreal rig would define each wheel
# joint's own local frame with X aligned to the true axle, needing no
# change to the animation code) -- this keeps the placeholder-generation
# script simple (matches tests/fixtures/skinned_wheel_test.gltf's own
# translation-only precedent) while still exercising the real skinning
# math end to end.

CAR_LEN = 5.08
CAR_WID = 2.0
WHEELBASE = 2.79
WHEEL_RADIUS = 0.35
TRACK_HALF = CAR_WID * 0.42  # slightly narrower than full body width

positions = []
normals = []
uvs = []
joints0 = []
weights0 = []
indices = []

FACES = [
    # (normal, corner offsets as (sx,sy,sz) multipliers of half-extent)
    ((0, 1, 0), [(-1, 1, -1), (1, 1, -1), (1, 1, 1), (-1, 1, 1)]),   # +Y top
    ((0, -1, 0), [(-1, -1, 1), (1, -1, 1), (1, -1, -1), (-1, -1, -1)]), # -Y bottom
    ((1, 0, 0), [(1, -1, -1), (1, 1, -1), (1, 1, 1), (1, -1, 1)]),   # +X
    ((-1, 0, 0), [(-1, -1, 1), (-1, 1, 1), (-1, 1, -1), (-1, -1, -1)]), # -X
    ((0, 0, 1), [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]),   # +Z
    ((0, 0, -1), [(1, -1, -1), (-1, -1, -1), (-1, 1, -1), (1, 1, -1)]), # -Z
]

def add_box(cx, cy, cz, hx, hy, hz, joint_idx, top_livery_uv):
    base = len(positions)
    for (nx, ny, nz), corners in FACES:
        face_base = len(positions)
        for (sx, sy, sz) in corners:
            positions.append((cx + sx * hx, cy + sy * hy, cz + sz * hz))
            normals.append((nx, ny, nz))
            if top_livery_uv and (nx, ny, nz) == (0, 1, 0):
                # Reuse the old flat-quad's carU()-style mapping: nose->0.02,
                # tail->0.78 along local X; roof-straddling band along Z.
                u = 0.02 + (hx - sx * hx) / (2.0 * hx) * 0.76
                v = 0.5 + (sz * hz / hz) * 0.20
            else:
                u, v = 0.4, 0.5  # a plain body-color sample away from any edge
            uvs.append((u, v))
            joints0.append((joint_idx, 0, 0, 0))
            weights0.append((1.0, 0.0, 0.0, 0.0))
        indices.extend([face_base, face_base + 1, face_base + 2,
                         face_base, face_base + 2, face_base + 3])
    return base

# Chassis (joint 0): sits from just above the wheel hubs up to a plausible
# roof height.
add_box(0.0, WHEEL_RADIUS * 1.3 + 0.45, 0.0, CAR_LEN / 2.0, 0.45, CAR_WID / 2.0,
        joint_idx=0, top_livery_uv=True)

# Wheels (joints 1-4): FL, FR, RL, RR. Local X = nose(+)/tail(-) offset from
# chassis origin; local Z = left(+)/right(-); local Y = wheel-radius (so the
# wheel's own box, spanning +-radius around its joint origin, touches down
# at world Y=0, matching the ground-height reference stepCar()'s c.x/c.y/
# surface height already provide).
wheel_offsets = [
    (WHEELBASE / 2.0, TRACK_HALF),   # FL
    (WHEELBASE / 2.0, -TRACK_HALF),  # FR
    (-WHEELBASE / 2.0, TRACK_HALF),  # RL
    (-WHEELBASE / 2.0, -TRACK_HALF), # RR
]
for i, (wx, wz) in enumerate(wheel_offsets):
    add_box(wx, WHEEL_RADIUS, wz, WHEEL_RADIUS * 0.6, WHEEL_RADIUS, WHEEL_RADIUS,
            joint_idx=i + 1, top_livery_uv=False)

def pack_f32(vals):
    return b"".join(struct.pack("<f", v) for tup in vals for v in tup)

def pack_u16(vals):
    return b"".join(struct.pack("<H", v) for tup in vals for v in tup)

def pack_u16_scalar(vals):
    return b"".join(struct.pack("<H", v) for v in vals)

pos_bytes = pack_f32(positions)
norm_bytes = pack_f32(normals)
uv_bytes = pack_f32(uvs)
joints_bytes = pack_u16(joints0)
weights_bytes = pack_f32(weights0)
idx_bytes = pack_u16_scalar(indices)

# Inverse bind matrices: translation-only bind poses (see the module
# docstring above), so IBM is just the negated translation, identity
# rotation -- column-major 4x4, translation in the last column.
def ibm_translate(x, y, z):
    m = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, -z, 1]
    return m

joint_translations = [(0.0, 0.0, 0.0)] + [(wx, WHEEL_RADIUS, wz) for wx, wz in wheel_offsets]
ibm_flat = []
for (x, y, z) in joint_translations:
    ibm_flat.extend(ibm_translate(x, y, z))
ibm_bytes = pack_f32([tuple(ibm_flat[i:i + 4]) for i in range(0, len(ibm_flat), 4)])

def align4(b):
    pad = (-len(b)) % 4
    return b + b"\x00" * pad

buffers = [pos_bytes, norm_bytes, uv_bytes, joints_bytes, weights_bytes, idx_bytes, ibm_bytes]
offsets = []
blob = b""
for b in buffers:
    b = align4(b)
    offsets.append((len(blob), len(b)))
    blob += b

(pos_off, pos_len), (norm_off, norm_len), (uv_off, uv_len), (joints_off, joints_len), \
    (weights_off, weights_len), (idx_off, idx_len), (ibm_off, ibm_len) = offsets

joint_names = ["chassis", "wheel_FL", "wheel_FR", "wheel_RL", "wheel_RR"]
nodes = []
for i, name in enumerate(joint_names):
    node = {"name": name}
    if i == 0:
        node["children"] = [1, 2, 3, 4]
        node["mesh"] = 0
        node["skin"] = 0
    else:
        wx, wz = wheel_offsets[i - 1]
        node["translation"] = [wx, WHEEL_RADIUS, wz]
    nodes.append(node)

gltf = {
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": nodes,
    "meshes": [{
        "name": "carPlaceholder",
        "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "JOINTS_0": 3, "WEIGHTS_0": 4},
            "indices": 5,
            "material": 0,
        }],
    }],
    "materials": [{
        "name": "carBody",
        "pbrMetallicRoughness": {"baseColorFactor": [0.85, 0.85, 0.85, 1.0]},
    }],
    "skins": [{
        "joints": [0, 1, 2, 3, 4],
        "inverseBindMatrices": 6,
    }],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3"},
        {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3"},
        {"bufferView": 2, "componentType": 5126, "count": len(uvs), "type": "VEC2"},
        {"bufferView": 3, "componentType": 5123, "count": len(joints0), "type": "VEC4"},
        {"bufferView": 4, "componentType": 5126, "count": len(weights0), "type": "VEC4"},
        {"bufferView": 5, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
        {"bufferView": 6, "componentType": 5126, "count": 5, "type": "MAT4"},
    ],
    "bufferViews": [
        {"buffer": 0, "byteOffset": pos_off, "byteLength": pos_len},
        {"buffer": 0, "byteOffset": norm_off, "byteLength": norm_len},
        {"buffer": 0, "byteOffset": uv_off, "byteLength": uv_len},
        {"buffer": 0, "byteOffset": joints_off, "byteLength": joints_len},
        {"buffer": 0, "byteOffset": weights_off, "byteLength": weights_len},
        {"buffer": 0, "byteOffset": idx_off, "byteLength": idx_len},
        {"buffer": 0, "byteOffset": ibm_off, "byteLength": ibm_len},
    ],
    "buffers": [{
        "byteLength": len(blob),
        "uri": "data:application/octet-stream;base64," + base64.b64encode(blob).decode("ascii"),
    }],
}

json_text = json.dumps(gltf)
print("JSON size:", len(json_text), "blob size:", len(blob), "verts:", len(positions), "indices:", len(indices))

with open("/tmp/car_rig_preview.gltf", "w") as f:
    f.write(json_text)

# Emit as a C++ header: a raw string literal (no escaping needed for JSON
# text as long as it contains no `)"` sequence -- base64 data + JSON
# structural characters never produce that).
assert ')"' not in json_text, "raw string delimiter collision, pick a longer delimiter"
with open("../src/render/car_rig_data.h", "w") as f:
    f.write("#pragma once\n\n")
    f.write("// Step 3 (PORT_PROGRESS.md, physics-driven car rig animation): a\n")
    f.write("// placeholder car rig (box chassis + 4 box wheels, joints \"chassis\"/\n")
    f.write("// \"wheel_FL\"/\"wheel_FR\"/\"wheel_RL\"/\"wheel_RR\"), generated by\n")
    f.write("// tools/gen_car_rig.py -- NOT a real art asset. Embedded as a C++ string\n")
    f.write("// literal (matching this port's \"no external runtime assets\" convention\n")
    f.write("// -- every other texture/mesh in this renderer is procedurally generated\n")
    f.write("// in code, not loaded from a file at runtime) rather than staged as a\n")
    f.write("// loose .glb Renderer::init() would need to open from disk. Swappable\n")
    f.write("// later for a real Unreal-authored rig without touching mesh_import.h/\n")
    f.write("// skinned_mesh.h/wheel_animation.h at all -- only this one file changes.\n")
    f.write("inline const char* const kCarRigGltfJson = R\"(")
    f.write(json_text)
    f.write(")\";\n")

print("wrote src/render/car_rig_data.h")
