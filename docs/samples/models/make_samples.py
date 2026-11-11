# Makes the small sample models in docs/samples/models (run from the repository root).
import base64, json, math, os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

def box(sx, sy, sz):
    # 6 faces, own normals, CCW from outside; origin at the bottom middle.
    verts, idx = [], []
    faces = [((1,0,0),(0,1,0),(0,0,1)), ((-1,0,0),(0,1,0),(0,0,-1)), ((0,1,0),(0,0,1),(1,0,0)),
             ((0,-1,0),(0,0,-1),(1,0,0)), ((0,0,1),(0,1,0),(-1,0,0)), ((0,0,-1),(0,1,0),(1,0,0))]
    for n, u, v in faces:
        base = len(verts)
        for a, b in ((-1,-1),(1,-1),(1,1),(-1,1)):
            p = [n[i]*0.5 + u[i]*0.5*a + v[i]*0.5*b for i in range(3)]
            p = [p[0]*sx, (p[1]+0.5)*sy, p[2]*sz]
            verts.append((p, n, ((a+1)/2, (1-b)/2)))
        tri = [0,1,2,0,2,3]
        # make sure CCW faces outward
        p0, p1, p2 = verts[base][0], verts[base+1][0], verts[base+2][0]
        e1 = [p1[i]-p0[i] for i in range(3)]; e2 = [p2[i]-p0[i] for i in range(3)]
        c = (e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0])
        if sum(c[i]*n[i] for i in range(3)) < 0: tri = [0,2,1,0,3,2]
        idx += [base+t for t in tri]
    return verts, idx

def roof(w, h, d):
    # A prism: the ridge along X, at height h; origin at the bottom middle. Flat faces.
    A, B = (-w/2, 0, d/2), (w/2, 0, d/2)
    C, D = (w/2, 0, -d/2), (-w/2, 0, -d/2)
    E, F = (-w/2, h, 0), (w/2, h, 0)
    quads = [(A, B, F, E), (C, D, E, F)]
    tris = [(B, C, F), (D, A, E)]
    verts, idx = [], []
    def normal(p0, p1, p2):
        e1 = [p1[i]-p0[i] for i in range(3)]; e2 = [p2[i]-p0[i] for i in range(3)]
        c = (e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0])
        l = math.sqrt(sum(x*x for x in c)); return tuple(x/l for x in c)
    for q in quads:
        n = normal(q[0], q[1], q[2]); base = len(verts)
        for k, p in enumerate(q): verts.append((p, n, ((0,0),(1,0),(1,1),(0,1))[k]))
        idx += [base, base+1, base+2, base, base+2, base+3]
    for t in tris:
        n = normal(*t); base = len(verts)
        for k, p in enumerate(t): verts.append((p, n, ((0,1),(1,1),(0.5,0))[k]))
        idx += [base, base+1, base+2]
    return verts, idx

def pack(prims):
    # prims: list of (verts, idx) -> bin, bufferViews, accessors, meshes
    bin_ = bytearray(); views = []; accs = []; meshes = []
    for verts, idx in prims:
        def view(data, target):
            while len(bin_) % 4: bin_.append(0)
            views.append({"buffer": 0, "byteOffset": len(bin_), "byteLength": len(data), "target": target})
            bin_.extend(data); return len(views) - 1
        pos = b"".join(struct.pack("<3f", *v[0]) for v in verts)
        nrm = b"".join(struct.pack("<3f", *v[1]) for v in verts)
        uv = b"".join(struct.pack("<2f", *v[2]) for v in verts)
        ind = b"".join(struct.pack("<H", i) for i in idx)
        mn = [min(v[0][i] for v in verts) for i in range(3)]; mx = [max(v[0][i] for v in verts) for i in range(3)]
        a0 = len(accs)
        accs.append({"bufferView": view(pos, 34962), "componentType": 5126, "count": len(verts), "type": "VEC3", "min": mn, "max": mx})
        accs.append({"bufferView": view(nrm, 34962), "componentType": 5126, "count": len(verts), "type": "VEC3"})
        accs.append({"bufferView": view(uv, 34962), "componentType": 5126, "count": len(verts), "type": "VEC2"})
        accs.append({"bufferView": view(ind, 34963), "componentType": 5123, "count": len(idx), "type": "SCALAR"})
        meshes.append({"primitives": [{"attributes": {"POSITION": a0, "NORMAL": a0+1, "TEXCOORD_0": a0+2}, "indices": a0+3}]})
    while len(bin_) % 4: bin_.append(0)
    return bytes(bin_), views, accs, meshes

body = box(2.0, 1.4, 1.6)
top = roof(2.3, 0.9, 1.9)
chimney = box(0.3, 0.7, 0.3)
bin_, views, accs, meshes = pack([body, top, chimney])
meshes[0]["name"] = "Body"; meshes[1]["name"] = "Roof"; meshes[2]["name"] = "Chimney"
doc = {
    "asset": {"version": "2.0", "generator": "RM Engine sample (docs/samples/models)"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    # The body is the root; the roof sits on it (a child, moved up); the chimney is on the roof, turned a little.
    "nodes": [
        {"name": "House", "mesh": 0, "children": [1]},
        {"name": "Roof", "mesh": 1, "translation": [0, 1.4, 0], "children": [2]},
        {"name": "Chimney", "mesh": 2, "translation": [0.55, 0.1, -0.35], "rotation": [0, math.sin(math.radians(10)), 0, math.cos(math.radians(10))]},
    ],
    "meshes": meshes,
    "bufferViews": views,
    "accessors": accs,
}
gltf = dict(doc)
gltf["buffers"] = [{"byteLength": len(bin_), "uri": "data:application/octet-stream;base64," + base64.b64encode(bin_).decode()}]
with open(os.path.join(out, "house.gltf"), "w", encoding="utf-8", newline="\n") as f:
    json.dump(gltf, f, indent=1); f.write("\n")

glbdoc = dict(doc)
glbdoc["buffers"] = [{"byteLength": len(bin_)}]
js = json.dumps(glbdoc, separators=(",", ":")).encode()
while len(js) % 4: js += b" "
glb = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_)) + struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(bin_), 0x004E4942) + bin_
with open(os.path.join(out, "house.glb"), "wb") as f: f.write(glb)

# A torus (ring) with smooth normals: shows the smooth light and a shadow with a hole.
R, r, nu, nv = 1.0, 0.35, 48, 24
lines = ["# RM Engine sample (docs/samples/models): a torus, smooth normals", "o Torus"]
for i in range(nu):
    u = 2 * math.pi * i / nu
    for j in range(nv):
        v = 2 * math.pi * j / nv
        x = (R + r * math.cos(v)) * math.cos(u); z = (R + r * math.cos(v)) * math.sin(u); y = r * math.sin(v) + r
        lines.append(f"v {x:.5f} {y:.5f} {z:.5f}")
for i in range(nu + 1):
    for j in range(nv + 1):
        lines.append(f"vt {i/nu:.5f} {j/nv:.5f}")
for i in range(nu):
    u = 2 * math.pi * i / nu
    for j in range(nv):
        v = 2 * math.pi * j / nv
        lines.append(f"vn {math.cos(v)*math.cos(u):.5f} {math.sin(v):.5f} {math.cos(v)*math.sin(u):.5f}")
def vi(i, j): return (i % nu) * nv + (j % nv) + 1
def ti(i, j): return i * (nv + 1) + j + 1
for i in range(nu):
    for j in range(nv):
        a, b, c, d = (i, j), (i, j + 1), (i + 1, j + 1), (i + 1, j)
        # CCW seen from outside
        lines.append("f " + " ".join(f"{vi(*p)}/{ti(*p)}/{vi(*p)}" for p in (a, b, c, d)))
with open(os.path.join(out, "torus.obj"), "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines) + "\n")
print("ok", len(glb), os.path.getsize(os.path.join(out, "torus.obj")))
