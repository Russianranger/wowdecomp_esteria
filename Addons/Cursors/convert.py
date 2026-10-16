"""Package cursor art and ANI timing for the native 32x32 renderer (requires Pillow)."""

from pathlib import Path
import struct

from PIL import Image


def convert(source: Path, destination: Path, key_black: bool = False) -> None:
    image = Image.open(source).convert("RGBA")
    if key_black:
        # shortcut: opaque black backdrop keyed out by threshold, fine for these frames.
        image.putdata([(r, g, b, 0) if max(r, g, b) < 8 else (r, g, b, a) for r, g, b, a in image.getdata()])
    if image.size == (64, 64):
        # Premultiply so transparent pixels do not bleed dark fringes into the edge.
        image = image.convert("RGBa").resize((32, 32), Image.Resampling.BOX).convert("RGBA")
    if image.size == (32, 36):
        # Human has two transparent rows above and below its 32-pixel artwork.
        if image.getchannel("A").crop((0, 0, 32, 2)).getbbox() or image.getchannel("A").crop((0, 34, 32, 36)).getbbox():
            raise ValueError(f"{source}: trimming would remove visible pixels")
        image = image.crop((0, 2, 32, 34))
    if image.size != (32, 32):
        raise ValueError(f"{source}: expected a 32x32 cursor, got {image.size}")

    # The engine reads the palette + separate alpha encoding reliably, so reduce
    # RGB to 256 colors when needed (alpha stays exact).
    if len(set(image.convert("RGB").getdata())) > 256:
        rgb = image.convert("RGB").quantize(256, Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert("RGB")
        image = Image.merge("RGBA", (*rgb.split(), image.getchannel("A")))
    raw = image.tobytes()
    rgba = list(zip(raw[0::4], raw[1::4], raw[2::4], raw[3::4]))
    colors = list(dict.fromkeys(pixel[:3] for pixel in rgba))
    if len(colors) <= 256:
        encoding = 1
        indices = {color: index for index, color in enumerate(colors)}
        palette = b"".join(bytes((b, g, r, 0)) for r, g, b in colors)
        palette += bytes((256 - len(colors)) * 4)
        pixels = bytes(indices[pixel[:3]] for pixel in rgba) + image.getchannel("A").tobytes()
    else:
        encoding = 3
        palette = bytes(1024)
        pixels = image.tobytes("raw", "BGRA")
    offset = 148 + 1024
    header = struct.pack("<4sI4BII", b"BLP2", 1, encoding, 8, 2, 0, 32, 32)
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
        if data[8] == 1:
            index = data[mip_offset + pixel]
            b, g, r, _ = struct.unpack_from("4B", data, 148 + index * 4)
            a = data[mip_offset + 1024 + pixel]
        else:
            b, g, r, a = struct.unpack_from("4B", data, mip_offset + pixel * 4)
        decoded.extend((r, g, b, a))
    if bytes(decoded) != image.tobytes():
        raise ValueError(f"{destination}: BLP pixel round-trip failed")
    print(f"{source.name} -> {destination.name}: 32x32 BLP2, alpha preserved")


def read_ani_timing(source: Path) -> tuple[int, list[int], list[int]]:
    data = source.read_bytes()
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"ACON":
        raise ValueError(f"{source}: not an animated cursor")
    end = struct.unpack_from("<I", data, 4)[0] + 8
    # This supplied ANI's exporter counted the eight-byte RIFF header in size.
    if end == len(data) + 8:
        end = len(data)
    if end != len(data):
        raise ValueError(f"{source}: invalid RIFF size")
    chunks = {}
    offset = 12
    while offset + 8 <= end:
        tag, size = struct.unpack_from("<4sI", data, offset)
        if offset + 8 + size > end:
            raise ValueError(f"{source}: truncated ANI chunk")
        chunks[tag] = data[offset + 8:offset + 8 + size]
        offset += 8 + size + size % 2
    header = chunks.get(b"anih", b"")
    if len(header) != 36:
        raise ValueError(f"{source}: missing ANI header")
    _, frames, steps, _, _, _, _, default_rate, flags = struct.unpack("<9I", header)
    if not frames or not steps or not default_rate:
        raise ValueError(f"{source}: empty ANI sequence")
    rates = list(struct.unpack(f"<{steps}I", chunks[b"rate"])) if b"rate" in chunks else [default_rate] * steps
    sequence = list(struct.unpack(f"<{steps}I", chunks[b"seq "])) if flags & 2 else list(range(steps))
    if any(rate == 0 for rate in rates) or any(frame >= frames for frame in sequence):
        raise ValueError(f"{source}: invalid ANI timing or frame index")
    return frames, sequence, rates


def convert_all(root: Path) -> None:
    for name in ("Human", "NightElf", "Orc", "Undead"):
        convert(root / "Raw" / f"{name}.png", root / "Textures" / f"{name}.blp")
    convert(root / "Raw/Hearthstone/Hearthstone-Hand.png", root / "Textures/Hearthstone.blp")
    frames, sequence, rates = read_ani_timing(root / "Raw/Starcraft/Cursor.ani")
    if frames != 5:
        raise ValueError("The native Starcraft theme expects five PNG frames")
    for frame in range(1, frames + 1):
        convert(root / "Raw/Starcraft" / f"normal cursor_page_{frame:02}.png",
                root / "Textures" / f"Starcraft{frame:02}.blp", key_black=True)
    timeline = ["// Generated by Addons/Cursors/convert.py from Starcraft/Cursor.ani.",
                "#pragma once", "#include <cstdint>",
                "struct CursorAnimationStep { uint32_t frame; uint32_t endJiffy; };",
                "inline constexpr CursorAnimationStep kStarcraftSteps[] = {"]
    end = 0
    for frame, rate in zip(sequence, rates):
        end += rate
        timeline.append(f"    {{ {frame}, {end} }},")
    timeline += ["};", f"inline constexpr uint32_t kStarcraftCycleJiffies = {end};", ""]
    (root / "Textures/StarcraftAnimation.h").write_text("\n".join(timeline), encoding="utf-8")
    print(f"Starcraft: {frames} frames, {len(sequence)} steps, {end}/60 seconds per loop")


if __name__ == "__main__":
    convert_all(Path(__file__).resolve().parent)
