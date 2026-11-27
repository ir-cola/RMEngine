# Makes docs/samples/models/skinned_arm.gltf: a small model with bones, for bone animation (run from
# the repository root):
#   python docs/samples/models/make_skinned_sample.py
#
#   skinned_arm.gltf   a square column 2 m tall in 3 segments (orange / cream / orange), 3 bones in a
#                      chain (Base -> Middle -> Top, joints at 2/3 m and 4/3 m) under an "Arm" node that
#                      is not a bone, smooth weights near the joints (at most 2 bones per vertex), and
#                      three animations that loop cleanly:
#                        "Wave"  2 s: the column bends from side to side (each bone a little later)
#                        "Idle"  4 s: a slight sway
#                        "Walk"  1 s: the root bone (Base) goes 0.8 m forward (+Z) with a small bob and
#                                     sway - for root motion (docs/ANIMATION.md 7-1. Its position is
#                                     not the same at the start and the end: root motion carries it on)
#                        "Puff"  2 s: blend shape weights only (docs/ANIMATION.md 7-2-2): "Fat" goes 0 -> 1 -> 0
#                      a spring bone setting (VRM 1.0 extension VRMC_springBone, docs/ANIMATION.md 7-2-3): the
#                      chain Middle -> Top sways (gravity 0.5), and a sphere collider sits on Base,
#                      and two blend shapes (morph targets, names in mesh.extras.targetNames):
#                        "Fat"   the top segment swells sideways by 0.1 m (and its side normals don't change)
#                        "Lean"  the whole column leans 0.2 m to +X at the top (more the higher the vertex)
#                      The buffer is written inside the file (data: URI).
#
# Only the Python standard library. Everything here is made by this script (no licence to follow).
import base64, json, math, os, struct

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "skinned_arm.gltf")

HALF = 0.15          # half the width of the column
HEIGHT = 2.0
SEGMENTS = 3
RINGS = 8            # rings of vertices per segment (more rings: a smoother bend)
JOINTS = [HEIGHT * k / SEGMENTS for k in range(SEGMENTS)]  # where each bone starts: 0, 2/3, 4/3
BLEND = 0.25         # the weights change over +- this much around each joint

def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)

def weights_at(y):
    # Bone k gets 1 between its joints; near a joint the weight goes smoothly to the next bone.
    s = [smoothstep(j - BLEND, j + BLEND, y) for j in JOINTS[1:]]
    w = [1 - s[0], s[0] * (1 - s[1]), s[1]]
    pairs = sorted([(k, v) for k, v in enumerate(w) if v > 1e-6], key=lambda p: -p[1])[:4]
    total = sum(v for _, v in pairs)
    pairs = [(k, v / total) for k, v in pairs]
    while len(pairs) < 4:
        pairs.append((0, 0.0))
    return pairs

def sub(a, b): return [a[i] - b[i] for i in range(3)]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def dot(a, b): return sum(a[i] * b[i] for i in range(3))

class Part:
    def __init__(self):
        self.pos, self.nrm, self.idx = [], [], []
    def vertex(self, p, n):
        self.pos.append(p)
        self.nrm.append(n)
        return len(self.pos) - 1
    def quad(self, corners, n):
        # Front faces are counter-clockwise seen from outside (glTF): turn the corners if needed.
        a, b, c, d = corners
        if dot(cross(sub(b, a), sub(c, a)), n) < 0:
            a, b, c, d = d, c, b, a
        i = [self.vertex(p, n) for p in (a, b, c, d)]
        self.idx += [i[0], i[1], i[2], i[0], i[2], i[3]]

SIDES = [[1, 0, 0], [0, 0, 1], [-1, 0, 0], [0, 0, -1]]
parts = []
for seg in range(SEGMENTS):
    part = Part()
    y0, y1 = JOINTS[seg], JOINTS[seg] + HEIGHT / SEGMENTS
    for r in range(RINGS):
        ya = y0 + (y1 - y0) * r / RINGS
        yb = y0 + (y1 - y0) * (r + 1) / RINGS
        for n in SIDES:
            u = cross([0, 1, 0], n)  # along the side
            c = [n[0] * HALF, 0, n[2] * HALF]
            corner = lambda s, y: [c[0] + u[0] * HALF * s, y, c[2] + u[2] * HALF * s]
            part.quad([corner(-1, ya), corner(1, ya), corner(1, yb), corner(-1, yb)], n)
    if seg == 0:
        part.quad([[-HALF, 0, -HALF], [HALF, 0, -HALF], [HALF, 0, HALF], [-HALF, 0, HALF]], [0, -1, 0])
    if seg == SEGMENTS - 1:
        part.quad([[-HALF, HEIGHT, -HALF], [HALF, HEIGHT, -HALF], [HALF, HEIGHT, HALF], [-HALF, HEIGHT, HALF]], [0, 1, 0])
    parts.append(part)

# --- The buffer ---------------------------------------------------------------------------------

buf = bytearray()
views, accessors = [], []

def add(data, target=None):
    while len(buf) % 4:
        buf.append(0)
    view = {"buffer": 0, "byteOffset": len(buf), "byteLength": len(data)}
    if target:
        view["target"] = target
    buf.extend(data)
    views.append(view)
    return len(views) - 1

def accessor(data, component, count, kind, target=None, mn=None, mx=None, normalized=False):
    a = {"bufferView": add(data, target), "componentType": component, "count": count, "type": kind}
    if mn is not None:
        a["min"], a["max"] = mn, mx
    if normalized:
        a["normalized"] = True
    accessors.append(a)
    return len(accessors) - 1

FLOAT, USHORT = 5126, 5123
ARRAY, ELEMENTS = 34962, 34963
primitives = []
for seg, part in enumerate(parts):
    n = len(part.pos)
    pos = accessor(struct.pack("<%df" % (3 * n), *[v for p in part.pos for v in p]), FLOAT, n, "VEC3", ARRAY,
                   [min(p[i] for p in part.pos) for i in range(3)], [max(p[i] for p in part.pos) for i in range(3)])
    nrm = accessor(struct.pack("<%df" % (3 * n), *[float(v) for p in part.nrm for v in p]), FLOAT, n, "VEC3", ARRAY)
    influences = [weights_at(p[1]) for p in part.pos]
    joints = accessor(struct.pack("<%dH" % (4 * n), *[k for w in influences for k, _ in w]), USHORT, n, "VEC4", ARRAY)
    weights = accessor(struct.pack("<%df" % (4 * n), *[v for w in influences for _, v in w]), FLOAT, n, "VEC4", ARRAY)
    idx = accessor(struct.pack("<%dH" % len(part.idx), *part.idx), USHORT, len(part.idx), "SCALAR", ELEMENTS)
    # Blend shapes: differences of position per vertex (no NORMAL: normals stay).
    fat = [[(0.1 if p[0] > 0 else -0.1 if p[0] < 0 else 0.0) if seg == SEGMENTS - 1 else 0.0, 0.0,
            (0.1 if p[2] > 0 else -0.1 if p[2] < 0 else 0.0) if seg == SEGMENTS - 1 else 0.0] for p in part.pos]
    lean = [[0.2 * p[1] / HEIGHT, 0.0, 0.0] for p in part.pos]
    targets = []
    for shape in (fat, lean):
        flat = [v for d in shape for v in d]
        targets.append({"POSITION": accessor(struct.pack("<%df" % (3 * n), *flat), FLOAT, n, "VEC3", ARRAY,
                                             [min(d[i] for d in shape) for i in range(3)], [max(d[i] for d in shape) for i in range(3)])})
    primitives.append({"attributes": {"POSITION": pos, "NORMAL": nrm, "JOINTS_0": joints, "WEIGHTS_0": weights},
                       "indices": idx, "material": seg % 2, "targets": targets})

# Inverse bind matrices (column by column): bone k sits at (0, JOINTS[k], 0) when bound.
ibm = []
for j in JOINTS:
    ibm += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -j, 0, 1]
ibm_accessor = accessor(struct.pack("<%df" % len(ibm), *ibm), FLOAT, SEGMENTS, "MAT4")

def turn_z(angle):
    return [0.0, 0.0, math.sin(angle / 2), math.cos(angle / 2)]

def animation(name, duration, step, angle_of_bone, root_position=None):
    keys = int(round(duration / step))
    times = [duration * k / keys for k in range(keys + 1)]
    t_acc = accessor(struct.pack("<%df" % len(times), *times), FLOAT, len(times), "SCALAR", None, [0.0], [duration])
    samplers, channels = [], []
    for bone in range(SEGMENTS):
        values = [v for t in times for v in turn_z(angle_of_bone(bone, t))]
        out = accessor(struct.pack("<%df" % len(values), *values), FLOAT, len(times), "VEC4")
        samplers.append({"input": t_acc, "output": out, "interpolation": "LINEAR"})
        channels.append({"sampler": bone, "target": {"node": 2 + bone, "path": "rotation"}})
    if root_position:
        # The root bone (Base, node 2) moves: root motion takes this out of the pose.
        values = [v for t in times for v in root_position(t)]
        out = accessor(struct.pack("<%df" % len(values), *values), FLOAT, len(times), "VEC3")
        samplers.append({"input": t_acc, "output": out, "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1, "target": {"node": 2, "path": "translation"}})
    return {"name": name, "samplers": samplers, "channels": channels}

# Both loop: the angles are sines whose period is the length of the animation.
wave = animation("Wave", 2.0, 1 / 12, lambda b, t: [0.25, 0.5, 0.6][b] * math.sin(math.pi * t - 0.6 * b))
idle = animation("Idle", 4.0, 0.25, lambda b, t: [0.04, 0.06, 0.08][b] * math.sin(math.pi * t / 2 - 0.8 * b))
walk = animation("Walk", 1.0, 1 / 30, lambda b, t: [0.05, 0.08, 0.1][b] * math.sin(2 * math.pi * t - 0.7 * b),
                 lambda t: [0.0, 0.03 * (1 - math.cos(4 * math.pi * t)), 0.8 * t])

def morph_animation(name, duration, step, weights_at):
    # A clip of blend shape weights only: the "weights" channel of the mesh node (ArmMesh, node 1).
    keys = int(round(duration / step))
    times = [duration * k / keys for k in range(keys + 1)]
    t_acc = accessor(struct.pack("<%df" % len(times), *times), FLOAT, len(times), "SCALAR", None, [0.0], [duration])
    values = [v for t in times for v in weights_at(t)]
    out = accessor(struct.pack("<%df" % len(values), *values), FLOAT, len(values), "SCALAR")
    return {"name": name, "samplers": [{"input": t_acc, "output": out, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 1, "path": "weights"}}]}

puff = morph_animation("Puff", 2.0, 0.25, lambda t: [0.5 - 0.5 * math.cos(math.pi * t), 0.0])

doc = {
    "asset": {"version": "2.0", "generator": "RM Engine sample (docs/samples/models/make_skinned_sample.py)"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [
        {"name": "Arm", "children": [1, 2]},
        {"name": "ArmMesh", "mesh": 0, "skin": 0},
        {"name": "Base", "children": [3]},
        {"name": "Middle", "translation": [0, JOINTS[1], 0], "children": [4]},
        {"name": "Top", "translation": [0, JOINTS[2] - JOINTS[1], 0]},
    ],
    "skins": [{"name": "ArmSkin", "joints": [2, 3, 4], "skeleton": 2, "inverseBindMatrices": ibm_accessor}],
    "meshes": [{"name": "ArmMesh", "primitives": primitives, "weights": [0.0, 0.0], "extras": {"targetNames": ["Fat", "Lean"]}}],
    "materials": [
        {"name": "Orange", "pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.35, 0.08, 1], "metallicFactor": 0, "roughnessFactor": 0.6}},
        {"name": "Cream", "pbrMetallicRoughness": {"baseColorFactor": [0.85, 0.8, 0.65, 1], "metallicFactor": 0, "roughnessFactor": 0.7}},
    ],
    "animations": [wave, idle, walk, puff],
    "extensionsUsed": ["VRMC_springBone"],
    "extensions": {"VRMC_springBone": {
        "specVersion": "1.0",
        "colliders": [{"node": 2, "shape": {"sphere": {"offset": [0.3, 0.5, 0], "radius": 0.1}}}],
        "colliderGroups": [{"colliders": [0]}],
        "springs": [{"name": "Top", "joints": [
            {"node": 3, "hitRadius": 0.02, "stiffness": 1.0, "gravityPower": 0.5, "gravityDir": [0, -1, 0], "dragForce": 0.4},
            {"node": 4, "hitRadius": 0.02, "stiffness": 1.0, "gravityPower": 0.5, "gravityDir": [0, -1, 0], "dragForce": 0.4}],
            "colliderGroups": [0]}]}},
    "accessors": accessors,
    "bufferViews": views,
    "buffers": [{"byteLength": len(buf), "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(buf)).decode()}],
}

with open(OUT, "w", encoding="utf-8", newline="\n") as f:
    json.dump(doc, f, indent=1)
    f.write("\n")
print("wrote", OUT, len(buf), "bytes of buffer")
