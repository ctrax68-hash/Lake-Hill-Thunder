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
# H1: 0.76, not the old CAR_WID*0.42 = 0.84. A real Gen-4 runs ~60in track
# with ~11in tires, so the tire centreline sits at 0.762 and its inner face
# lands at 0.762-0.14 = 0.62 -- which is exactly index.html's WHEEL_INNER_Z,
# the constant its wheel-arch relief is written against. At 0.84 the tires
# sat 0.08 too far outboard: their outer face reached 0.98 while the widest
# bodywork is only 0.95, so the tires stuck out past the fenders AND the
# JS-tuned relief band no longer described where the tire actually was.
# The H1 decode check caught the body passing through the tire barrel at the
# fender because of it.
TRACK_HALF = 0.76
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
SW_SIDEWALL = (0.90, 0.75)  # near-black rubber, the outer end-cap annulus

# I1 (car visual fidelity plan): the wheel end caps used to be a single fan
# sampling one fixed UV point (SW_SIDEWALL) for every vertex -- structurally
# incapable of ever showing a rim/tire distinction no matter what that one
# swatch is painted, since every vertex decodes to the identical texel. Real
# reference NASCAR Thunder cars show a clearly separate metallic hub from
# the black tire plus a lighter sidewall-lettering band, so add_wheel()
# below now builds each cap as three concentric annuli/disc, each sampling
# its own swatch. Placed at u=0.815, NOT the naively-free-looking u=0.80:
# livery.cpp's nose/tail lamp decals (kTailU0=0.764, kTailUW=0.028) actually
# reach u=0.792, past the body wrap's own U1=0.78 -- the true open margin is
# (0.792, 0.85), not (0.78, 0.85). u=0.83-0.85 stays reserved for I2's
# mirror swatch, a separate column in the same margin.
SW_TIRE_LETTER = (0.815, 0.25)  # lighter "lettering" annulus, inside the rubber
SW_RIM = (0.815, 0.75)          # bright metallic hub disc, innermost

# I2 (car visual fidelity plan): a fixed dark plastic/trim swatch for the
# new door mirror -- a separate column (u=0.835) from I1's tire swatches
# (u=0.815) in the same reserved margin, still clear of the tail lamp
# decal's real extent (u<=0.792, see I1's own comment for why the naive
# "free" boundary was wrong) and the wheel/spoiler columns (u>=0.85).
SW_MIRROR = (0.835, 0.5)

# G8 (Gen-4 car overhaul): two more fixed-color swatches for the new
# spoiler, in the same reserved-UV-margin column as the G1c wheel swatches
# above but a separate row so they don't overlap.
SW_SPOILER_BODY = (0.97, 0.25)  # top blade face -- painted per-car (body color)
SW_SPOILER_DARK = (0.97, 0.75)  # underside/edges/risers -- fixed near-black

# J5 (car visual fidelity plan, part 2): a single center-lock wheel hub nut,
# module-level (not a local inside add_wheel()) so check_car_rig.py can
# reference the exact same geometry constants by name -- the same loose
# cross-file convention the SW_* swatches above already use, just within
# this one file instead of across gen_car_rig.py/livery.cpp. Fraction of
# R_INNER (add_wheel()'s own inner-disc radius), not an absolute size, so
# it scales automatically if WHEEL_RADIUS ever changes.
# R2b: 0.22 -> 0.28 purely to HOLD the nut's absolute size. It is a fraction
# of R_INNER, and R_INNER moved 0.68 -> 0.54 of the radius when the rim face
# was cut back to a real 15-inch wheel: 0.54*0.28 == 0.68*0.22 to three
# decimals, so the nut on screen is unchanged.
_HUB_R_FRAC = 0.28
_HUB_SIDES = 8
_HUB_Z_OFFSET_FRAC = 0.06  # fraction of half_width; avoids z-fighting
                           # against the coplanar SW_RIM disc beneath it,
                           # not a visible-gap risk like the mirror's own
                           # embed-vs-float tuning -- a different problem.

# R2b: WHEEL PROPORTIONS, all fractions of the wheel's own radius/half_width.
#
# The turntable is what found this. I1 built the end cap as three concentric
# bands with the metallic rim disc reaching 0.68 of the radius, so the black
# rubber was reduced to a 0.22-wide outer ring -- from every angle the wheel
# renders as a pale disc with a thin dark edge, which is exactly what "fix the
# wheels" is pointing at. A Gen-4 ran a 15 in wheel inside a ~28 in tire, so
# the rim face is 0.54 of the radius and the SIDEWALL is the dominant feature,
# not the rim.
#
# Three other things the old wheel could not express, all cheap here:
#   * the tread was a straight cylinder, so the tire had a hard 90 deg corner
#     at each face -- it read as a coin. WHEEL_R_SHOULDER/WHEEL_Z_CROWN round
#     it into the sidewall.
#   * the rim face was coplanar with the tire's outer face. Real wheels are
#     dished; WHEEL_Z_DISH recesses it so the arch has depth to it.
#   * the rim face was one flat fan, structurally incapable of showing spokes
#     (the same argument I1's own docstring makes about swatch fans). The face
#     is now built as WHEEL_RIM_SECTORS separate wedges, alternating rim metal
#     and near-black, giving WHEEL_SPOKES spokes and a dark gap between each.
WHEEL_R_SHOULDER = 0.90   # tread rolls into the sidewall here
WHEEL_R_LETTER_O = 0.80   # sidewall lettering ring, outer edge
WHEEL_R_LETTER_I = 0.73   # sidewall lettering ring, inner edge
WHEEL_R_BEAD = 0.54       # tire bead / rim flange -- a 15 in wheel in a 28 in tire
WHEEL_Z_CROWN = 0.72      # tread crown, as a fraction of half_width
WHEEL_Z_DISH = 0.34       # rim face recessed inboard, as a fraction of half_width
WHEEL_SPOKES = 5
WHEEL_RIM_SECTORS = 20    # 4 sectors per spoke: 2 metal, 2 open
assert WHEEL_RIM_SECTORS % (WHEEL_SPOKES * 2) == 0, "sectors must split evenly into spoke/gap pairs"

def emit_swatch_quad(p0, p1, p2, p3, outward_hint, swatch, joint_idx=0):
    """Like emit_quad(), but every vertex samples one fixed swatch texel
    instead of the carU()/carV() wraparound livery UV -- for small
    flat-color parts (the spoiler) that don't need per-vertex livery
    paint, matching the SW_TREAD/SW_SIDEWALL precedent above."""
    n = _norm(_cross(_sub(p1, p0), _sub(p3, p0)))
    if _dot(n, outward_hint) < 0:
        p0, p1, p2, p3 = p0, p3, p2, p1
        n = _norm(_cross(_sub(p1, p0), _sub(p3, p0)))
    base = len(positions)
    for p in (p0, p1, p2, p3):
        positions.append(p)
        normals.append(n)
        uvs.append(swatch)
        joints0.append((joint_idx, 0, 0, 0))
        weights0.append((1.0, 0.0, 0.0, 0.0))
    indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])

def add_wheel(cx, cy, cz, radius, half_width, joint_idx, sides=10):
    """A radial (cylindrical) tire mesh, replacing the placeholder box --
    thin along local Z (the axle axis, matching the rig's existing
    convention: a real tire is thin along its axle and full-radius in the
    X-Y plane it rolls in). The cylindrical tread band (normal radially
    outward in X-Y) samples SW_TREAD -- geometry alone gives the wheel a
    round silhouette from every angle.

    I1 (car visual fidelity plan): each flat end cap is now three
    concentric bands rather than one single-swatch fan -- an outer rubber
    annulus (SW_SIDEWALL), a thin lighter "lettering" annulus
    (SW_TIRE_LETTER), and an inner metallic hub disc (SW_RIM). A fan whose
    every vertex samples one fixed UV point is structurally incapable of
    ever showing a rim/tire distinction no matter what that single swatch
    is painted -- every vertex decodes to the identical texel -- so real
    concentric geometry is required, not just a repaint. Each band's own
    two boundary rings are separate vertices even where they sit at the
    same 3D position (same "flat swatch" convention emit_swatch_quad()
    already uses: every vertex of a same-color region must share one
    swatch UV, so two differently-colored bands meeting at a radius can't
    share vertices there without one of them sampling the wrong swatch).

    Triangle cost: per cap, `sides*2` (outer annulus) + `sides*2` (mid
    annulus) + `sides` (inner disc fan) = `sides*5`; two caps + the
    `sides*2` tread band = `sides*12` per wheel. At sides=16 (this rig's
    actual call-site value) that's 192 tris/wheel, ~15.4k across a 20-car
    field's 80 wheels -- still negligible next to the car body loft's own
    720 triangles x20 cars.
    """
    base = len(positions)
    ring = [(math.cos(2 * math.pi * i / sides), math.sin(2 * math.pi * i / sides)) for i in range(sides)]

    def add_vertex(pos, normal, uv):
        positions.append(pos)
        normals.append(normal)
        uvs.append(uv)
        joints0.append((joint_idx, 0, 0, 0))
        weights0.append((1.0, 0.0, 0.0, 0.0))

    # R2b: end-cap radii. R_INNER is the rim face's outer edge and is now the
    # tire BEAD (0.54 of the radius), not 0.68 -- see the WHEEL_R_* block for
    # why. R_OUTER is the shoulder, where the rounded tread hands off to the
    # sidewall, rather than the full radius.
    R_OUTER = radius * WHEEL_R_SHOULDER
    R_LET_O = radius * WHEEL_R_LETTER_O
    R_LET_I = radius * WHEEL_R_LETTER_I
    R_INNER = radius * WHEEL_R_BEAD

    # End caps: normals/winding are hand-set per vertex (not derived from
    # winding, as emit_quad() does), and this renderer applies no backface
    # culling anywhere (renderer.cpp's own convention), so triangle winding
    # here only needs to describe valid triangles, not a specific facing --
    # the side_sign-based order flip below just keeps the two caps
    # consistent with each other and with this file's own care elsewhere
    # about winding, not because culling would otherwise hide anything.
    for side_sign in (1, -1):
        z = cz + side_sign * half_width
        n = (0, 0, side_sign)

        def make_ring(r, swatch):
            ring_base = len(positions)
            for (rx, ry) in ring:
                add_vertex((cx + rx * r, cy + ry * r, z), n, swatch)
            return ring_base

        def annulus_tris(outer_base, inner_base):
            for i in range(sides):
                i2 = (i + 1) % sides
                a0, a1 = outer_base + i, outer_base + i2
                b0, b1 = inner_base + i, inner_base + i2
                if side_sign > 0:
                    indices.extend([a0, b0, b1, a0, b1, a1])
                else:
                    indices.extend([a0, b1, b0, a0, a1, b1])

        # R2b: the INNER cap faces the chassis and lives inside the wheelhouse.
        # It is never visible from any camera this game has, so it gets one
        # flat rubber disc rather than the four-band treatment -- that pays for
        # the extra outer-cap detail below instead of growing the wheel's
        # triangle count. It still exists, so the tire is a closed solid if a
        # spin or the mirror ever does show it edge-on.
        outer_face = (side_sign == (1 if cz > 0 else -1))
        if not outer_face:
            flat = make_ring(R_OUTER, SW_SIDEWALL)
            flat_c = len(positions)
            add_vertex((cx, cy, z), n, SW_SIDEWALL)
            for i in range(sides):
                i0, i1 = flat + i, flat + (i + 1) % sides
                indices.extend([flat_c, i1, i0])
            continue

        # Sidewall, outer half.
        annulus_tris(make_ring(R_OUTER, SW_SIDEWALL), make_ring(R_LET_O, SW_SIDEWALL))
        # The lettering ring, sitting ON the sidewall the way a real one does,
        # rather than being the last thing before the rim as it was in I1.
        annulus_tris(make_ring(R_LET_O, SW_TIRE_LETTER), make_ring(R_LET_I, SW_TIRE_LETTER))
        # Sidewall, inner half, down to the bead.
        annulus_tris(make_ring(R_LET_I, SW_SIDEWALL), make_ring(R_INNER, SW_SIDEWALL))

        # R2b: the rim face as WHEEL_SPOKES spokes with dark gaps between
        # them, and dished inboard so the face is not coplanar with the tire.
        # Each sector is its own three vertices because adjacent sectors carry
        # different swatches and a shared vertex can only sample one of them --
        # the same constraint that forced I1's concentric bands apart.
        z_rim = z - side_sign * (half_width * WHEEL_Z_DISH)
        _per = WHEEL_RIM_SECTORS // WHEEL_SPOKES        # 4
        for s in range(WHEEL_RIM_SECTORS):
            a0 = 2 * math.pi * s / WHEEL_RIM_SECTORS
            a1 = 2 * math.pi * (s + 1) / WHEEL_RIM_SECTORS
            sw = SW_RIM if (s % _per) < _per // 2 else SW_TREAD
            c_i = len(positions)
            add_vertex((cx, cy, z_rim), n, sw)
            add_vertex((cx + math.cos(a0) * R_INNER, cy + math.sin(a0) * R_INNER, z), n, sw)
            add_vertex((cx + math.cos(a1) * R_INNER, cy + math.sin(a1) * R_INNER, z), n, sw)
            if side_sign > 0:
                indices.extend([c_i, c_i + 1, c_i + 2])
            else:
                indices.extend([c_i, c_i + 2, c_i + 1])

        # J5 (car visual fidelity plan, part 2): a single center-lock hub
        # nut, not 5 street-car lug nuts -- period-correct for the Gen-4/Cup
        # cars this project is visually inspired by (a single knock-off hub
        # nut, not a 5-lug pattern), cheaper, and a larger single element
        # reads better at chase-cam distance than five tiny ones would.
        #
        # Outer cap only: `cz` is always +-TRACK_HALF (never 0), so the
        # outer face -- the only one ever seen from a supported camera
        # angle -- is unambiguously the side where side_sign matches
        # sign(cz). The inner cap (facing the chassis) never gets this
        # geometry, so no triangles are spent on it.
        #
        # Bound to THIS wheel's own joint_idx (inherited from the enclosing
        # scope), NOT chassis joint 0 like every other prop in this file
        # (spoiler/mirrors/exhaust are chassis-bound because they don't move
        # independently) -- this nut is on the wheel face and must spin
        # with it via wheel_animation.cpp's bone palette. Getting this wrong
        # would be a bug invisible in any single still screenshot (the nut
        # would sit still while the tire visibly rotates around it), which
        # is exactly why check_car_rig.py asserts the joint binding directly
        # rather than trusting a screenshot to catch it.
        if outer_face:
            _hub_r = R_INNER * _HUB_R_FRAC
            # R2b: measured from the DISHED rim face, not the tire's outer
            # plane -- otherwise the nut floats half a wheel-width proud of
            # the face it is supposed to be bolted to.
            _hub_z = z_rim + side_sign * (half_width * _HUB_Z_OFFSET_FRAC)
            _hub_ring_base = len(positions)
            for _hi in range(_HUB_SIDES):
                _ha = 2 * math.pi * _hi / _HUB_SIDES
                add_vertex((cx + math.cos(_ha) * _hub_r, cy + math.sin(_ha) * _hub_r, _hub_z), n, SW_RIM)
            _hub_center = len(positions)
            add_vertex((cx, cy, _hub_z), n, SW_RIM)
            for _hi in range(_HUB_SIDES):
                _hi0, _hi1 = _hub_ring_base + _hi, _hub_ring_base + (_hi + 1) % _HUB_SIDES
                if side_sign > 0:
                    indices.extend([_hub_center, _hi0, _hi1])
                else:
                    indices.extend([_hub_center, _hi1, _hi0])

    # R2b: tread band with a ROUNDED SHOULDER. Four rings instead of two --
    # the crown runs flat across the middle at full radius, then each edge
    # falls away to WHEEL_R_SHOULDER where it meets the end cap, so the tire
    # turns into its sidewall instead of ending in a 90 deg corner. That hard
    # corner is most of why the old wheel read as a stamped coin: a cylinder
    # has one silhouette from every angle and no highlight rolls across it.
    #
    # Normals are radial-plus-axial on the shoulder rings so the fall-off
    # actually shades as a curve; the crown rings stay purely radial, which
    # keeps the flat middle of the tread reading flat.
    _sh = radius * WHEEL_R_SHOULDER
    _zc = half_width * WHEEL_Z_CROWN
    _bands = [(-half_width, _sh, -1.0), (-_zc, radius, 0.0),
              (_zc, radius, 0.0), (half_width, _sh, 1.0)]
    _prev = None
    for (_dz, _r, _na) in _bands:
        _b = len(positions)
        _nl = math.hypot(1.0, _na)
        for (rx, ry) in ring:
            add_vertex((cx + rx * _r, cy + ry * _r, cz + _dz),
                       (rx / _nl, ry / _nl, _na / _nl), SW_TREAD)
        if _prev is not None:
            for i in range(sides):
                i2 = (i + 1) % sides
                a0, a1 = _prev + i, _prev + i2
                b0, b1 = _b + i, _b + i2
                indices.extend([a0, b0, b1, a0, b1, a1])
        _prev = _b
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

def emit_smooth_quad(v0, v1, v2, v3, outward_hint, joint_idx=0):
    """H1: appends one SMOOTH-shaded quad to the chassis mesh.

    Each argument is a (position, normal, uv) triple -- unlike emit_quad()
    below, the normal is supplied per vertex by the caller rather than
    derived from the quad's own plane, which is the whole point: the loft's
    normals now come from the surrounding surface's tangents (see
    ring_normals()), so adjacent quads share a continuous normal field
    instead of each reading as its own flat plate.

    Winding is still corrected against `outward_hint` so callers don't have
    to hand-verify it, exactly as emit_quad() does.
    """
    p0, p1, p2, p3 = v0[0], v1[0], v2[0], v3[0]
    face = _norm(_cross(_sub(p1, p0), _sub(p3, p0)))
    if _dot(face, outward_hint) < 0:
        v0, v1, v2, v3 = v0, v3, v2, v1
    base = len(positions)
    for (p, n, uv) in (v0, v1, v2, v3):
        positions.append(p)
        normals.append(n)
        uvs.append(uv)
        joints0.append((joint_idx, 0, 0, 0))
        weights0.append((1.0, 0.0, 0.0, 0.0))
    indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])

def emit_smooth_tri(v0, v1, v2, outward_hint, joint_idx=0):
    """H1: the triangle form of emit_smooth_quad(), for the nose/tail cap
    fans -- a ring can be closed by a fan to a single apex, which a
    4-corner box section had no way to express."""
    p0, p1, p2 = v0[0], v1[0], v2[0]
    face = _norm(_cross(_sub(p1, p0), _sub(p2, p0)))
    if _dot(face, outward_hint) < 0:
        v0, v1, v2 = v0, v2, v1
    base = len(positions)
    for (p, n, uv) in (v0, v1, v2):
        positions.append(p)
        normals.append(n)
        uvs.append(uv)
        joints0.append((joint_idx, 0, 0, 0))
        weights0.append((1.0, 0.0, 0.0, 0.0))
    indices.extend([base, base + 1, base + 2])

def emit_quad(p0, p1, p2, p3, outward_hint, joint_idx=0):
    """Appends one flat-shaded quad (2 triangles) to the chassis mesh.

    Superseded for the chassis loft by emit_smooth_quad() above (H1); kept
    because its UV derivation is the reference the ring's own carU()/carV()
    still matches, and because add_box() callers may want it again.

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

# G8 (NASCAR-Thunder gap-analysis plan, Gen-4 car overhaul): chassis loft
# ported from index.html's own already-tuned Gen-4 stock-car loft
# (buildCarMesh()'s CAR_ST, real-world 198in x 75in x 51in / 110in
# wheelbase / 60in spoiler dims) rather than re-deriving proportions from
# scratch -- that JS rig went through many rounds of visual nudging in its
# own project history to land on this exact silhouette (low wide nose,
# fender bulges over both axles, pinched greenhouse, long flat deck).
#
# Stations run nose(+X) to tail(-X) as (x, halfWidthBottom, yBottom,
# halfWidthTop, yTop). yBottom stays the flat FLOOR_Y undercarriage (a
# deliberate simplification kept from the original G1 loft -- JS's own
# per-station yLow taper isn't ported). x is JS's CAR_ST x scaled by
# HALF_LEN/2.51 (~1.012, negligible) so the nose/tail stations land
# exactly on +-HALF_LEN, keeping emit_quad()'s U mapping exact at the
# tips. halfWidthBottom is JS's halfWidth directly; yTop is JS's roofY
# directly -- JS's own "beltline rises continuously, cabin only humps up
# across the greenhouse" effect is already baked into roofY as data (it
# only exceeds beltY across the cowl-through-fastback rows), so no
# separate shoulder-fraction code is needed, just this one column.
# halfWidthTop is halfWidthBottom scaled by 0.60 across the 5 greenhouse
# rows (cowl through fastback-glass-start) or 0.92 elsewhere, matching
# the ratio this loft's own prior (10-station) table already used at its
# one greenhouse row.
FLOOR_Y = 0.45

# Wheel-arch relief (ported from JS's ringPts(): only ring points near the
# rocker/bottom ever reach into a wheel's cylindrical volume -- with this
# loft's simple 4-corner trapezoid cross-section (not JS's 14-point round
# ring), that collapses to "pull only the bottom half-width in, near each
# axle" -- applied to halfWidthBottom alone, halfWidthTop (the visible
# fender bulge above the tire) is computed from the PRE-relief width so the
# flare itself stays full width; only the rocker corner right at the wheel
# gets tucked in so the body doesn't visually poke through the tire.
# H1 (NT2003 engine-feel plan): the loft finally uses JS's real 14-point
# round ring. This file's own comment used to say so plainly -- "with this
# loft's simple 4-corner trapezoid cross-section (NOT JS's 14-point round
# ring)" -- which is exactly why the body read as a faceted tube: a
# 4-corner section is a trapezoid with hard 90-degree edges at the rocker,
# the shoulder and the roof, and no amount of shading can round something
# that has no roundness in it. fs_car.sc's G2 specular/Fresnel/env terms
# had nothing to catch, so the paint looked matte no matter how it was
# tuned.
#
# RINGF, SHOULDER, NK and the wheel-relief constants are all transcribed
# from index.html (ringPts(), :2419-2446). The ring runs from the
# bottom-RIGHT rocker (k=0, z=+0.76w) up the right flank, across the roof,
# and down to the bottom-LEFT rocker (k=NK-1) -- an open arc; the underbody
# closes it (see the loft loop).
# R1: re-proportioned to a Gen-4 stock-car section. NK is deliberately
# UNCHANGED at 14, and every k index keeps its meaning, because car_v() maps V
# from the ring INDEX (`0.97 - k/(NK-1) * 0.94`), not from height -- so every
# livery band in livery.cpp (beltline 0.677, glass GV0/GVH, the roof/hood
# highlight span, the rocker shadow) is pinned to these indices. Changing the
# count would silently slide all of them. Only the WIDTH fractions move here.
#
# What changed and why (old -> new):
#   k4 beltline  0.885 -> 0.97   flat slab side, not a rolled shoulder
#   k5 glass     0.58  -> 0.86   real greenhouse tumblehome, not a wedge
#   k6 roof      0.22  -> 0.72   THE big one: the roof was a 0.22-halfwidth
#                                RIDGE. At a cabin halfWidth of 0.91 that is a
#                                40 cm wide roof. A Gen-4 roof is ~1.25 m, so
#                                0.72 gives 1.31 m and the cabin reads as a
#                                boxy greenhouse instead of a tapered blob.
#
# The beltline crease comes from the slope BREAK at k4, not from a duplicated
# point: k3->k4 runs nearly vertical (0.995 -> 0.97) and k4->k5 leans hard
# inboard (0.97 -> 0.86), so the smooth-normal calculation puts a real edge
# there. The roof stays genuinely flat because k6 and k7 are both at hf 1.00
# and differ only in sign.
# R1: (heightFrac, widthFrac, liveryV) for the +z half; the -z half is
# mirrored (wf -> -wf, v -> 1-v). 11 per side, NK = 22.
#
# TWO structural changes, both forced by real defects:
#
# 1. V IS NOW EXPLICIT PER POINT, not `0.97 - k/(NK-1)*0.94`. Deriving V from
#    the ring INDEX silently couples every livery band to the point count, and
#    it was already wrong: livery.cpp paints the roof number panel across
#    v [0.420, 0.580] -- 0.160 wide -- while the old ring's roof plateau
#    (k=6..7) spanned only v [0.464, 0.536], 0.072. The roof number has been
#    bleeding over the drip rail and down the tumblehome for the whole life of
#    this mesh. Explicit V lets the roof plateau land exactly on the panel.
#
# 2. CREASES ARE DUPLICATED POINT PAIRS, not single slope breaks.
#    ring_normals() takes a CENTRAL difference around the ring
#    (`kp = r[k+1]`, `km = r[k-1]`), so a lone crease point has its normal
#    averaged across the crease and shades perfectly smooth. A pair a few
#    millimetres apart gives each side its own tangent and the edge survives.
#    This is why the previous shape read as a bar of soap no matter how the
#    widths were tuned.
#
# Widths: the door is dead vertical (wf 0.995 -> 1.000 -> 1.000) from hf 0.30
# to the beltline, so the car's widest point is AT the belt -- what a Cup car's
# slab doors actually do. The roof plateau at wf 0.640 gives 1.19 m / 46.9 in
# against a real Gen-4's 47 in; the old 0.22 gave 0.18 m.
_RING_HALF = [
    (0.000, 0.640, 0.985),   #  0 underbody / floor edge
    (0.022, 0.850, 0.955),   #  1 rocker bottom face   (inside livery's black rocker band)
    (0.055, 0.930, 0.912),   #  2 rocker lip  A   ] crease pair
    (0.075, 0.955, 0.888),   #  3 rocker lip  B   ]
    (0.300, 0.995, 0.815),   #  4 lower door
    (0.580, 1.000, 0.752),   #  5 door mid            (door number band)
    (0.793, 1.000, 0.690),   #  6 BELTLINE A      ] crease pair, straddles SHOULDER
    (0.807, 0.9975, 0.677),  #  7 BELTLINE B      ]   (livery's beltline seam, v=0.677)
    (0.890, 0.900, 0.630),   #  8 side glass / tumblehome
    (0.980, 0.720, 0.596),   #  9 drip rail   A   ] crease pair
    (1.000, 0.640, 0.590),   # 10 drip rail   B   ]   (roof plateau edge)
]
RINGF = [(hf, wf) for (hf, wf, _v) in _RING_HALF] + \
        [(hf, -wf) for (hf, wf, _v) in reversed(_RING_HALF)]
RINGV = [v for (_h, _w, v) in _RING_HALF] + \
        [1.0 - v for (_h, _w, v) in reversed(_RING_HALF)]
NK = len(RINGF)

# heightFrac at/below this rides the beltline curve (yLow -> beltY); above
# it rides the roof rise (beltY -> roofY). 0.80 is RINGF's k=4, right where
# the side-glass band starts, so the glass sits exactly where the roof rise
# begins (index.html:2429).
SHOULDER = 0.80

# Wheel-arch relief, now applied the way JS actually does it. The old
# 4-corner version could only pull in `halfWidthBottom`, because that was
# the only knob a trapezoid had. On a real ring the fix is per-ring-point
# and JS already worked out which points need it: only k=0,1,2 (hf<=0.28)
# reach into the wheel's cylindrical volume; k=3 (hf=0.56) and above are
# already clear at every axle-adjacent station. Correcting a taller band is
# what used to show up as a visible dent (index.html:2440-2446, and this
# port's own CAR-L/CAR-M/CAR-N history).
_WHEEL_AXLE_X = [WHEELBASE / 2.0, -WHEELBASE / 2.0]

# R2: a REAL wheel arch, replacing an inward pinch that could never be one.
#
# The old relief pulled |z| toward 0.62 for ring points below hf 0.30, within
# 0.45 m of an axle. That cannot produce an arch, because it only ever moves
# the section INBOARD at constant height -- the body's lower edge stays down at
# yLow, so from the side there is no opening at all.
#
# Worse, it did not even cover the tire. At the front-axle station the tire's
# crown (y = WHEEL_RADIUS*2 = 0.70) sits at hf 0.68, while the relief stopped
# at hf 0.30. So the bodywork enclosed the tire from 30% to 68% of section
# height and only the bottom of it poked out below. The wheels were never
# small; they were BURIED, which is exactly how they read on the turntable.
#
# The arch is now a circular opening about the axle: the four lowest ring
# points are lifted onto a lip curve and pulled inboard to form a wheelhouse,
# and everything above the lip is re-anchored so the section cannot fold.
ARCH_R = 0.45          # opening radius about the axle centre
ARCH_CY = WHEEL_RADIUS # opening centre sits at axle height, 0.35
ARCH_INNER_Z = 0.58    # wheelhouse wall; the tire's inner face is at 0.62
# R2c: 0.58 -> 0.52. A 1.16 m mouth around a 0.70 m tire is not an arch, it
# is a slot; the opening now runs 1.04 m. ARCH_R cannot come down with it --
# it is pinned at >= 0.43 by the suspension travel below -- so the mouth is
# tightened lengthwise only, which is the axis the travel budget does not
# constrain.
ARCH_X_MAX = 0.52      # half-length of the opening along the body
K_LIP = 3              # ring index of the lip's outer edge (mirror: NK-1-K_LIP)

# ARCH_R must clear a fully compressed wheel: renderer.cpp lifts the wheel
# joint by up to kMaxTravel under load, and nothing previously knew that.
SUSP_MAX_TRAVEL = 0.08
assert ARCH_R >= WHEEL_RADIUS + SUSP_MAX_TRAVEL, "arch would clip a compressed wheel"
assert ARCH_INNER_Z <= TRACK_HALF - 0.14 - 0.02, "wheelhouse wall would touch the tire"

def _arch_lip_y(x, axle_x, y_base):
    """Height of the arch lip at station x, for one axle."""
    dx = abs(x - axle_x)
    if dx >= ARCH_X_MAX:
        return y_base
    if dx <= ARCH_R:
        return ARCH_CY + math.sqrt(max(0.0, ARCH_R * ARCH_R - dx * dx))
    t = (dx - ARCH_R) / (ARCH_X_MAX - ARCH_R)
    return ARCH_CY + (y_base - ARCH_CY) * (t * t * (3.0 - 2.0 * t))

# The arch needs stations to resolve its curve; the silhouette table only has
# one at each axle. These offsets are inserted either side of both axles and
# interpolated from the hand-authored neighbours.
#
# 0.44 exists because the circle has a VERTICAL tangent at dx == ARCH_R (0.45)
# -- that is the arch opening's leading and trailing edge, a genuine crease on
# a real car, not an artifact. Without a sample just inside it the lip appears
# to fall 0.31 m between two stations 0.135 m apart and the crease smears into
# the fender instead of reading as an edge.
ARCH_SAMPLE_DX = (0.20, 0.34, 0.42, 0.47, 0.52)

# R2c: how far inboard the wheelhouse wall is pulled at a given station.
#
# The first cut applied the pull at FULL strength at every station the arch
# touched -- literally `* 1.0` -- so the body's lower flank was yanked 0.36 m
# inboard along the whole 1.16 m mouth regardless of whether the lip above it
# had risen at all. The turntable showed the result exactly: the tire is black,
# the recess behind it is black, and the wheel disappears into a cave instead
# of standing in an opening. An arch is a hole cut in a fender, not a trench
# down the side of the car.
#
# The wall now eases back out to the body line toward the ends of the mouth.
# It reaches full depth inside ARCH_WALL_FULL, which is keyed to the TIRE and
# not to the lip: every ring point the tire's own radial shadow can reach must
# be fully inboard, or bodywork ends up between the tire's two faces, which is
# the CAR-F/CAR-M defect check_car_rig.py holds at exactly zero.
ARCH_WALL_FULL = WHEEL_RADIUS + 0.04

def _arch_wall_w(x, axle_x):
    dx = abs(x - axle_x)
    if dx <= ARCH_WALL_FULL:
        return 1.0
    if dx >= ARCH_X_MAX:
        return 0.0
    t = 1.0 - (dx - ARCH_WALL_FULL) / (ARCH_X_MAX - ARCH_WALL_FULL)
    return t * t * (3.0 - 2.0 * t)
ARCH_DEDUP_TOL = 0.05

def _station(x_js, w, belt_y, y_low, roof_y):
    # x scaled so the nose/tail stations land exactly on +-HALF_LEN, keeping
    # carU()'s mapping exact at the tips (unchanged from the previous loft).
    return (x_js * (HALF_LEN / 2.51), w, belt_y, y_low, roof_y)

# (x_js, halfWidth, beltY, yLow, roofY) per row, transcribed verbatim from
# index.html's CAR_ST. The previous port kept only columns 0/1/4 and
# derived a top width from a fixed 0.60/0.92 greenhouse ratio; `beltY` and
# `yLow` are restored here because the ring genuinely needs them -- beltY
# is the SHOULDER split point every ring point interpolates around, and
# yLow is the rocker/valance height the ring's bottom points sit at (the
# old loft pinned every bottom corner to a single flat FLOOR_Y).
_CAR_ST_JS = [
    # K3 (car visual fidelity plan, part 3): roofY raised 0.40 -> 0.44 --
    # the K1 fallback this station's own row 0 previously left documented
    # but unapplied. K1's forward apex offset alone read as too subtle a
    # fix on its own; this is the other half of that same diagnosis, not a
    # new one. With beltY==roofY the ring's top six points (k4..k9,
    # RINGF hf 0.80-1.00) all landed at the identical flat y=0.40, which is
    # what fed the fan a hard crease regardless of where the apex sat.
    # Raising roofY gives that same span a genuine 4cm crown (k4/k9 stay at
    # 0.40 exactly -- SHOULDER's own branch point, so the beltline/rocker
    # below is untouched -- k5/k8 land at 0.428, k6/k7 at the new 0.44
    # peak), so the fan now has real curvature to work with along its
    # whole top, not just at the apex. Confirmed still safe by the same
    # reasoning K1 already worked through: car_v() depends only on ring
    # index, never station beltY/roofY; wheel-arch relief never reaches
    # station 0 (1.12 away from the front axle, outside
    # _WHEEL_NOTCH_RANGE=0.45) and never touches roofY at any station
    # regardless. This DOES reshape the hood surface immediately behind
    # the bumper (station 0->1's own quad strip) -- screenshot-checked for
    # a new seam or dent there, not just assumed fine because the
    # isolated-safety checks passed.
    # R1: RE-AUTHORED to Gen-4 Cup proportions. The rows below are no longer a
    # transcription of index.html's CAR_ST.
    #
    # The old table came over verbatim from the JS prototype and described a
    # rounded FASTBACK coupe, which is why four rounds of props and paint on
    # top of it never made the car read as a stock car:
    #
    #   * roofY exceeded beltY only at stations 7-10 and REJOINED it at 11 --
    #     the definition of a fastback. A Gen-4 is a notchback: a long flat
    #     roof, a distinct steeply-raked rear window, then a flat deck.
    #   * the nose tapered to halfWidth 0.50 and came to a point. Gen-4 noses
    #     are blunt and nearly full width at the bumper, over a deep air dam.
    #   * the roof peaked mid-cabin at station 9 rather than running flat.
    #
    # Targets, against a real Gen-4 Cup car: 5.08 m long (HALF_LEN, unchanged),
    # 1.90 m wide (halfWidth 0.95 max, unchanged), roof 1.30 m, ~1.3 m of flat
    # roof, windshield raked ~35 deg from horizontal, rear glass ~31 deg, deck
    # flat at ~1.02 m, air dam 0.09 m off the ground.
    #
    # R2b: THE VERTICAL PROFILE WAS THE "still awkward" DEFECT, and the arch
    # work found it by folding. R1 kept the JS prototype's beltY column almost
    # untouched -- it ran 0.60 at the nose to 1.01 at the rear axle, a 0.41 m
    # wedge, so the car nosed down like a sports coupe. Two measurable
    # consequences:
    #
    #   * At the front axle beltY was 0.80 and the tire crown sits at 0.70:
    #     50 mm of fender above a 700 mm tire. There was no room for an arch,
    #     which is why the R2 arch lip (peak ARCH_CY + ARCH_R = 0.80) landed
    #     exactly ON the beltline and collapsed the whole flank from the rocker
    #     to the shoulder into one horizontal shelf -- a 154 deg normal fold.
    #   * The hood plane at the nose was 0.72 m off the ground against a 1.30 m
    #     roof. A Gen-4's nose is ~0.86 and its hood is close to level.
    #
    # So beltY is now a near-level line at cowl/cabin/deck height (0.98-1.02)
    # with the hood stepping down ahead of it, which is what the real car does,
    # and the arch lip clears the tire by 100 mm with 140 mm of fender above it.
    (2.51,  0.84,  0.78,  0.09,  0.86),   # bumper/valance -- BLUNT and wide, deep air dam
    (2.30,  0.90,  0.83,  0.09,  0.90),   # front fascia
    (2.00,  0.93,  0.88,  0.10,  0.93),   # hood leading edge
    (1.70,  0.95,  0.92,  0.11,  0.96),   # front fender -- full width
    (1.40,  0.95,  0.94,  0.13,  0.98),   # front axle -- 0.14 of fender over the arch lip
    (1.10,  0.94,  0.96,  0.15,  1.00),   # hood mid
    (0.80,  0.93,  0.98,  0.16,  1.02),   # cowl / windshield base
    (0.55,  0.92,  0.99,  0.17,  1.16),   # windshield lower
    (0.35,  0.91,  1.00,  0.18,  1.30),   # A-pillar top -- ROOF STARTS
    (-0.20, 0.91,  1.00,  0.19,  1.30),   # roof, flat
    (-0.95, 0.92,  1.01,  0.19,  1.30),   # C-pillar top -- ROOF ENDS
    (-1.15, 0.94,  1.01,  0.20,  1.16),   # rear glass, mid
    (-1.40, 0.95,  1.02,  0.20,  1.04),   # rear axle -- deck starts, belt/roof rejoin
    (-1.85, 0.94,  1.01,  0.20,  1.02),   # deck, flat
    (-2.25, 0.90,  1.00,  0.24,  1.01),   # deck rear
    (-2.51, 0.84,  0.98,  0.34,  0.99),   # tail panel -- wide and square, not a point
]
_KEY_STATIONS = [_station(*row) for row in _CAR_ST_JS]

def _lerp_station(x):
    """Interpolate w/beltY/yLow/roofY at an arbitrary x from the key table."""
    xs = [st[0] for st in _KEY_STATIONS]
    if x >= xs[0]:
        return _KEY_STATIONS[0]
    if x <= xs[-1]:
        return _KEY_STATIONS[-1]
    for i in range(len(xs) - 1):
        if xs[i] >= x >= xs[i + 1]:
            a, b = _KEY_STATIONS[i], _KEY_STATIONS[i + 1]
            t = (a[0] - x) / (a[0] - b[0])
            return tuple([x] + [a[j] + (b[j] - a[j]) * t for j in range(1, 5)])
    return _KEY_STATIONS[-1]

# R2: the silhouette table has one station at each axle, which cannot resolve
# a 1.16 m arch opening. Insert samples either side of both axles, dropping any
# that land on top of a hand-authored station.
def _build_stations():
    xs = [st[0] for st in _KEY_STATIONS]
    extra = []
    for wx in _WHEEL_AXLE_X:
        for d in ARCH_SAMPLE_DX:
            for x in (wx + d, wx - d):
                if all(abs(x - e) > ARCH_DEDUP_TOL for e in xs + extra):
                    extra.append(x)
    allx = sorted(xs + extra, reverse=True)
    return [_lerp_station(x) if x in extra else _KEY_STATIONS[xs.index(x)] for x in allx]

CHASSIS_STATIONS = _build_stations()

def ring_pts(station):
    """Direct port of index.html's ringPts() (:2447)."""
    x, w, belt_y, y_low, roof_y = station
    out = []
    for (hf, wf) in RINGF:
        if hf <= SHOULDER:
            py = y_low + (hf / SHOULDER) * (belt_y - y_low)
        else:
            py = belt_y + ((hf - SHOULDER) / (1.0 - SHOULDER)) * (roof_y - belt_y)
        pz = wf * w
        out.append([x, py, pz])

    # R2: carve the arch. Done as a second pass over the finished ring so the
    # lip height and the re-anchoring above it can both see the un-arched
    # section they are derived from.
    lip_hf = RINGF[K_LIP][0]
    y_base_lip = y_low + (lip_hf / SHOULDER) * (belt_y - y_low)
    for wx in _WHEEL_AXLE_X:
        lip_y = _arch_lip_y(x, wx, y_base_lip)
        if lip_y <= y_base_lip + 1e-9:
            continue                      # this station is clear of the arch
        wall_w = _arch_wall_w(x, wx)
        for k, (hf, wf) in enumerate(RINGF):
            km = min(k, NK - 1 - k)       # mirrored index: 0..K_LIP are the low points
            sgn = 1.0 if wf > 0 else -1.0
            if km <= K_LIP:
                # Lift the low points onto the lip, and pull the inner ones in
                # to form the wheelhouse wall. k=K_LIP stays at full width --
                # it IS the lip's outer edge, i.e. the fender's bottom lip.
                frac = km / float(K_LIP)          # 0 at the floor, 1 at the lip
                out[k][1] = max(out[k][1], y_low + frac * (lip_y - y_low))
                if km < K_LIP:
                    inner = sgn * ARCH_INNER_Z
                    out[k][2] = out[k][2] + (inner - out[k][2]) * wall_w
            else:
                # Re-anchor the fender skin above the lip. Without this the
                # ring folds: at the axle the lip reaches 0.80 while the next
                # point up would still sit at its un-arched height.
                span = SHOULDER - lip_hf
                if span > 1e-9 and hf < SHOULDER:
                    t = (hf - lip_hf) / span
                    out[k][1] = max(out[k][1], lip_y + t * (belt_y - lip_y))
    return [tuple(p) for p in out]

RINGS = [ring_pts(st) for st in CHASSIS_STATIONS]

def ring_normals():
    """Smooth per-vertex normals from surface tangents -- port of the NRM
    block in index.html's buildCarMesh().

    The normal at ring point (i,k) is the cross product of the tangent
    around the ring and the tangent along the body's length, so it varies
    continuously across the whole surface. This is what makes the body
    shade as one curved shell rather than as a stack of separate plates,
    and it is what finally gives fs_car.sc's specular/Fresnel terms
    something to sweep across.
    """
    out = []
    for i, r in enumerate(RINGS):
        row = []
        for k, p in enumerate(r):
            rp = RINGS[min(len(RINGS) - 1, i + 1)][k]
            rm = RINGS[max(0, i - 1)][k]
            # H1: the ring tangent WRAPS (modulo NK) rather than clamping at
            # the ends. RINGF is an open arc from one rocker to the other,
            # but the section is genuinely closed through the flat underbody,
            # so wrapping gives the two rocker points a real two-sided
            # tangent. Clamping instead left them with a one-sided estimate
            # and a 93.7-degree jump to their neighbour -- a hard shading
            # seam right along the rocker, which the decode check caught.
            kp = r[(k + 1) % NK]
            km = r[(k - 1) % NK]
            t_len = _sub(rp, rm)   # along the body's length
            t_ring = _sub(kp, km)  # around the cross-section
            n = _norm(_cross(t_ring, t_len))
            # Flip inward-facing normals: measure against the outward
            # direction from the section's own mid-height axis.
            mid = (CHASSIS_STATIONS[i][2] + CHASSIS_STATIONS[i][3]) / 2.0
            if n[1] * (p[1] - mid) + n[2] * p[2] < 0:
                n = (-n[0], -n[1], -n[2])
            row.append(n)
        out.append(row)
    return out

RING_NRM = ring_normals()

def car_u(x):
    return 0.02 + (HALF_LEN - x) / (2.0 * HALF_LEN) * 0.76

def car_v(k):
    """Ring index -> livery V.

    R1: an explicit table lookup, not `0.97 - k/(NK-1)*0.94`.

    Deriving V from the ring INDEX silently couples every band livery.cpp
    paints to the ring's point count, and it was already wrong: the roof
    number panel is painted across v [0.420, 0.580], 0.160 wide, while the old
    14-point ring's roof plateau spanned only v [0.464, 0.536], 0.072 wide. The
    roof number has been overflowing onto the drip rail and the tumblehome for
    the whole life of this mesh. RINGV lets the roof plateau be authored to
    land exactly on the painted panel, and lets the ring gain crease pairs
    without sliding every other band.

    This port's livery is mirrored relative to JS's (livery.cpp paints the +z
    flank at HIGH v), which is why RINGV runs 0.985 at the +z rocker down to
    0.015 at the -z rocker rather than the other way round.
    """
    return RINGV[k]

def _rv(i, k):
    """(position, normal, uv) for ring point k of station i."""
    return (RINGS[i][k], RING_NRM[i][k], (car_u(CHASSIS_STATIONS[i][0]), car_v(k)))

for i in range(len(RINGS) - 1):
    for k in range(NK - 1):
        # Outward hint from the ring point's own smooth normal -- already
        # corrected to face outward by ring_normals().
        emit_smooth_quad(_rv(i, k), _rv(i + 1, k), _rv(i + 1, k + 1), _rv(i, k + 1),
                          RING_NRM[i][k])
    # Underbody: RINGF is an open arc (bottom-right .. bottom-left), so the
    # floor closes it. Never visible in normal play, but leaving a hole
    # would show the body's inside from a low angle -- this renderer does no
    # backface culling (renderer.cpp's own convention).
    emit_smooth_quad(
        (RINGS[i][NK - 1], (0, -1, 0), (car_u(CHASSIS_STATIONS[i][0]), 0.01)),
        (RINGS[i + 1][NK - 1], (0, -1, 0), (car_u(CHASSIS_STATIONS[i + 1][0]), 0.01)),
        (RINGS[i + 1][0], (0, -1, 0), (car_u(CHASSIS_STATIONS[i + 1][0]), 0.99)),
        (RINGS[i][0], (0, -1, 0), (car_u(CHASSIS_STATIONS[i][0]), 0.99)),
        (0, -1, 0))

# Nose/tail caps: a fan to a single apex at the section's own mid-height,
# which is what a ring makes possible and a 4-corner quad never could.
#
# The cap's UV is PLANAR (v from the point's own lateral z), not the ring's
# car_v(k). Using the ring index here instead put v=0.5 at the apex and swept
# 0.03..0.97 around the rim, so every livery band G16 painted across the tail
# -- the dark centre panel at v 0.462-0.538 and the red lamp bands either
# side -- came out smeared into radial wedges around a bullseye. Mapping v
# from z instead lays those bands out as vertical stripes across the tail,
# which is how G16 drew them and what the old flat cap quad produced.
def _cap_v(z, wmax):
    return min(0.97, max(0.03, 0.5 + (z / wmax) * 0.47))

# K1 (car visual fidelity plan, part 3): the nose apex's forward offset.
# Every cap vertex used to sit in the station's own YZ plane -- the apex's
# X was literally `st[0]`, identical to every ring point's own X -- so the
# "cap" was a flat 2D disc, not a convex bumper fascia. Confirmed by a
# close-up screenshot showing a hard diagonal crease through the nose/
# grille area, worst here specifically because station 0's beltY==roofY
# (0.40, identical) produces a flat 6-point plateau across the ring's own
# top (RINGF hf 0.80-1.00, k=4..9) that fed straight into the fan.
#
# 0.06m (~2.4in): ~12% of station 0's own 0.50m half-width, ~17% of
# WHEEL_RADIUS -- a subtle, clearly non-planar bulge, not an exaggerated
# pointed nose. Clearance checked against J4's front splitter (its own
# forward tip sits at HALF_LEN+0.18=2.72; this apex lands at HALF_LEN+0.06
# =2.60, a full 0.12m clear).
#
# Tail deliberately NOT given this treatment -- out of scope. The user's
# report was specifically about the front nose, and the tail already has
# its own different fix for its own different symptom (yLow raised at the
# tail's own station row "so the cap fan's apex doesn't read as an
# inverted-V boat hull from behind"). check_car_rig.py's own new checks
# assert the tail cap stays flat, so this boundary is enforced, not just
# stated in a comment.
NOSE_APEX_DX = 0.06

# K1: exposed so check_car_rig.py can isolate exactly the cap vertices to
# check -- caps sample ordinary body-livery UV (no distinguishing swatch
# the way props like the spoiler/exhaust do), so a vertex-index range is
# the only reliable way to find them from outside this file.
NOSE_CAP_RANGE = None
TAIL_CAP_RANGE = None

for (idx, outward) in ((0, (1, 0, 0)), (len(RINGS) - 1, (-1, 0, 0))):
    st = CHASSIS_STATIONS[idx]
    ring = RINGS[idx]
    wmax = max(abs(p[2]) for p in ring) or 1.0
    u = car_u(st[0])
    apex_y = (st[2] + st[3]) / 2.0
    apex_x = st[0] + NOSE_APEX_DX * outward[0] if idx == 0 else st[0]
    apex_pos = (apex_x, apex_y, 0.0)
    _cap_start = len(positions)

    def cap_tri(p1, p2):
        # Flat per-facet normal computed from this triangle's own three
        # positions, replacing the old hardcoded constant `outward` every
        # cap vertex used to share regardless of the triangle it belonged
        # to. That constant was only ever correct because the cap was
        # perfectly flat; now that the nose apex sits off the station's own
        # plane, a stale flat normal would make the geometry SHADE as flat
        # even though it no longer IS flat, defeating the point of this fix.
        n = _norm(_cross(_sub(p1, apex_pos), _sub(p2, apex_pos)))
        if _dot(n, outward) < 0:
            n = (-n[0], -n[1], -n[2])
        apex_v = (apex_pos, n, (u, _cap_v(0.0, wmax)))
        return apex_v, (p1, n, (u, _cap_v(p1[2], wmax))), (p2, n, (u, _cap_v(p2[2], wmax)))

    for k in range(NK - 1):
        a, b, c = cap_tri(ring[k], ring[k + 1])
        emit_smooth_tri(a, b, c, outward)
    # close the fan across the underbody edge
    a, b, c = cap_tri(ring[NK - 1], ring[0])
    emit_smooth_tri(a, b, c, outward)

    if idx == 0:
        NOSE_CAP_RANGE = (_cap_start, len(positions))
    else:
        TAIL_CAP_RANGE = (_cap_start, len(positions))

# G8 (Gen-4 car overhaul): spoiler -- a flat angled blade plus two small
# corner risers down to the deck, ported from index.html's own spoiler
# (buildCarMesh(), "60 in angled blade"). The body loft is a faceted tube,
# not a flat decklid, so a full-width blade at a fixed height would float
# clear of the quarter panels; narrowing it to the tail station's own
# half-width and anchoring both ends with a riser block (real Gen-4
# spoilers sit on short riser blocks) makes the mount read as intentional
# rather than a detached plate. Bound to joint 0 like the rest of the
# chassis -- a spoiler doesn't move independently, no new joint needed.
# H1: pinned to the tail station's own half-width, which is what G8's
# comment below says the blade is sized against anyway. It used to be
# TRACK_HALF*0.6, which accidentally coupled the spoiler's width to the
# wheel track -- so moving the wheels inboard would have silently narrowed
# the spoiler too.
# R2d: 0.6 -> 0.80 of the tail half-width. At 0.6 the blade was 1.01 m wide
# on a 1.90 m car and stood 0.24 m proud of the deck -- narrow and tall is
# exactly the "detached plate" the G8 comment above says the riser blocks
# exist to avoid, and the turntable shows it reading that way from every
# rear angle. A Gen-4 spoiler is ~1.45 m wide and ~0.17 m tall. 0.80 puts
# the blade's tips at 0.672, between the tail section's own top-plateau edge
# (0.538) and its widest upper point (0.756), so the ends land on the
# tumblehome instead of floating clear of it -- which is the constraint that
# put the original number low, just solved with the right value rather than
# by shrinking the blade until it could not overhang anything.
_spz = CHASSIS_STATIONS[-1][1] * 0.80
_sp_x0, _sp_x1 = -(HALF_LEN - 0.12), -(HALF_LEN + 0.06)
_sp_deckY = CHASSIS_STATIONS[-1][4]  # tail station's own yTop
_sp_y0, _sp_y1, _sp_th = _sp_deckY + 0.02, _sp_deckY + 0.18, 0.03

def _p(x, y, z):
    return (x, y, z)

# Top blade face (this car's own body color).
emit_swatch_quad(_p(_sp_x0, _sp_y0, -_spz), _p(_sp_x0, _sp_y0, _spz),
                  _p(_sp_x1, _sp_y1, _spz), _p(_sp_x1, _sp_y1, -_spz),
                  (0, 1, 0), SW_SPOILER_BODY)
# Underside + two edge faces (dark).
emit_swatch_quad(_p(_sp_x0, _sp_y0 - _sp_th, _spz), _p(_sp_x0, _sp_y0 - _sp_th, -_spz),
                  _p(_sp_x1, _sp_y1 - _sp_th, -_spz), _p(_sp_x1, _sp_y1 - _sp_th, _spz),
                  (0, -1, 0), SW_SPOILER_DARK)
emit_swatch_quad(_p(_sp_x0, _sp_y0, -_spz), _p(_sp_x1, _sp_y1, -_spz),
                  _p(_sp_x1, _sp_y1 - _sp_th, -_spz), _p(_sp_x0, _sp_y0 - _sp_th, -_spz),
                  (0, 0, -1), SW_SPOILER_DARK)
emit_swatch_quad(_p(_sp_x1, _sp_y1, _spz), _p(_sp_x0, _sp_y0, _spz),
                  _p(_sp_x0, _sp_y0 - _sp_th, _spz), _p(_sp_x1, _sp_y1 - _sp_th, _spz),
                  (0, 0, 1), SW_SPOILER_DARK)
# Corner risers down to the deck, one per side.
for _rz in (_spz, -_spz):
    emit_swatch_quad(_p(_sp_x0 - 0.05, _sp_deckY, _rz - 0.05), _p(_sp_x0 - 0.05, _sp_deckY, _rz + 0.05),
                      _p(_sp_x0, _sp_y0, _rz + 0.05), _p(_sp_x0, _sp_y0, _rz - 0.05),
                      (0, 0, 1 if _rz > 0 else -1), SW_SPOILER_DARK)
    emit_swatch_quad(_p(_sp_x0 - 0.05, _sp_deckY, _rz + 0.05), _p(_sp_x0 - 0.05, _sp_deckY, _rz - 0.05),
                      _p(_sp_x0 - 0.05, _sp_y0, _rz - 0.05), _p(_sp_x0 - 0.05, _sp_y0, _rz + 0.05),
                      (-1, 0, 0), SW_SPOILER_DARK)

# J3 (car visual fidelity plan, part 2): spoiler endplates. The blade's own
# "edge faces" above (the two emit_swatch_quad calls right after the top
# blade face) only trace the blade's 0.03 thickness -- not a fin -- and the
# corner risers just above only run from the deck up to the blade's own
# FRONT corner (x0,y0). Nothing fills the profile from there back to the
# blade's REAR corner (x1,y1) down to the deck -- an open triangular gap a
# real Gen-4 spoiler doesn't have (it reads as a filled endplate at each
# corner, not a floating blade). One quad per side, reusing this block's
# own already-computed constants (no new anchor derivation), SW_SPOILER_DARK
# to match every other non-top-blade surface -- SW_SPOILER_BODY stays
# reserved for the single most prominent top face.
for _epz in (_spz, -_spz):
    emit_swatch_quad(_p(_sp_x0 - 0.05, _sp_deckY, _epz), _p(_sp_x1, _sp_deckY, _epz),
                      _p(_sp_x1, _sp_y1, _epz), _p(_sp_x0, _sp_y0, _epz),
                      (0, 0, 1 if _epz > 0 else -1), SW_SPOILER_DARK)

# I2 (car visual fidelity plan): door mirrors. No mirror geometry existed
# anywhere in this rig before -- every reference NASCAR Thunder image shows
# a clearly visible, chunky housing on each door. Mounted at station 6
# ("cowl / windshield base") ring points k=4 (+z/right) and k=9 (-z/left) --
# exactly SHOULDER's own split point (hf=0.80), i.e. right where the
# side-glass band starts, matching where a real mirror sits relative to the
# A-pillar. A small axis-aligned box offset outward along that ring point's
# own smooth normal (RING_NRM), so it clears the body surface without
# needing to match the surface's local orientation -- same "cheap flat prop
# is enough at chase-cam distance" idiom the spoiler above already uses.
_MIRROR_STATION = 6
for _mk in (4, 9):
    _mp = RINGS[_MIRROR_STATION][_mk]
    _mn = RING_NRM[_MIRROR_STATION][_mk]
    # I2 fix: 0.08 left a visible gap between the body surface and the
    # housing (it read as a small dark blob floating detached against the
    # sky rather than mounted on the door, confirmed by screenshot). The
    # housing's own half-extent along the normal is only ~0.035, so a
    # smaller offset that overlaps the body slightly (housing extends back
    # past the surface, into the door) keeps it visually flush -- the
    # overlap itself is invisible since it's inside the opaque body.
    _MIRROR_OUT = 0.025  # distance from the body surface to the housing's centre
    _mcx = _mp[0] + _mn[0] * _MIRROR_OUT
    _mcy = _mp[1] + _mn[1] * _MIRROR_OUT
    _mcz = _mp[2] + _mn[2] * _MIRROR_OUT
    for (_fn, _corners) in FACES:
        _face_base = len(positions)
        for (_sx, _sy, _sz) in _corners:
            positions.append((_mcx + _sx * 0.045, _mcy + _sy * 0.028, _mcz + _sz * 0.035))
            normals.append(_fn)
            uvs.append(SW_MIRROR)
            joints0.append((0, 0, 0, 0))
            weights0.append((1.0, 0.0, 0.0, 0.0))
        indices.extend([_face_base, _face_base + 1, _face_base + 2,
                         _face_base, _face_base + 2, _face_base + 3])

# J2 (car visual fidelity plan, part 2): dual exhaust pipes. No exhaust
# geometry existed anywhere in this rig -- real Gen-4/Cup cars run a
# visible side-exit exhaust along the rocker ahead of the rear wheel, and
# every reference NASCAR Thunder image shows one. This is a direct port of
# JS's own exhaustPipe() (index.html:2591-2606): an open half-cylinder
# segment strip, structurally the same idea as add_wheel()'s tread band
# above, just an open arc (not a closed ring) and short. x-span scaled by
# the same HALF_LEN/2.51 factor _station() already applies to every
# station x; y0/r kept at JS's own values (0.26/0.055) -- close enough to
# this x-range's real yLow (stations 9-11: 0.195-0.205) that no rescale is
# warranted, the same call the plan for this phase made for every other
# hand-placed prop's height (spoiler, mirrors).
#
# z convention: JS's z>0 is the car's right side, but THIS port's z axis is
# mirrored relative to JS's (car_v()'s own comment above says so plainly,
# and it's independently visible in two already-shipped facts: livery.cpp's
# door-number labels put "right door" at the p[2]<0 UV family, and its
# existing "exhaust soot smudge, right side only" texture cue already sits
# in that same z<0 band). A global z-mirror flips both the z0 anchor's sign
# and the local radial term's sign (the arc's own y/sin term is untouched --
# only z/cos flips), so this is not a literal transcription of JS's
# per-vertex formula, and the two pipes are called with negative z0.
#
# z0 magnitudes: NOT JS's literal 0.74/0.90. Those were tuned against JS's
# own wz=0.80 wheel-z constant; this rig's analogous constant is
# TRACK_HALF=0.76, so JS's values are scaled by 0.76/0.80=0.95 instead of
# copied -- 0.74*0.95~=0.70 (the inner pipe, landing right at this x-range's
# own rocker width of ~0.63-0.72, computed from CHASSIS_STATIONS'
# halfWidth*RINGF[0]'s wf=0.76 across stations 9-11) and 0.90*0.95~=0.86
# (the outer pipe, visibly proud of the rocker for a real dual-pipe read).
# Still a hand-placed prop, not a ring-interpolated one -- same idiom the
# spoiler/mirror sections already use -- so this is a reasoned starting
# point for the screenshot pass, not assumed final.
#
# check_car_rig.py's own rear-tire-clearance check caught a real intrusion
# at JS's literal x1=-1.15 scaled (~-1.164): the rear wheel's radial shadow
# in X starts at axle_x+WHEEL_RADIUS = -1.395+0.35 = -1.045 (any point
# within WHEEL_RADIUS of the axle in the X-Y side-view plane is "in front
# of" the tire, not just points sharing its exact X), so a pipe reaching
# past that in X can land between the tire's inner and outer face -- poking
# through the sidewall -- regardless of how carefully z is chosen. -1.02
# clears that boundary with a small margin instead of tuning z to dodge it,
# since JS's own x1 wasn't derived against this rig's WHEELBASE/WHEEL_RADIUS
# in the first place.
_EXH_X0, _EXH_X1 = -0.55 * (HALF_LEN / 2.51), -1.02
_EXH_Y0, _EXH_R, _EXH_SEGS = 0.26, 0.055, 6

def add_exhaust_pipe(z0):
    for k in range(_EXH_SEGS):
        a0 = -math.pi / 2 + k / _EXH_SEGS * math.pi
        a1 = -math.pi / 2 + (k + 1) / _EXH_SEGS * math.pi
        s0, c0 = math.sin(a0), math.cos(a0)
        s1, c1 = math.sin(a1), math.cos(a1)
        p0a = (_EXH_X0, _EXH_Y0 + s0 * _EXH_R, z0 - c0 * _EXH_R)
        p1a = (_EXH_X0, _EXH_Y0 + s1 * _EXH_R, z0 - c1 * _EXH_R)
        p0b = (_EXH_X1, _EXH_Y0 + s0 * _EXH_R, z0 - c0 * _EXH_R)
        p1b = (_EXH_X1, _EXH_Y0 + s1 * _EXH_R, z0 - c1 * _EXH_R)
        mid = (a0 + a1) / 2.0
        # Outward radial normal, hand-set like add_wheel()'s own end-cap
        # normals -- this renderer applies no backface culling anywhere, so
        # triangle winding only needs to describe valid triangles, not a
        # specific facing (add_wheel()'s own comment on this point applies
        # unchanged here).
        nm = (0.0, math.sin(mid), -math.cos(mid))
        base = len(positions)
        for p in (p0a, p0b, p1b, p0a, p1b, p1a):
            positions.append(p)
            normals.append(nm)
            uvs.append(SW_SIDEWALL)
            joints0.append((0, 0, 0, 0))
            weights0.append((1.0, 0.0, 0.0, 0.0))
        indices.extend([base, base + 1, base + 2, base + 3, base + 4, base + 5])

for _ez0 in (-0.74 * (TRACK_HALF / 0.80), -0.90 * (TRACK_HALF / 0.80)):
    add_exhaust_pipe(_ez0)

# J4 (car visual fidelity plan, part 2): front splitter. New design -- no JS
# precedent (grep confirms zero "splitter" hits in index.html) -- but every
# reference photo the user compared against shows a pronounced one, and the
# car has none. Same idiom as the spoiler above: a flat swatch-quad blade
# anchored to a station's own geometry rather than a free-floating prop.
#
# Anchored to CHASSIS_STATIONS[0] (the bumper cap / nose tip): x=HALF_LEN
# exactly, halfWidth=0.50, yLow=0.13 -- the car's own lowest, frontmost
# chassis point, which is the correct real-world splitter attachment line.
_SPL_ST0 = CHASSIS_STATIONS[0]
_spl_zhalf = _SPL_ST0[1] * 0.90  # narrower than the full nose width, clear of the fender corners
# Ground-clipping is the single biggest risk in this phase (flagged in the
# plan before any code was written): yLow=0.13 is already the car's lowest
# point, so the splitter is dropped a further 0.03 below it (a visible
# protruding dip, the actual point of a splitter) while keeping a real
# 0.10 clearance above y=0 -- not just nominal, since this rig has no
# dynamic ride-height/pitch at the chassis level, only per-wheel suspension
# travel, so that clearance is a real, static number, not a worst-case one.
_spl_y = _SPL_ST0[3] - 0.03
_spl_th = 0.02
# x_rear sits slightly BEHIND the nose tip (embeds into the opaque body, the
# same "overlap rather than gap" idiom I2's mirror history established) so
# the attachment seam is hidden; x_front is the real forward protrusion.
_spl_x_rear, _spl_x_front = HALF_LEN - 0.05, HALF_LEN + 0.18

# Top face.
emit_swatch_quad(_p(_spl_x_rear, _spl_y, -_spl_zhalf), _p(_spl_x_rear, _spl_y, _spl_zhalf),
                  _p(_spl_x_front, _spl_y, _spl_zhalf), _p(_spl_x_front, _spl_y, -_spl_zhalf),
                  (0, 1, 0), SW_SPOILER_DARK)
# Underside.
emit_swatch_quad(_p(_spl_x_rear, _spl_y - _spl_th, _spl_zhalf), _p(_spl_x_rear, _spl_y - _spl_th, -_spl_zhalf),
                  _p(_spl_x_front, _spl_y - _spl_th, -_spl_zhalf), _p(_spl_x_front, _spl_y - _spl_th, _spl_zhalf),
                  (0, -1, 0), SW_SPOILER_DARK)
# Front edge cap -- without it the leading edge would read as an
# infinitely thin plane when seen head-on, the angle the chase/showcase
# cameras actually view an oncoming car's nose from.
emit_swatch_quad(_p(_spl_x_front, _spl_y - _spl_th, -_spl_zhalf), _p(_spl_x_front, _spl_y, -_spl_zhalf),
                  _p(_spl_x_front, _spl_y, _spl_zhalf), _p(_spl_x_front, _spl_y - _spl_th, _spl_zhalf),
                  (1, 0, 0), SW_SPOILER_DARK)

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
    # G8 (Gen-4 car overhaul): narrower tire width (was 0.6) -- matches
    # JS's own wW/2 ratio relative to its wheel radius more closely.
    # H1: sides 10 -> 16. At 10 the tire silhouette was visibly a decagon at
    # chase-cam distance, which is the same faceting problem the body had;
    # 16 costs ~28 extra tris per wheel and reads round.
    add_wheel(wx, WHEEL_RADIUS, wz, WHEEL_RADIUS, WHEEL_RADIUS * 0.4, joint_idx=i + 1, sides=16)

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
