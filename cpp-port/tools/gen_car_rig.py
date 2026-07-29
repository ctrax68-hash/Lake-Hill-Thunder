import struct, json, base64, math

# Roadmap Phase 5 / G1 (NASCAR-Thunder gap-analysis plan): replaces the
# original box-chassis placeholder with a real cross-section loft -- a
# sequence of (half-width, top-y, bottom-y) stations running nose-to-tail,
# ribboned into quads -- so the car finally reads as a stock-car silhouette
# (blunt nose/tail, fender shoulders, a greenhouse that steps in narrower and
# up from the beltline, a roof plateau) instead of a single rectangular prism.
#
# The wheels were still simple boxes (add_box()) until G1c (below) replaced
# them with a real radial tire mesh (add_wheel()) -- the skinning/animation
# code addresses each wheel purely by joint index and doesn't care what
# shape is bound to it, so that upgrade needed zero changes outside this
# file.
#
# Critically, the joint/skin structure is untouched: everything the loft
# generates below is bound to joint 0 ("chassis"), translation-only IBM,
# same as before -- wheel_animation.cpp/skinned_mesh.cpp need zero changes.
# mesh_import.h's own "first primitive of first mesh" limit isn't hit either
# -- this is still one mesh, one primitive, just with far more vertices.

CAR_LEN = 5.08
CAR_WID = 2.0
WHEELBASE = 2.79
WHEEL_RADIUS = 0.35
TRACK_HALF = CAR_WID * 0.42  # slightly narrower than full body width
HALF_LEN = CAR_LEN / 2.0

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

# G1c (NASCAR-Thunder gap-analysis plan, wheel/tire mesh upgrade): two
# small fixed-color swatches painted into livery.cpp's currently-unused
# U margin (body paint only occupies u in [0.02,0.78]) -- reviving the
# concept of JS's SW.* solid-color swatches (index.html) for wheel faces
# to sample. These UV constants must match the swatch rects livery.cpp
# paints (see its own G1c comment) -- not derived from a shared header
# since this is a Python generator and a C++ file, same loose-sync
# convention this codebase already uses for carU()/livery band constants.
SW_TREAD = (0.90, 0.25)     # near-black rubber, cylindrical tread band
SW_SIDEWALL = (0.90, 0.75)  # mid-gray sidewall, the two flat end caps

def add_wheel(cx, cy, cz, radius, half_width, joint_idx, sides=10):
    """A radial (cylindrical) tire mesh, replacing the placeholder box --
    thin along local Z (the axle axis, matching the rig's existing
    convention: a real tire is thin along its axle and full-radius in the
    X-Y plane it rolls in). The two flat end caps (normal +-Z) sample
    SW_SIDEWALL; the cylindrical tread band between them (normal radially
    outward in X-Y) samples SW_TREAD -- geometry alone gives the wheel a
    round silhouette from every angle, and the two swatches give it a
    tread/sidewall color distinction without any new texture or sampler.
    `sides` end-cap fan tris x2 + `sides` tread quads (2 tris each): with
    the default sides=10 that's (10-2)*2 + 10*2 = 36 tris/wheel, ~2.9k
    tris total across a 20-car field's 80 wheels -- negligible next to
    the car body loft itself.
    """
    base = len(positions)
    ring = [(math.cos(2 * math.pi * i / sides), math.sin(2 * math.pi * i / sides)) for i in range(sides)]

    def add_vertex(pos, normal, uv):
        positions.append(pos)
        normals.append(normal)
        uvs.append(uv)
        joints0.append((joint_idx, 0, 0, 0))
        weights0.append((1.0, 0.0, 0.0, 0.0))

    # End caps (sidewalls): a center vertex + a ring, fanned into triangles.
    # Normals/winding are hand-set per vertex (not derived from winding, as
    # emit_quad() does), and this renderer applies no backface culling
    # anywhere (renderer.cpp's own convention), so triangle winding here
    # only needs to describe valid triangles, not a specific facing.
    for side_sign in (1, -1):
        z = cz + side_sign * half_width
        center_idx = len(positions)
        add_vertex((cx, cy, z), (0, 0, side_sign), SW_SIDEWALL)
        ring_base = len(positions)
        for (rx, ry) in ring:
            add_vertex((cx + rx * radius, cy + ry * radius, z), (0, 0, side_sign), SW_SIDEWALL)
        for i in range(sides):
            i0, i1 = ring_base + i, ring_base + (i + 1) % sides
            if side_sign > 0:
                indices.extend([center_idx, i0, i1])
            else:
                indices.extend([center_idx, i1, i0])

    # Cylindrical tread band connecting the two rings.
    z0, z1 = cz - half_width, cz + half_width
    ring0_base = len(positions)
    for (rx, ry) in ring:
        add_vertex((cx + rx * radius, cy + ry * radius, z0), (rx, ry, 0), SW_TREAD)
    ring1_base = len(positions)
    for (rx, ry) in ring:
        add_vertex((cx + rx * radius, cy + ry * radius, z1), (rx, ry, 0), SW_TREAD)
    for i in range(sides):
        i2 = (i + 1) % sides
        a0, a1 = ring0_base + i, ring0_base + i2
        b0, b1 = ring1_base + i, ring1_base + i2
        indices.extend([a0, b0, b1, a0, b1, a1])
    return base

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

def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])

def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])

def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]

def _norm(a):
    l = math.sqrt(_dot(a, a))
    return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-9 else (0.0, 0.0, 0.0)

def emit_quad(p0, p1, p2, p3, outward_hint, joint_idx=0):
    """Appends one flat-shaded quad (2 triangles) to the chassis mesh.

    `outward_hint` is a rough direction the face should point; the actual
    normal is computed from the quad's own plane and the winding is flipped
    if it points the wrong way, so callers don't have to hand-verify winding
    order for every non-axis-aligned wall.

    UV (G1b, NASCAR-Thunder gap-analysis plan -- car UV/livery fix): every
    face now gets a real wraparound UV, not just the roof/hood/trunk deck.
    U is the same carU()-style nose-to-tail formula G1 already used for the
    top faces (livery.cpp's own carU(), index.html:2249, ported verbatim --
    this just extends it to every vertex instead of only "is_top" ones). V
    is chosen by corner role so the mesh lines up with what livery.cpp
    already paints at each V band: every station's bottom corners sit at
    world Y == FLOOR_Y exactly (see CHASSIS_STATIONS below -- every station
    tuple's `by` field is FLOOR_Y), so `p[1] == FLOOR_Y` reliably tells a
    rocker/underbody corner from a beltline/roof one, for every face
    (sides, top deck, underbody, nose/tail caps alike) without needing to
    thread per-vertex role flags through the call sites:
      - bottom corners (rocker/seam) -> v=0.945 (p[2]>0) / v=0.055
        (p[2]<0), landing in livery.cpp's near-black rocker/seam bands
        (v in [0,0.052] and [0.948,1.0]) and its wheel-arch shadow rings
        (painted at v=0.055/0.945).
      - top corners (beltline/greenhouse) -> v=0.7 (p[2]>0) / v=0.3
        (p[2]<0), matching the side-window bands livery.cpp already paints
        at v in [0.335,0.410] and [0.590,0.665) and the door-number
        placements (v=0.235 right door, v=0.765 left door) -- both fall
        inside the 0.3/0.7 side-shoulder region this maps onto.
    Nose/tail cap faces mix both corner roles of one station -- no special
    case needed, the same per-point rule naturally lands their bottom
    corners in the rocker tone and top corners in the roof/hood tone, with
    U already pinned near 0.02/0.78 (the nose/tail ends of livery.cpp's own
    U range) since they're the extreme-X stations.
    """
    n = _norm(_cross(_sub(p1, p0), _sub(p3, p0)))
    if _dot(n, outward_hint) < 0:
        p0, p1, p2, p3 = p0, p3, p2, p1
        n = _norm(_cross(_sub(p1, p0), _sub(p3, p0)))
    base = len(positions)
    for p in (p0, p1, p2, p3):
        positions.append(p)
        normals.append(n)
        u = 0.02 + (HALF_LEN - p[0]) / (2.0 * HALF_LEN) * 0.76
        is_bottom = abs(p[1] - FLOOR_Y) < 1e-9
        if is_bottom:
            v = 0.945 if p[2] > 0 else (0.055 if p[2] < 0 else 0.5)
        else:
            v = 0.7 if p[2] > 0 else (0.3 if p[2] < 0 else 0.5)
        uvs.append((u, v))
        joints0.append((joint_idx, 0, 0, 0))
        weights0.append((1.0, 0.0, 0.0, 0.0))
    indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])

# Chassis loft (joint 0): stations run nose(+X) to tail(-X) as
# (x, halfWidthBottom, yBottom, halfWidthTop, yTop). Bottom stays a flat
# undercarriage; top rises from hood height, steps narrower for the
# greenhouse (cowl -> roof -> decklid), then back down to trunk/rear-fender
# height -- the same nose/cowl/greenhouse/deck-station sequence the roadmap
# plan calls for. Fender half-widths (1.08/1.10) are kept comfortably wider
# than the wheel's own outer edge (TRACK_HALF + wheel half-width, ~1.05) so
# the wheels sit visibly tucked under the body instead of poking through it.
FLOOR_Y = 0.45
CHASSIS_STATIONS = [
    (HALF_LEN,        0.15, FLOOR_Y, 0.15, 0.55),  # nose tip / front fascia
    (HALF_LEN - 0.49,  1.08, FLOOR_Y, 0.95, 0.74),  # front bumper -> hood start
    (HALF_LEN - 1.04,  1.10, FLOOR_Y, 0.95, 0.80),  # front fender peak (over front wheel)
    (HALF_LEN - 1.59,  0.92, FLOOR_Y, 0.60, 0.92),  # cowl / windshield base (steps in+up)
    (HALF_LEN - 2.14,  0.92, FLOOR_Y, 0.56, 1.32),  # windshield top / roof front
    (-(HALF_LEN - 2.14), 0.92, FLOOR_Y, 0.56, 1.32),  # roof rear (plateau)
    (-(HALF_LEN - 1.59), 0.92, FLOOR_Y, 0.60, 0.92),  # rear window base / decklid start
    (-(HALF_LEN - 1.04), 1.10, FLOOR_Y, 0.95, 0.78),  # rear fender peak (over rear wheel)
    (-(HALF_LEN - 0.49), 1.08, FLOOR_Y, 0.95, 0.66),  # rear bumper / trunk
    (-HALF_LEN,          0.15, FLOOR_Y, 0.15, 0.55),  # tail tip / rear fascia
]

def _corners(station):
    x, bw, by, tw, ty = station
    return {
        "BR": (x, by, bw), "BL": (x, by, -bw),
        "TR": (x, ty, tw), "TL": (x, ty, -tw),
    }

for i in range(len(CHASSIS_STATIONS) - 1):
    c0 = _corners(CHASSIS_STATIONS[i])
    c1 = _corners(CHASSIS_STATIONS[i + 1])
    emit_quad(c0["BR"], c1["BR"], c1["TR"], c0["TR"], (0, 0, 1))   # right side wall
    emit_quad(c0["BL"], c0["TL"], c1["TL"], c1["BL"], (0, 0, -1))  # left side wall
    emit_quad(c0["TL"], c1["TL"], c1["TR"], c0["TR"], (0, 1, 0))   # top deck (hood/roof/decklid)
    emit_quad(c0["BL"], c0["BR"], c1["BR"], c1["BL"], (0, -1, 0))  # underbody

nose = _corners(CHASSIS_STATIONS[0])
emit_quad(nose["BR"], nose["BL"], nose["TL"], nose["TR"], (1, 0, 0))   # nose cap
tail = _corners(CHASSIS_STATIONS[-1])
emit_quad(tail["BL"], tail["BR"], tail["TR"], tail["TL"], (-1, 0, 0))  # tail cap

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
    # G1c (NASCAR-Thunder gap-analysis plan, wheel/tire mesh upgrade): a
    # real radial tire mesh (add_wheel()) instead of a box -- still thin
    # along local Z (lateral/axle direction), full radius in the X-Y
    # (forward/vertical) rolling plane, same axis convention the box-wheel
    # orientation fix already established (a real tire is thin along its
    # axle and full-radius in the plane it rolls in). The box shape itself
    # (as opposed to which axis it used) was always a placeholder --
    # gen_car_rig.py's own module docstring called it "an acceptable
    # short-term simplification" pending exactly this replacement.
    add_wheel(wx, WHEEL_RADIUS, wz, WHEEL_RADIUS, WHEEL_RADIUS * 0.6, joint_idx=i + 1)

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
    f.write("// Roadmap Phase 5 / G1 (NASCAR-Thunder gap-analysis plan): a\n")
    f.write("// lofted car rig -- a real nose/cowl/greenhouse/deck silhouette for the\n")
    f.write("// chassis (joint \"chassis\"), still-simplified box wheels (joints\n")
    f.write("// \"wheel_FL\"/\"wheel_FR\"/\"wheel_RL\"/\"wheel_RR\") -- generated by\n")
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
