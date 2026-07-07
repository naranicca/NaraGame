"""Extract the teal cube from Desktop/n.png and produce scene-editor.ico.

The source PNG has a checkerboard-pattern transparent-indicator background
(alternating white / light-grey squares). Flood-filling from the four corners
peels that away without touching the white "N" glyph, which is enclosed by
the cube's outline. We then tight-crop to the cube's bounding box, resample
to a set of ICO sizes (16 / 32 / 48 / 64 / 128 / 256), and write the .ico.

Also emits scene-editor-icon.png (256x256) alongside the .ico for anyone
who wants the loose PNG for docs / About boxes.
"""

from __future__ import annotations
import sys
from pathlib import Path
from PIL import Image
from collections import deque

# ---------------------------------------------------------------------------
# Paths
SRC = Path.home() / "Desktop" / "n.png"
OUT_DIR = Path(__file__).resolve().parent
OUT_ICO = OUT_DIR / "scene-editor.ico"
OUT_PNG = OUT_DIR / "scene-editor-icon.png"

# ---------------------------------------------------------------------------
# Load + pad an alpha channel so we can just overwrite alpha to 0 on removal.
src = Image.open(SRC).convert("RGBA")
w, h = src.size
print(f"loaded {SRC} -> {w}x{h}")

pixels = src.load()

def is_bg(rgba):
    """Checkerboard background heuristic.

    Background squares are near-white (255,255,255) or near-light-grey
    (~240,240,240) with negligible chroma. The cube's teal is heavily
    saturated (chroma dominates), and the "N" glyph is pure white but
    is landlocked inside the cube so flood-fill from the corners never
    reaches it.
    """
    r, g, b, _ = rgba
    if min(r, g, b) < 200:
        return False
    chroma = max(r, g, b) - min(r, g, b)
    return chroma < 25

# BFS flood fill from every edge pixel that classifies as background.
# Any pixel reached by the fill becomes fully transparent; everything
# else keeps its original color.
visited = bytearray(w * h)
q: deque[tuple[int, int]] = deque()

def enqueue(x, y):
    if 0 <= x < w and 0 <= y < h and not visited[y * w + x]:
        if is_bg(pixels[x, y]):
            visited[y * w + x] = 1
            q.append((x, y))

for x in range(w):
    enqueue(x, 0)
    enqueue(x, h - 1)
for y in range(h):
    enqueue(0, y)
    enqueue(w - 1, y)

while q:
    x, y = q.popleft()
    enqueue(x + 1, y)
    enqueue(x - 1, y)
    enqueue(x, y + 1)
    enqueue(x, y - 1)

# Materialize alpha changes.
cleared = 0
for y in range(h):
    for x in range(w):
        if visited[y * w + x]:
            r, g, b, _ = pixels[x, y]
            pixels[x, y] = (r, g, b, 0)
            cleared += 1
print(f"flood-fill cleared {cleared} background pixels")

# ---------------------------------------------------------------------------
# Tight crop to the non-transparent bounding box, then re-pad to a square
# so downscaling to 16x16 etc. stays symmetric.
bbox = src.getbbox()
print(f"cube bbox = {bbox}")
if not bbox:
    print("ERROR: nothing left after flood fill", file=sys.stderr)
    sys.exit(1)
cube = src.crop(bbox)
cw, ch = cube.size
side = max(cw, ch)
# 8% breathing room so the smallest icon sizes don't clip edges.
side = int(side * 1.08)
square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
square.paste(cube, ((side - cw) // 2, (side - ch) // 2), cube)

# High-quality resample to a comfortable master size.
master = square.resize((512, 512), Image.LANCZOS)
master.save(OUT_PNG)
print(f"wrote {OUT_PNG}")

# ---------------------------------------------------------------------------
# Write multi-size ICO. Pillow picks up sizes= and stores each as a separate
# image, which is what Windows Explorer / the taskbar want.
sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
master.save(OUT_ICO, format="ICO", sizes=sizes)
print(f"wrote {OUT_ICO}  ({', '.join(f'{s[0]}x{s[1]}' for s in sizes)})")
