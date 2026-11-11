# Makes the small sample models in docs/samples/models (run from the repository root):
#   python docs/samples/models/make_samples.py docs/samples/models
#
#   house.gltf + house_walls.png + roof_tiles.png   glTF: materials, pictures in files next to it
#   house.glb                                       the same house in one file (pictures inside it)
#   crate.gltf                                      a box with its picture written inside (data: URI)
#   torus.obj + torus.mtl + torus_stripes.png        OBJ: two materials from the .mtl (a picture and a colour)
#   pbr_ball.glb                                    metal / roughness: bands of polished metal and rough paint
#                                                   (a metal / rough picture), dimples (a normal map) and a
#                                                   glowing ring (an emission picture), all inside the file
#   glass.gltf                                      three see-through panes (alphaMode BLEND) one behind another
#
# Only the Python standard library (the PNGs are written by hand: zlib + the PNG chunks).
import base64, json, math, os, struct, sys, zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

# --- Pictures -------------------------------------------------------------------------------------

def png(w, h, pixel):
    # pixel(x, y) -> (r, g, b, a) 0..255; rows from the top.
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # no filter
        for x in range(w):
            raw.extend(bytes(max(0, min(255, int(v))) for v in pixel(x, y)))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))

def noise(x, y, seed):
    # A little grain, the same every time.
    n = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 255) / 255.0 - 0.5

def walls(x, y):
    # Cream plaster with dark wooden beams (a frame and a cross).
    s = 128
    beam = x < 8 or x >= s - 8 or y < 8 or y >= s - 8 or abs(x - y) < 6 or abs(x + y - s) < 6
    if beam:
        g = 1 + 0.25 * noise(x // 2, y, 3)
        return (92 * g, 60 * g, 38 * g, 255)
    g = 1 + 0.08 * noise(x, y, 1)
    return (236 * g, 222 * g, 190 * g, 255)

def tiles(x, y):
    # Rows of red-brown roof tiles, each row shifted by half a tile; dark lines between.
    row = y // 16
    tx = (x + (8 if row % 2 else 0)) % 16
    if y % 16 >= 14 or tx == 0:
        return (70, 32, 24, 255)
    shade = 1 - (y % 16) / 40 + 0.1 * noise(x // 16 + row * 7, row, 5)
    return (178 * shade, 74 * shade, 52 * shade, 255)

def crate(x, y):
    # A wooden crate: planks along X, a frame, and a diagonal brace.
    s = 64
    frame = x < 6 or x >= s - 6 or y < 6 or y >= s - 6 or abs(x - y) < 4
    g = 1 + 0.18 * noise(x // 3, y, 9) + (0.06 if (y // 8) % 2 else 0)
    if frame:
        return (150 * g, 98 * g, 50 * g, 255)
    if y % 8 == 0:
        return (90, 58, 30, 255)
    return (196 * g, 144 * g, 82 * g, 255)

def stripes(x, y):
    # Blue and white stripes around the ring.
    blue = (x // 8) % 2 == 0
    return (40, 96, 200, 255) if blue else (245, 245, 250, 255)

walls_png = png(128, 128, walls)
tiles_png = png(64, 64, tiles)
crate_png = png(64, 64, crate)
stripes_png = png(64, 16, stripes)
for name, data in (("house_walls.png", walls_png), ("roof_tiles.png", tiles_png), ("torus_stripes.png", stripes_png)):
    with open(os.path.join(out, name), "wb") as f: f.write(data)

# --- Shapes ---------------------------------------------------------------------------------------

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

def roof(w, h, d, repeat=(3, 2)):
    # A prism: the ridge along X, at height h; origin at the bottom middle. Flat faces. The UVs of the
    # two slopes go past 1 (the tiles repeat).
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
    ru, rv = repeat
    for q in quads:
        n = normal(q[0], q[1], q[2]); base = len(verts)
        for k, p in enumerate(q): verts.append((p, n, ((0,rv),(ru,rv),(ru,0),(0,0))[k]))
        idx += [base, base+1, base+2, base, base+2, base+3]
    for t in tris:
        n = normal(*t); base = len(verts)
        for k, p in enumerate(t): verts.append((p, n, ((0,1),(1,1),(0.5,0))[k]))
        idx += [base, base+1, base+2]
    return verts, idx

def pack(prims, images=()):
    # prims: list of (verts, idx, material) -> bin, bufferViews, accessors, meshes. `images`: picture
    # bytes to put in the buffer too (a .glb); their buffer views are returned last.
    bin_ = bytearray(); views = []; accs = []; meshes = []
    def view(data, target=None):
        while len(bin_) % 4: bin_.append(0)
        v = {"buffer": 0, "byteOffset": len(bin_), "byteLength": len(data)}
        if target: v["target"] = target
        views.append(v); bin_.extend(data); return len(views) - 1
    for verts, idx, material in prims:
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
        meshes.append({"primitives": [{"attributes": {"POSITION": a0, "NORMAL": a0+1, "TEXCOORD_0": a0+2}, "indices": a0+3, "material": material}]})
    image_views = [view(data) for data in images]
    while len(bin_) % 4: bin_.append(0)
    return bytes(bin_), views, accs, meshes, image_views

def linear(c):
    # glTF's baseColorFactor is linear light: the screen's 0..1 turned into it.
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

def write_glb(path, doc, bin_):
    js = json.dumps(doc, separators=(",", ":")).encode()
    while len(js) % 4: js += b" "
    glb = (struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_)) + struct.pack("<II", len(js), 0x4E4F534A) + js +
           struct.pack("<II", len(bin_), 0x004E4942) + bin_)
    with open(path, "wb") as f: f.write(glb)
    return len(glb)

# --- The house (glTF + GLB) -----------------------------------------------------------------------

materials = [
    {"name": "Walls", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0, "roughnessFactor": 0.9}},
    {"name": "Roof tiles", "pbrMetallicRoughness": {"baseColorTexture": {"index": 1}, "metallicFactor": 0, "roughnessFactor": 0.8}},
    # A colour only (dark red brick): no picture.
    {"name": "Brick", "pbrMetallicRoughness": {"baseColorFactor": [linear(0.55), linear(0.22), linear(0.18), 1], "metallicFactor": 0}},
]
body = box(2.0, 1.4, 1.6) + (0,)
top = roof(2.3, 0.9, 1.9) + (1,)
chimney = box(0.3, 0.7, 0.3) + (2,)

def house(bin_, views, accs, meshes):
    meshes[0]["name"] = "Body"; meshes[1]["name"] = "Roof"; meshes[2]["name"] = "Chimney"
    return {
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
        "materials": materials,
        "textures": [{"source": 0}, {"source": 1}],
        "bufferViews": views,
        "accessors": accs,
    }

# .gltf: the shape in a data: URI, the pictures in files next to it.
bin_, views, accs, meshes, _ = pack([body, top, chimney])
gltf = house(bin_, views, accs, meshes)
gltf["images"] = [{"uri": "house_walls.png"}, {"uri": "roof_tiles.png"}]
gltf["buffers"] = [{"byteLength": len(bin_), "uri": "data:application/octet-stream;base64," + base64.b64encode(bin_).decode()}]
with open(os.path.join(out, "house.gltf"), "w", encoding="utf-8", newline="\n") as f:
    json.dump(gltf, f, indent=1); f.write("\n")

# .glb: everything in one file (the pictures in the binary chunk).
bin_, views, accs, meshes, image_views = pack([body, top, chimney], [walls_png, tiles_png])
glbdoc = house(bin_, views, accs, meshes)
glbdoc["images"] = [{"bufferView": image_views[0], "mimeType": "image/png"}, {"bufferView": image_views[1], "mimeType": "image/png"}]
glbdoc["buffers"] = [{"byteLength": len(bin_)}]
glb_size = write_glb(os.path.join(out, "house.glb"), glbdoc, bin_)

# --- The crate (glTF, the picture in a data: URI) -------------------------------------------------

cube = box(1.0, 1.0, 1.0) + (0,)
bin_, views, accs, meshes, _ = pack([cube])
meshes[0]["name"] = "Crate"
crate_doc = {
    "asset": {"version": "2.0", "generator": "RM Engine sample (docs/samples/models)"},
    "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": "Crate", "mesh": 0}],
    "meshes": meshes,
    "materials": [{"name": "Wood", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0}}],
    "textures": [{"source": 0}],
    "images": [{"name": "crate", "uri": "data:image/png;base64," + base64.b64encode(crate_png).decode()}],
    "buffers": [{"byteLength": len(bin_), "uri": "data:application/octet-stream;base64," + base64.b64encode(bin_).decode()}],
    "bufferViews": views, "accessors": accs,
}
with open(os.path.join(out, "crate.gltf"), "w", encoding="utf-8", newline="\n") as f:
    json.dump(crate_doc, f, indent=1); f.write("\n")

# --- The torus (OBJ + MTL): the outer half striped (a picture), the inner half gold (a colour) ----

R, r, nu, nv = 1.0, 0.35, 48, 24
lines = ["# RM Engine sample (docs/samples/models): a torus, smooth normals, two materials", "mtllib torus.mtl", "o Torus"]
for i in range(nu):
    u = 2 * math.pi * i / nu
    for j in range(nv):
        v = 2 * math.pi * j / nv
        x = (R + r * math.cos(v)) * math.cos(u); z = (R + r * math.cos(v)) * math.sin(u); y = r * math.sin(v) + r
        lines.append(f"v {x:.5f} {y:.5f} {z:.5f}")
for i in range(nu + 1):
    for j in range(nv + 1):
        lines.append(f"vt {i/nu*4:.5f} {j/nv:.5f}")  # the stripes go round 4 times (the picture repeats)
for i in range(nu):
    u = 2 * math.pi * i / nu
    for j in range(nv):
        v = 2 * math.pi * j / nv
        lines.append(f"vn {math.cos(v)*math.cos(u):.5f} {math.sin(v):.5f} {math.cos(v)*math.sin(u):.5f}")
def vi(i, j): return (i % nu) * nv + (j % nv) + 1
def ti(i, j): return i * (nv + 1) + j + 1
for half, material in ((range(0, nv // 4), "Stripes"), (range(nv // 4, 3 * nv // 4), "Gold"), (range(3 * nv // 4, nv), "Stripes")):
    lines.append(f"usemtl {material}")
    for i in range(nu):
        for j in half:
            a, b, c, d = (i, j), (i, j + 1), (i + 1, j + 1), (i + 1, j)
            # CCW seen from outside
            lines.append("f " + " ".join(f"{vi(*p)}/{ti(*p)}/{vi(*p)}" for p in (a, b, c, d)))
with open(os.path.join(out, "torus.obj"), "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines) + "\n")
with open(os.path.join(out, "torus.mtl"), "w", encoding="utf-8", newline="\n") as f:
    f.write("# RM Engine sample (docs/samples/models): the torus's materials\n"
            "newmtl Stripes\nKd 1 1 1\nmap_Kd torus_stripes.png\n\n"
            "newmtl Gold\nKd 0.95 0.72 0.25\n")
# --- The PBR ball (GLB): metal / roughness, a normal map and emission --------------------------

def ball(radius, nu, nv):
    # A UV sphere centred above the floor; u around (0..1), v from the top (0) to the bottom (1).
    verts, idx = [], []
    for j in range(nv + 1):
        t = math.pi * j / nv
        for i in range(nu + 1):
            p_ = 2 * math.pi * i / nu
            n = (math.sin(t) * math.cos(p_), math.cos(t), -math.sin(t) * math.sin(p_))
            verts.append(([radius * n[0], radius * n[1] + radius, radius * n[2]], n, (i / nu, j / nv)))
    for j in range(nv):
        for i in range(nu):
            a = j * (nu + 1) + i; b = a + nu + 1
            idx += [a, b, a + 1, a + 1, b, b + 1]  # CCW from outside
    return verts, idx

W, H = 128, 64
def band(y):
    return (y * 8) // H  # 8 bands from the top
def metal_rough(x, y):
    # glTF: green = roughness, blue = metalness. Even bands: polished metal; odd bands: rough paint.
    return (0, 40, 255, 255) if band(y) % 2 == 0 else (0, 220, 0, 255)
def colour(x, y):
    return (230, 190, 120, 255) if band(y) % 2 == 0 else (40, 90, 160, 255)
def heights(x, y):
    # Round dimples on a grid (deeper in the middle).
    cx, cy = (x % 16) - 7.5, (y % 16) - 7.5
    d = math.sqrt(cx * cx + cy * cy)
    return -max(0.0, 1 - d / 6) ** 2
def normal(x, y):
    k = 3.0
    dx = (heights((x + 1) % W, y) - heights((x - 1) % W, y)) * 0.5
    dy = (heights(x, (y + 1) % H) - heights(x, (y - 1) % H)) * 0.5  # down the picture
    n = (-dx * k, dy * k, 1.0)  # +Y is up the picture
    l = math.sqrt(sum(c * c for c in n))
    return tuple(int((c / l * 0.5 + 0.5) * 255) for c in n) + (255,)
def emission(x, y):
    # A glowing ring round the middle.
    return (255, 120, 40, 255) if abs(y - H // 2) <= 2 else (0, 0, 0, 255)

ball_pngs = [png(W, H, colour), png(W, H, metal_rough), png(W, H, normal), png(W, H, emission)]
sphere = ball(0.6, 48, 24) + (0,)
bin_, views, accs, meshes, image_views = pack([sphere], ball_pngs)
meshes[0]["name"] = "Ball"
ball_doc = {
    "asset": {"version": "2.0", "generator": "RM Engine sample (docs/samples/models)"},
    "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": "PBR ball", "mesh": 0}],
    "meshes": meshes,
    "materials": [{
        "name": "Bands",
        "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 1, "roughnessFactor": 1,
                                 "metallicRoughnessTexture": {"index": 1}},
        "normalTexture": {"index": 2, "scale": 1},
        "emissiveTexture": {"index": 3}, "emissiveFactor": [1, 1, 1],
        "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 2}},
    }],
    "extensionsUsed": ["KHR_materials_emissive_strength"],
    "textures": [{"source": k} for k in range(4)],
    "images": [{"name": n, "bufferView": image_views[k], "mimeType": "image/png"} for k, n in enumerate(["colour", "metal_rough", "normal", "emission"])],
    "buffers": [{"byteLength": len(bin_)}],
    "bufferViews": views, "accessors": accs,
}
write_glb(os.path.join(out, "pbr_ball.glb"), ball_doc, bin_)

# --- Glass (glTF): three see-through panes, red / green / blue, one behind another ----------------
panes = [box(1.2, 1.2, 0.04) + (k,) for k in range(3)]
bin_, views, accs, meshes, _ = pack(panes)
glass_doc = {
    "asset": {"version": "2.0", "generator": "RM Engine sample (docs/samples/models)"},
    "scene": 0, "scenes": [{"nodes": [0, 1, 2]}],
    "nodes": [{"name": n, "mesh": k, "translation": [k * 0.25, 0, -k * 0.5]} for k, n in enumerate(["Red", "Green", "Blue"])],
    "meshes": meshes,
    "materials": [{"name": n, "alphaMode": "BLEND",
                   "pbrMetallicRoughness": {"baseColorFactor": [linear(c[0]), linear(c[1]), linear(c[2]), 0.45], "metallicFactor": 0, "roughnessFactor": 0.1}}
                  for n, c in (("Red glass", (0.9, 0.2, 0.2)), ("Green glass", (0.2, 0.85, 0.3)), ("Blue glass", (0.25, 0.4, 0.95)))],
    "buffers": [{"byteLength": len(bin_), "uri": "data:application/octet-stream;base64," + base64.b64encode(bin_).decode()}],
    "bufferViews": views, "accessors": accs,
}
with open(os.path.join(out, "glass.gltf"), "w", encoding="utf-8", newline="\n") as f:
    json.dump(glass_doc, f, indent=1); f.write("\n")

print("ok", glb_size, os.path.getsize(os.path.join(out, "torus.obj")))
