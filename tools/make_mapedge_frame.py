r"""The Oblivion Remastered framework's own frame art: an embroidered map's edge in gold and brown.

The owner, 2026-10-02: "give it a frame art similar to how Skyrim has a frame art, except this frame will be more like
an embroidered map's edge. Something a bit decorative in a gold or brown color."

Original art, drawn here from nothing but shapes (no game file), so it ships with the framework the way the Skyrim
framework ships its knotwork. One nine-slice texture: four corner ornaments and an edge band whose pattern repeats
every PERIOD pixels, so the renderer TILES the edges (DrawNineSlice, tiled mode) instead of stretching the stitches.

From the outside in, the band is
  - a thin dark-brown hem line;
  - a gold couched cord: slanted satin stitches with a light and a dark side, so it reads as twisted thread;
  - a brown running stitch (dash, gap).
At each corner the cords meet in a small compass star in gold, outlined in brown, inside a stitched ring.

Drawn at SS times the size and reduced with Lanczos, so every stitch is anti-aliased at 1x.
Writes include/MapEdgeBorder.h (the embedded RGBA, like KnotworkBorder.h) and tools/mapedge-preview.png.
Run: python tools/make_mapedge_frame.py
"""
import math
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

W = 84                 # texture size (square)
CORNER = 26            # corner slice, the knotwork's, so every theme keeps one layout; W - 2*CORNER = 32 px of tileable edge
PERIOD = 8             # edge pattern period - divides the 32 px edge, so the tile repeats seamlessly
SS = 8                 # supersampling
K = 2                  # the texture holds every design pixel as KxK: the renderer draws the frame at the UI's scale
                       # (1.67x at 1800 px tall), so a 1x texture would be magnified and lose its stitches

HEM = (74, 52, 32)             # dark brown
BROWN = (110, 78, 44)          # thread brown
GOLD_HI = (238, 205, 120)      # gold, lit side
GOLD = (201, 162, 74)          # gold
GOLD_LO = (138, 102, 40)       # gold, shadow side

# Band rows, in texture pixels from the outer edge.
HEM_AT, HEM_W = 2.0, 1.4
CORD_FROM, CORD_TO = 4.5, 11.5
STITCH_AT, STITCH_W = 14.0, 1.3
STITCH_ON = 5.0                # of every PERIOD px


def s(v):
    return v * SS * K


def edge_band(img, length):
    """The top edge band across [0, length) texture px, drawn on img (SS-sized) at y = 0 outward."""
    d = ImageDraw.Draw(img)
    # hem
    d.rectangle([0, s(HEM_AT), s(length), s(HEM_AT + HEM_W)], fill=HEM + (255,))
    # couched cord: one slanted stitch per period, a lit upper half and a shadowed lower half
    h = CORD_TO - CORD_FROM
    slant = 4.0
    stitch = PERIOD - 1.2      # stitches packed tight; the 1.2 px between them is the cord's own shadow
    d.rectangle([0, s(CORD_FROM), s(length), s(CORD_TO)], fill=GOLD_LO + (255,))
    for k in range(-2, int(length / PERIOD) + 3):
        x = k * PERIOD
        poly = [(x, CORD_FROM), (x + stitch, CORD_FROM), (x + stitch + slant, CORD_TO), (x + slant, CORD_TO)]
        d.polygon([(s(px), s(py)) for px, py in poly], fill=GOLD + (255,))
        lit = [(x + 0.9, CORD_FROM + 0.7), (x + stitch - 1.6, CORD_FROM + 0.7),
               (x + stitch - 1.6 + slant * 0.5, CORD_FROM + h * 0.5), (x + 0.9 + slant * 0.5, CORD_FROM + h * 0.5)]
        d.polygon([(s(px), s(py)) for px, py in lit], fill=GOLD_HI + (255,))
        dark = [(x + stitch + slant * 0.55, CORD_FROM + h * 0.55), (x + stitch + slant, CORD_TO),
                (x + stitch - 1.8 + slant, CORD_TO), (x + stitch - 1.8 + slant * 0.55, CORD_FROM + h * 0.55)]
        d.polygon([(s(px), s(py)) for px, py in dark], fill=GOLD_LO + (255,))
    # the couching thread's edges: a fine brown line above and below the cord
    d.rectangle([0, s(CORD_FROM - 0.6), s(length), s(CORD_FROM)], fill=BROWN + (200,))
    d.rectangle([0, s(CORD_TO), s(length), s(CORD_TO + 0.6)], fill=BROWN + (200,))
    # running stitch
    for k in range(-1, int(length / PERIOD) + 2):
        x = k * PERIOD + 1.5
        d.rounded_rectangle([s(x), s(STITCH_AT), s(x + STITCH_ON), s(STITCH_AT + STITCH_W)], radius=s(STITCH_W / 2),
                            fill=BROWN + (255,))


def star(d, cx, cy, r_out, r_in, points, rot, fill, outline=None, width=0):
    pts = []
    for i in range(points * 2):
        r = r_out if i % 2 == 0 else r_in
        a = rot + math.pi * i / points
        pts.append((s(cx + r * math.cos(a)), s(cy + r * math.sin(a))))
    d.polygon(pts, fill=fill, outline=outline, width=width)


def corner_ornament(img):
    """Top-left corner ornament on img (SS-sized), centred where the two cords meet."""
    d = ImageDraw.Draw(img)
    c = (CORD_FROM + CORD_TO) / 2.0 + 1.5        # where the cords' centre lines cross
    cx = cy = c + 2.0
    # stitched ring
    r = 10.5
    for i in range(16):
        a0 = 2 * math.pi * i / 16
        a1 = a0 + 2 * math.pi / 16 * 0.55
        d.arc([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], math.degrees(a0), math.degrees(a1), fill=BROWN + (255,),
              width=int(s(1.2)))
    # a disc of shadow so the star reads on any background
    d.ellipse([s(cx - 8.6), s(cy - 8.6), s(cx + 8.6), s(cy + 8.6)], fill=HEM + (235,))
    # compass star: four long points (gold, lit and shadowed halves) over four short ones
    star(d, cx, cy, 6.2, 2.0, 4, math.pi / 4, GOLD_LO + (255,))
    star(d, cx, cy, 8.2, 2.2, 4, -math.pi / 2, GOLD + (255,))
    for i in range(4):     # lit half of each long point
        a = -math.pi / 2 + i * math.pi / 2
        tip = (cx + 8.2 * math.cos(a), cy + 8.2 * math.sin(a))
        side = (cx + 2.2 * math.cos(a - math.pi / 4), cy + 2.2 * math.sin(a - math.pi / 4))
        d.polygon([(s(cx), s(cy)), (s(tip[0]), s(tip[1])), (s(side[0]), s(side[1]))], fill=GOLD_HI + (255,))
    d.ellipse([s(cx - 1.4), s(cy - 1.4), s(cx + 1.4), s(cy + 1.4)], fill=HEM + (255,))
    # little knots on the hem beyond the ring, where a map's border would have its corner fleuron
    for (kx, ky) in ((cx + 11.5, HEM_AT + HEM_W / 2), (HEM_AT + HEM_W / 2, cy + 11.5)):
        d.ellipse([s(kx - 1.6), s(ky - 1.6), s(kx + 1.6), s(ky + 1.6)], fill=GOLD + (255,), outline=HEM + (255,),
                  width=int(s(0.5)))


def build():
    big = Image.new("RGBA", (s(W), s(W)), (0, 0, 0, 0))
    # top band across the full width, then rotate copies for the other three sides
    top = Image.new("RGBA", (s(W), s(W)), (0, 0, 0, 0))
    edge_band(top, W)
    # mask the band's ends with a 45-degree mitre so neighbouring sides meet cleanly under the ornament
    mask = Image.new("L", (s(W), s(W)), 0)
    ImageDraw.Draw(mask).polygon([(0, 0), (s(W), 0), (s(W) - s(W) // 2, s(W) // 2), (s(W) // 2, s(W) // 2)], fill=255)
    band = Image.new("RGBA", top.size, (0, 0, 0, 0))
    band.paste(top, (0, 0), mask)
    for angle in (0, 90, 180, 270):
        big.alpha_composite(band.rotate(angle))
    # corner ornaments, drawn once and rotated into place
    orn = Image.new("RGBA", (s(W), s(W)), (0, 0, 0, 0))
    corner_ornament(orn)
    for angle in (0, 90, 180, 270):
        big.alpha_composite(orn.rotate(angle))
    return big.resize((W * K, W * K), Image.LANCZOS)


def write_header(img):
    data = img.tobytes()
    lines = []
    for i in range(0, len(data), 80):
        lines.append("\t" + ",".join(str(b) for b in data[i:i + 80]) + ",")
    path = os.path.join(REPO, "include", "MapEdgeBorder.h")
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("#pragma once\n")
        fh.write("// Generated by tools/make_mapedge_frame.py - do not hand-edit; change the script and run it again.\n")
        fh.write("// The framework's own frame art for Oblivion Remastered: an embroidered map's edge in gold and brown\n")
        fh.write(f"// (the owner, 2026-10-02). Original art drawn from shapes, no game file. {W * K}x{W * K} RGBA nine-slice:\n")
        fh.write(f"// {CORNER * K} px corners (a compass star in a stitched ring) and an edge band that repeats every {PERIOD * K} px,\n")
        fh.write("// drawn TILED along each side so the stitches keep their size. The texture is twice the design size: the\n")
        fh.write("// renderer draws the corner at kDrawCorner x the UI scale, so it stays sharp on a 4K screen.\n")
        fh.write("#include <cstdint>\n\nnamespace mapedge\n{\n")
        fh.write(f"\tconstexpr int kWidth = {W * K};\n\tconstexpr int kHeight = {W * K};\n\tconstexpr int kCorner = {CORNER * K};\n")
        fh.write(f"\tconstexpr int kDrawCorner = {CORNER};   // on screen at the 1080p baseline, before the UI scale\n")
        fh.write(f"\tconstexpr int kPeriod = {PERIOD * K};\n")
        fh.write(f"\tconstexpr unsigned char kRGBA[{W * K}*{W * K}*4] = {{\n")
        fh.write("\n".join(lines))
        fh.write("\n\t};\n}\n")
    return path


def preview(img):
    """A mock window on the parchment theme colour, the frame tiled the way the renderer draws it."""
    from itertools import product
    pw, ph = 520, 300
    out = Image.new("RGBA", (pw, ph), (228, 219, 204, 255))
    W2 = W * K
    c = CORNER * K
    def tile(src_box, dst_x0, dst_y0, dst_x1, dst_y1, horizontal):
        piece = img.crop(src_box)
        if horizontal:
            x = dst_x0
            while x < dst_x1:
                w = min(piece.width, dst_x1 - x)
                out.alpha_composite(piece.crop((0, 0, w, piece.height)), (x, dst_y0))
                x += piece.width
        else:
            y = dst_y0
            while y < dst_y1:
                h = min(piece.height, dst_y1 - y)
                out.alpha_composite(piece.crop((0, 0, piece.width, h)), (dst_x0, y))
                y += piece.height
    tile((c, 0, W2 - c, c), c, 0, pw - c, 0, True)
    tile((c, W2 - c, W2 - c, W2), c, ph - c, pw - c, ph - c, True)
    tile((0, c, c, W2 - c), 0, c, 0, ph - c, False)
    tile((W2 - c, c, W2, W2 - c), pw - c, c, pw - c, ph - c, False)
    for (sx, sy), (dx, dy) in zip(((0, 0), (W2 - c, 0), (0, W2 - c), (W2 - c, W2 - c)),
                                  ((0, 0), (pw - c, 0), (0, ph - c), (pw - c, ph - c))):
        out.alpha_composite(img.crop((sx, sy, sx + c, sy + c)), (dx, dy))
    path = os.path.join(HERE, "mapedge-preview.png")
    out.save(path)
    img.resize((W2 * 3, W2 * 3), Image.NEAREST).save(os.path.join(HERE, "mapedge-texture-x6.png"))
    return path


if __name__ == "__main__":
    frame = build()
    print(write_header(frame))
    print(preview(frame))
