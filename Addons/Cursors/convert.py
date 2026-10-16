"""Convert the supplied cursor art to lossless, 32x32 BLP2 (requires Pillow)."""

from pathlib import Path
import struct

from PIL import Image


def convert(source: Path, destination: Path) -> None:
    image = Image.open(source).convert("RGBA")
    if image.size == (32, 36):
        # Human has two transparent rows above and below its 32-pixel artwork.
        if image.getchannel("A").crop((0, 0, 32, 2)).getbbox() or image.getchannel("A").crop((0, 34, 32, 36)).getbbox():
            raise ValueError(f"{source}: trimming would remove visible pixels")
        image = image.crop((0, 2, 32, 34))
    if image.size != (32, 32):
        raise ValueError(f"{source}: expected a 32x32 cursor, got {image.size}")

    # The art has fewer than 256 RGB colors. An exact palette plus a separate
    # 8-bit alpha plane preserves every pixel without DXT compression artifacts.
    raw = image.tobytes()
    rgba = list(zip(raw[0::4], raw[1::4], raw[2::4], raw[3::4]))
    colors = list(dict.fromkeys(pixel[:3] for pixel in rgba))
    if len(colors) > 256:
        raise ValueError(f"{source}: too many colors for lossless palette encoding")
    indices = {color: index for index, color in enumerate(colors)}
    palette = b"".join(bytes((b, g, r, 0)) for r, g, b in colors)
    palette += bytes((256 - len(colors)) * 4)
    offset = 148 + 1024
    pixels = bytes(indices[pixel[:3]] for pixel in rgba) + image.getchannel("A").tobytes()
    header = struct.pack("<4sI4BII", b"BLP2", 1, 1, 8, 2, 0, 32, 32)
    header += struct.pack("<16I", offset, *([0] * 15))
    header += struct.pack("<16I", len(pixels), *([0] * 15))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(header + palette + pixels)
    # Read back the native BLP2 palette + separate alpha layout. Pillow's BLP
    # decoder incorrectly takes alpha from palette entries for this encoding.
    data = destination.read_bytes()
    mip_offset = struct.unpack_from("<I", data, 20)[0]
    decoded = bytearray()
    for pixel in range(1024):
        index = data[mip_offset + pixel]
        b, g, r, _ = struct.unpack_from("4B", data, 148 + index * 4)
        decoded.extend((r, g, b, data[mip_offset + 1024 + pixel]))
    if bytes(decoded) != image.tobytes():
        raise ValueError(f"{destination}: BLP pixel round-trip failed")
    print(f"{source.name} -> {destination.name}: 32x32 BLP2, alpha preserved")


if __name__ == "__main__":
    root = Path(__file__).resolve().parent
    for name in ("Human", "NightElf", "Orc", "Undead"):
        convert(root / "Raw" / f"{name}.png", root / "Textures" / f"{name}.blp")
