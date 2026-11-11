# Makes docs/samples/models/pyramid.fbx: a small binary FBX 7.4 written the way 3D tools write them
# (Z up, centimetres, zlib-compressed arrays, normals by polygon vertex, two materials, a parent
# model turned and a child model on it). Standard library only. Run from the repository root:
#   python docs/samples/models/make_fbx_sample.py
import math, os, struct, zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pyramid.fbx")

def prop_l(v): return b"L" + struct.pack("<q", v)
def prop_i(v): return b"I" + struct.pack("<i", v)
def prop_d(v): return b"D" + struct.pack("<d", v)
def prop_s(s):
    b = s.encode("utf-8") if isinstance(s, str) else s
    return b"S" + struct.pack("<I", len(b)) + b
def arr(kind, values, compress=True):
    fmt = {"d": "<%dd", "i": "<%di"}[kind] % len(values)
    raw = struct.pack(fmt, *values)
    if compress:
        data = zlib.compress(raw, 9)
        return kind.encode() + struct.pack("<III", len(values), 1, len(data)) + data
    return kind.encode() + struct.pack("<III", len(values), 0, len(raw)) + raw

def node(name, props=(), kids=()):
    return (name, list(props), list(kids))

def write(n, out):
    name, props, kids = n
    start = len(out)
    out += struct.pack("<III", 0, len(props), 0) + bytes([len(name)]) + name.encode()
    p0 = len(out)
    for p in props: out += p
    struct.pack_into("<I", out, start + 8, len(out) - p0)
    for k in kids: write(k, out)
    if kids: out += b"\0" * 13
    struct.pack_into("<I", out, start, len(out))

def P(name, *values, kind="Vector3D"):
    return node("P", [prop_s(name), prop_s(kind), prop_s(""), prop_s("A")] + [prop_d(v) for v in values])
def PI(name, v): return node("P", [prop_s(name), prop_s("int"), prop_s("Integer"), prop_s(""), prop_i(v)])

# A pyramid in centimetres, Z up: a square base 100 x 100 and a top 120 up.
pts = [(-50, -50, 0), (50, -50, 0), (50, 50, 0), (-50, 50, 0), (0, 0, 120)]
faces = [[3, 2, 1, 0], [0, 1, 4], [1, 2, 4], [2, 3, 4], [3, 0, 4]]  # counter-clockwise from outside
pvi, normals, uvs = [], [], []
for f in faces:
    a, b, c = (pts[i] for i in f[:3])
    e1 = [b[k] - a[k] for k in range(3)]; e2 = [c[k] - a[k] for k in range(3)]
    n = (e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0])
    l = math.sqrt(sum(x*x for x in n)); n = [x / l for x in n]
    for k, i in enumerate(f):
        pvi.append(i if k < len(f) - 1 else -i - 1)
        normals += n
        uvs += [(pts[i][0] + 50) / 100, (pts[i][1] + 50) / 100]
geometry = node("Geometry", [prop_l(1000), prop_s(b"Pyramid\x00\x01Geometry"), prop_s("Mesh")], [
    node("Vertices", [arr("d", [c for p in pts for c in p])]),
    node("PolygonVertexIndex", [arr("i", pvi)]),
    node("LayerElementNormal", [prop_i(0)], [node("MappingInformationType", [prop_s("ByPolygonVertex")]),
                                             node("ReferenceInformationType", [prop_s("Direct")]),
                                             node("Normals", [arr("d", normals)])]),
    node("LayerElementUV", [prop_i(0)], [node("MappingInformationType", [prop_s("ByPolygonVertex")]),
                                         node("ReferenceInformationType", [prop_s("Direct")]),
                                         node("UV", [arr("d", uvs)])]),
    node("LayerElementMaterial", [prop_i(0)], [node("MappingInformationType", [prop_s("ByPolygon")]),
                                               node("ReferenceInformationType", [prop_s("IndexToDirect")]),
                                               node("Materials", [arr("i", [0, 1, 1, 1, 1], compress=False)])]),
])
# The base stone grey; the sides sand. The pyramid sits on a turned plinth model (its parent).
stone = node("Material", [prop_l(3000), prop_s(b"Stone\x00\x01Material"), prop_s("")],
             [node("Properties70", [], [P("DiffuseColor", 0.45, 0.45, 0.48, kind="Color")])])
sand = node("Material", [prop_l(3001), prop_s(b"Sand\x00\x01Material"), prop_s("")],
            [node("Properties70", [], [P("DiffuseColor", 0.93, 0.78, 0.45, kind="Color"), P("DiffuseFactor", 1.0, kind="Number")])])
plinth = node("Model", [prop_l(2000), prop_s(b"Plinth\x00\x01Model"), prop_s("Null")],
              [node("Properties70", [], [P("Lcl Rotation", 0, 0, 30)])])
pyramid = node("Model", [prop_l(2001), prop_s(b"Pyramid\x00\x01Model"), prop_s("Mesh")],
               [node("Properties70", [], [P("Lcl Translation", 0, 0, 10)])])
def C(kind, child, parent, prop=None):
    props = [prop_s(kind), prop_l(child), prop_l(parent)] + ([prop_s(prop)] if prop else [])
    return node("C", props)
top = [
    node("FBXHeaderExtension", [], [node("FBXVersion", [prop_i(7400)]), node("Creator", [prop_s("RM Engine sample (make_fbx_sample.py)")])]),
    node("GlobalSettings", [], [node("Properties70", [], [PI("UpAxis", 2), PI("UpAxisSign", 1), PI("FrontAxis", 1), PI("FrontAxisSign", -1),
                                                           PI("CoordAxis", 0), PI("CoordAxisSign", 1), P("UnitScaleFactor", 1.0, kind="double")])]),
    node("Objects", [], [geometry, plinth, pyramid, stone, sand]),
    node("Connections", [], [C("OO", 2000, 0), C("OO", 2001, 2000), C("OO", 1000, 2001), C("OO", 3000, 2001), C("OO", 3001, 2001)]),
]
out = bytearray(b"Kaydara FBX Binary  \x00\x1a\x00" + struct.pack("<I", 7400))
for n in top: write(n, out)
out += b"\0" * 13 + b"\xfa" * 16
with open(OUT, "wb") as f: f.write(out)
print("wrote", OUT, len(out), "bytes")
