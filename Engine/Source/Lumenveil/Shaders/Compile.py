"""Compiles Lumenveil's passes to SPIR-V and writes Spirv.inc (the mod's passes, for Renderer.cpp) and DebugSpirv.inc
(the debug views, for Lumenveil.cpp): uint32_t arrays the code embeds.

Two kinds of source:

  *.frag here      GLSL written for this port (the debug views), compiled as they are.
  Core/shaders     Lumenveil's own Shader Model 3 HLSL, kept as upstream wrote it, translated here into the HLSL
                   glslang's front end reads, then compiled:
                     - the includes are expanded (each file once, as the upstream files never include one twice);
                     - every constant pinned to a register (float4 cX : register(c9)) moves into one cbuffer at the
                       GxEffects constant binding (16), at the same register by packoffset, so the renderer's
                       constant rows land where the D3D9 code put them; a vertex shader shares that one block;
                     - a sampler's register(sN) becomes binding N of set 0;
                     - VPOS (D3D9's pixel position, at the integer) becomes SV_Position - 0.5;
                     - a point-list vertex shader's grid position (POSITION, from a D3D9 vertex buffer of the grid)
                       comes from the vertex index and the grid width the renderer puts in c255, its clip-space y is
                       flipped (glslang's --invert-y), and the shaft memory's texel position moves half a texel to
                       Vulkan's pixel centre (D3D9 has its pixel centres at the integers).

The .inc files are committed: a build needs no glslang. Run this after editing a shader:

    python Compile.py [path/to/glslang.exe]

glslang is taken from the argument, else from GLSLANG in the environment, else from PATH.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
UPSTREAM = os.path.join(HERE, "..", "Core", "shaders")

GLSL = [
    ("DepthView.frag", "kDepthViewFrag"),
    ("DepthProbe.frag", "kDepthProbeFrag"),
]

# (upstream source, symbol, stage, defines), as engine/CMakeLists.txt compiles them.
HLSL = [
    ("vf_march.hlsl", "kPsMarchLow", "frag", {"STEPS": 16, "SHAFT_STEPS": 12}),
    ("vf_march.hlsl", "kPsMarchMid", "frag", {"STEPS": 24, "SHAFT_STEPS": 16}),
    ("vf_march.hlsl", "kPsMarchHigh", "frag", {"STEPS": 32, "SHAFT_STEPS": 24}),
    ("vf_march.hlsl", "kPsMarchMemoryLow", "frag", {"STEPS": 16, "SHAFT_STEPS": 12, "SHAFT_MEMORY": 1}),
    ("vf_march.hlsl", "kPsMarchMemoryMid", "frag", {"STEPS": 24, "SHAFT_STEPS": 16, "SHAFT_MEMORY": 1}),
    ("vf_march.hlsl", "kPsMarchMemoryHigh", "frag", {"STEPS": 32, "SHAFT_STEPS": 24, "SHAFT_MEMORY": 1}),
    ("vf_temporal.hlsl", "kPsTemporal", "frag", {}),
    ("vf_composite.hlsl", "kPsComposite", "frag", {}),
    ("vf_ray_mask.hlsl", "kPsRayMask", "frag", {}),
    ("vf_ray_blur.hlsl", "kPsRayBlur", "frag", {}),
    ("vf_glow_mask.hlsl", "kPsGlowMask", "frag", {}),
    ("vf_glow_blur.hlsl", "kPsGlowBlur", "frag", {}),
    ("vf_edge_occluders.hlsl", "kPsEdgeOccluders", "frag", {}),
    ("vf_water_depth.hlsl", "kPsWaterDepth", "frag", {}),
    ("vf_memory_scatter_vs.hlsl", "kVsMemoryScatter", "vert", {}),
    ("vf_memory_scatter.hlsl", "kPsMemoryScatter", "frag", {}),
    ("vf_memory_realign_vs.hlsl", "kVsMemoryRealign", "vert", {}),
    ("vf_memory_keep.hlsl", "kPsMemoryKeep", "frag", {}),
    ("vf_memory_fill.hlsl", "kPsMemoryFill", "frag", {}),
    ("vf_sky_reference.hlsl", "kPsSkyGather", "frag", {"PASS": 0}),
    ("vf_sky_reference.hlsl", "kPsSkyColumns", "frag", {"PASS": 1}),
    ("vf_sky_reference.hlsl", "kPsSkyReference", "frag", {"PASS": 2}),
    ("vf_lamp_visibility.hlsl", "kPsLampVisibility", "frag", {}),
    ("vf_lamps.hlsl", "kPsLamps", "frag", {}),
    ("vf_lamp_surfaces.hlsl", "kPsLampSurfaces", "frag", {}),
    ("vf_lamp_blur.hlsl", "kPsLampBlur", "frag", {}),
    ("vf_light_shadow.hlsl", "kPsLightShadow", "frag", {}),
    ("vf_water.hlsl", "kPsWater", "frag", {}),
    ("vf_water_reflect.hlsl", "kPsWaterReflect", "frag", {}),
    ("vf_water_memory.hlsl", "kPsWaterMemory", "frag", {}),
    ("vf_water_lamps.hlsl", "kPsWaterLamps", "frag", {}),
    ("vf_wet_surfaces.hlsl", "kPsWetSurfaces", "frag", {}),
    # The wave pass writes three targets (COLOR0..2); GxEffects has one, so it runs once for each.
    ("vf_water_waves.hlsl", "kPsWaterWaves0", "frag", {"WAVE_OUTPUT": 0}),
    ("vf_water_waves.hlsl", "kPsWaterWaves1", "frag", {"WAVE_OUTPUT": 1}),
    ("vf_water_waves.hlsl", "kPsWaterWaves2", "frag", {"WAVE_OUTPUT": 2}),
    ("vf_water_slope.hlsl", "kPsWaterSlope", "frag", {}),
    # The gather writes two targets (COLOR0, COLOR1); GxEffects has one, so it runs once for each.
    ("vf_effect_gather.hlsl", "kPsEffectGatherLight", "frag", {"EFFECT_OUTPUT": 0}),
    ("vf_effect_gather.hlsl", "kPsEffectGatherPlace", "frag", {"EFFECT_OUTPUT": 1}),
    ("vf_effect_reduce.hlsl", "kPsEffectReduce", "frag", {}),
]

CONSTANT_BINDING = 16
GRID_REGISTER = 255

REGISTER_CONSTANT = re.compile(
    r"^[ \t]*((?:row_major[ \t]+)?(?:float|float2|float3|float4|float4x4|float3x3|int|int4|bool))[ \t]+(\w+)"
    r"(\[[^\]]+\])?[ \t]*:[ \t]*register\(c(\d+)\)[ \t]*;[ \t]*$",
    re.M)
REGISTER_SAMPLER = re.compile(r"\b(sampler2D|sampler3D|samplerCUBE)([ \t]+\w+[ \t]*):[ \t]*register\(s(\d+)\)")
VPOS_MAIN = re.compile(r"\b(\w+)[ \t]+main[ \t]*\([ \t]*float2[ \t]+(\w+)[ \t]*:[ \t]*VPOS[ \t]*\)([^{]*)\{")
GRID_MAIN = re.compile(r"\b(\w+)[ \t]+main[ \t]*\([ \t]*float2[ \t]+(\w+)[ \t]*:[ \t]*POSITION[ \t]*\)[ \t\r\n]*\{")
INTEGER_CONSTANT = re.compile(r"^[ \t]*static[ \t]+const[ \t]+(?:int|uint)[ \t]+\w+[ \t]*=[^;]*;[ \t]*$", re.M)
TEXEL_POSITION = "float4(texel.x * 2 / size - 1, 1 - texel.y * 2 / size, 0.5, 1)"
TEXEL_POSITION_CENTRED = "float4((texel.x + 0.5) * 2 / size - 1, 1 - (texel.y + 0.5) * 2 / size, 0.5, 1)"


def find_glslang():
    if len(sys.argv) > 1:
        return sys.argv[1]
    if os.environ.get("GLSLANG"):
        return os.environ["GLSLANG"]
    for name in ("glslang", "glslangValidator"):
        found = shutil.which(name)
        if found:
            return found
    sys.exit("glslang not found: pass its path, set GLSLANG, or put it on PATH")


def expand(path, seen):
    name = os.path.basename(path)
    if name in seen:
        return ""
    seen.add(name)
    out = []
    for line in open(path, encoding="utf-8").read().splitlines():
        m = re.match(r'\s*#include\s+"([^"]+)"', line)
        out.append(expand(os.path.join(os.path.dirname(path), m.group(1)), seen) if m else line)
    return "\n".join(out)


def translate(source, stage, defines):
    text = expand(os.path.join(UPSTREAM, source), set())

    members = []

    def take(m):
        kind, name, array, register = m.group(1), m.group(2), m.group(3) or "", m.group(4)
        members.append(f"    {kind} {name}{array} : packoffset(c{register});")
        return ""

    if "EFFECT_OUTPUT" in defines:
        # One of the gather's two outputs: the entry point becomes a function, and a new one returns half.
        text = re.sub(r"[ 	]*:[ 	]*COLOR[01][ 	]*;", ";", text)
        text, count = re.subn(r"EffectSums main\(float2 texel : VPOS\)", "EffectSums lvGather(float2 texel)", text)
        if count != 1:
            sys.exit(f"{source}: the gather's entry point changed upstream")
        text += "\nfloat4 main(float4 lvPosition : SV_Position) : SV_Target0\n{\n"
        text += "    EffectSums sums = lvGather(lvPosition.xy - 0.5);\n"
        text += "    return EFFECT_OUTPUT ? sums.place : sums.light;\n}\n"

    if "WAVE_OUTPUT" in defines:
        text = re.sub(r"[ 	]*:[ 	]*COLOR[0-2][ 	]*;", ";", text)
        text, count = re.subn(r"LayerMoments main\(float2 vpos : VPOS\)", "LayerMoments lvLayers(float2 vpos)", text)
        if count != 1:
            sys.exit(f"{source}: the wave pass's entry point changed upstream")
        text += "\nfloat4 main(float4 lvPosition : SV_Position) : SV_Target0\n{\n"
        text += "    LayerMoments moments = lvLayers(lvPosition.xy - 0.5);\n"
        text += "    return WAVE_OUTPUT == 0 ? moments.first : (WAVE_OUTPUT == 1 ? moments.second : moments.third);\n}\n"

    text = REGISTER_CONSTANT.sub(take, text)
    text = REGISTER_SAMPLER.sub(lambda m: f"{m.group(1)}{m.group(2)}: register(s{m.group(3)}, space0)", text)

    if stage == "vert":
        def grid(m):
            result, position = m.group(1), m.group(2)
            return (f"{result} main(uint lvVertex : SV_VertexID)\n{{\n"
                    f"    float2 {position} = float2(lvVertex % (uint)lvGrid.x, lvVertex / (uint)lvGrid.x);")

        text, count = GRID_MAIN.subn(grid, text)
        if count != 1:
            sys.exit(f"{source}: expected one main(float2 ... : POSITION), found {count}")
        members.append(f"    float4 lvGrid : packoffset(c{GRID_REGISTER});")
        # D3D9's output semantics: the clip position and the point size are Vulkan built-ins, so the point's own
        # values (TEXCOORD0) start at location 0, where the pixel shader reads them.
        text = re.sub(r"(\w+[ \t]+\w+[ \t]*):[ \t]*POSITION[ \t]*;", r"\1: SV_Position;", text)
        text = re.sub(r"(\w+[ \t]+\w+[ \t]*:[ \t]*PSIZE[ \t]*;)", r'[[vk::builtin("PointSize")]] \1', text)
        if TEXEL_POSITION in text:
            text = text.replace(TEXEL_POSITION, TEXEL_POSITION_CENTRED)
        elif "MemoryTexelPosition" in text:
            sys.exit(f"{source}: MemoryTexelPosition changed upstream; recheck its half-texel offset")
    else:
        def entry(m):
            result, pixel, rest = m.group(1), m.group(2), m.group(3)
            return (f"{result} main(float4 lvPosition : SV_Position){rest}{{\n"
                    f"    float2 {pixel} = lvPosition.xy - 0.5;")

        text = VPOS_MAIN.sub(entry, text)
        text = re.sub(r"\bCOLOR0\b", "SV_Target0", text)

    # The cbuffer goes first, so the integer constants its array sizes use move up with it, in their order.
    integers = []

    def hoist(m):
        integers.append(m.group(0).strip())
        return ""

    text = INTEGER_CONSTANT.sub(hoist, text)

    head = [f"#define {k} {v}" for k, v in defines.items()] + integers
    if members:
        head.append(f"cbuffer LvConstants : register(b{CONSTANT_BINDING}, space0)")
        head.append("{")
        head.extend(members)
        head.append("};")
    return "\n".join(head) + "\n" + text


def compile_to_words(glslang, args, source_path):
    fd, spv = tempfile.mkstemp(suffix=".spv")
    os.close(fd)
    try:
        subprocess.check_call([glslang] + args + [source_path, "-o", spv])
        with open(spv, "rb") as f:
            data = f.read()
    finally:
        os.remove(spv)
    return [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]


def main():
    glslang = find_glslang()
    modules = []
    for source, symbol in GLSL:
        words = compile_to_words(glslang, ["-V", "-Os", "--target-env", "vulkan1.0", "-I" + HERE],
                                 os.path.join(HERE, source))
        modules.append((source, symbol, words))
    work = tempfile.mkdtemp()
    for source, symbol, stage, defines in HLSL:
        translated = os.path.join(work, symbol + ".hlsl")
        with open(translated, "w", encoding="utf-8", newline="\n") as f:
            f.write(translate(source, stage, defines))
        args = ["-D", "-V", "-Os", "-S", stage, "-e", "main", "--target-env", "vulkan1.0", "--hlsl-dx9-compatible",
                "--hlsl-iomap"]
        if stage == "vert":
            args.append("--invert-y")
        try:
            words = compile_to_words(glslang, args, translated)
        except subprocess.CalledProcessError:
            sys.exit(f"{source} ({symbol}) failed; the translated source is kept at {translated}")
        modules.append((source, symbol, words))
    shutil.rmtree(work)

    write_inc("DebugSpirv.inc", "the debug views (*.frag here)", modules[:len(GLSL)])
    write_inc("Spirv.inc", "Lumenveil's HLSL (Core/shaders)", modules[len(GLSL):])


def write_inc(name, sources, modules):
    lines = [
        f"// Generated by Shaders/Compile.py from {sources}: SPIR-V 1.0 for Vulkan 1.0, as uint32_t words.",
        "// Do not edit; edit the source and run the script.",
        "",
    ]
    for source, symbol, words in modules:
        lines.append(f"// {source}")
        lines.append(f"static const uint32_t {symbol}[] = {{")
        for i in range(0, len(words), 8):
            lines.append("    " + ", ".join(f"0x{w:08x}" for w in words[i:i + 8]) + ",")
        lines.append("};")
        lines.append("")
    with open(os.path.join(HERE, name), "w", newline="\n") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
