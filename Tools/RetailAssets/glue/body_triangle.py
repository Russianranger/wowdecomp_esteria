"""Body-type triangle art (body_triangle.png) -> the creator's BLP2 texture.

The art's content is cropped, scaled into a 512x512 canvas (WoW textures are power-of-two) and written as raw
BGRA BLP2 with mipmaps, the same layout as the client's Retail::BuildBlp. creator_skin.lua places it using
TRIANGLE below, so the stroke's three corners land on the Body page's knob triangle.
"""

import struct
from pathlib import Path

from PIL import Image

SOURCE = Path(__file__).with_name("body_triangle.png")
ENTRY = "Interface" + chr(92) + "Glues" + chr(92) + "CharacterCreate" + chr(92) + "EsteriaBodyTriangle.blp"
CROP = (150, 40, 1305, 1066)          # source pixels: triangle plus its three labels
CORNERS = ((724, 148), (242, 944), (1196, 944))  # stroke centre: apex, base left, base right (source pixels)
CANVAS = 512


def build():
    image = Image.open(SOURCE).convert("RGBA").crop(CROP)
    width = CANVAS
    height = round(image.height * CANVAS / image.width)
    canvas = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    canvas.paste(image.resize((width, height), Image.LANCZOS), (0, 0))
    blp = bytearray(1172)
    blp[0:4] = b"BLP2"
    struct.pack_into("<I", blp, 4, 1)
    blp[8:12] = bytes((3, 8, 8, 1))  # raw BGRA, 8-bit alpha, mips present
    struct.pack_into("<II", blp, 12, CANVAS, CANVAS)
    level = canvas
    for mip in range(16):
        r, g, b, a = level.split()
        data = Image.merge("RGBA", (b, g, r, a)).tobytes()
        struct.pack_into("<I", blp, 20 + mip * 4, len(blp))
        struct.pack_into("<I", blp, 84 + mip * 4, len(data))
        blp += data
        if level.size == (1, 1):
            break
        level = level.resize((max(1, level.width // 2), max(1, level.height // 2)), Image.BOX)
    return bytes(blp), height / CANVAS


def placement(tri_width=180, tri_height=156):
    """Size, offset and texcoord bottom for the texture on a tri_width x tri_height knob triangle frame."""
    (ax, ay), (lx, ly), (rx, _) = CORNERS
    sx, sy = tri_width / (rx - lx), tri_height / (ly - ay)
    left, top, right, bottom = CROP
    return {"width": (right - left) * sx, "height": (bottom - top) * sy,
            "x": -(lx - left) * sx, "y": (ly - top) * sy}


if __name__ == "__main__":
    data, v = build()
    print(len(data), "bytes; texcoord bottom", round(v, 6), placement())
