# Makes the small sample images in docs/samples/images (run from the repository root):
#   python docs/samples/images/make_samples.py docs/samples/images
# Only the standard library (PNG = zlib + a few chunks).
import math, os, struct, sys, zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def write_png(path, w, h, pixel):
    # pixel(x, y) -> (r, g, b, a), 0..255; y = 0 is the top row.
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # no filter
        for x in range(w):
            raw.extend(pixel(x, y))

    def chunk(kind, data):
        c = struct.pack(">I", len(data)) + kind + data
        return c + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


# slime.png: 16 x 16 pixel art (try 「くっきり (ドット絵)」 and 1 単位のピクセル数 16), transparent around it.
SLIME = [
    "................",
    "................",
    "................",
    "......####......",
    "....##oooo##....",
    "...#oooooooo#...",
    "..#oowoooowoo#..",
    "..#oobooooboo#..",
    ".#oooooooooooo#.",
    ".#oooommmmoooo#.",
    ".#ooooommooooo#.",
    ".#oooooooooooo#.",
    "..#oooooooooo#..",
    "...##########...",
    "................",
    "................",
]
COLORS = {".": (0, 0, 0, 0), "#": (30, 60, 40, 255), "o": (90, 200, 110, 255), "w": (255, 255, 255, 255),
          "b": (20, 20, 30, 255), "m": (40, 90, 50, 255)}
write_png(os.path.join(out, "slime.png"), 16, 16, lambda x, y: COLORS[SLIME[y][x]])


# arrow.png: 128 x 64, an arrow pointing right (+X) - shows the turn and the flips.
def arrow(x, y):
    cx, cy = x + 0.5, y + 0.5
    inside = (16 <= cx <= 84 and 24 <= cy <= 40) or (cx >= 84 and cx <= 120 and abs(cy - 32) <= (120 - cx) * 0.8)
    if not inside:
        return (0, 0, 0, 0)
    t = cx / 128
    return (int(240 - 80 * t), int(170 + 40 * t), 60, 255)


write_png(os.path.join(out, "arrow.png"), 128, 64, arrow)


# ball.png: 100 x 100 (1 unit at 100 pixels per unit), a soft ball with transparent edges.
def ball(x, y):
    dx, dy = (x + 0.5 - 50) / 48, (y + 0.5 - 50) / 48
    d = math.hypot(dx, dy)
    if d > 1:
        return (0, 0, 0, 0)
    light = max(0.0, 1 - math.hypot(dx + 0.35, dy + 0.35)) * 0.8
    a = int(255 * min(1.0, (1 - d) * 20))
    return (int(70 + 185 * light), int(130 + 125 * light), 255, a)


write_png(os.path.join(out, "ball.png"), 100, 100, ball)
print("wrote", out)
