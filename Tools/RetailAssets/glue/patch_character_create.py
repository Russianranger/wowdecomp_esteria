"""Adds the Retail race option panel to the client's GlueXML CharacterCreate.lua.

The creator already draws up to 29 option rows from CycleCharCustomization("EA_GET", i). This patch makes any
race mapped to Retail data (native "EA_RETAIL" command) use that panel, shows the real choice names and
routes the randomizer to the native EA_RANDOM, and binds viewport wheel input to
the native face zoom. It is idempotent.

Default is a dry run that writes the patched files to --stage. With --apply it backs up and rewrites
Data\\patch-Z.MPQ and Data\\enUS\\patch-enUS-Z.MPQ. Close the game first.

  python patch_character_create.py --client "G:\\3.3.5a - Dev"            # dry run
  python patch_character_create.py --client "G:\\3.3.5a - Dev" --apply
"""

import argparse
import ctypes
import hashlib
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

MARKER = "-- Retail race customization (wowdecomp)"
ENTRY = "Interface" + chr(92) + "GlueXML" + chr(92) + "CharacterCreate.lua"
ARCHIVES = (Path("Data") / "patch-Z.MPQ", Path("Data") / "enUS" / "patch-enUS-Z.MPQ")


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit("Expected exactly one match for: " + old[:80])
    return text.replace(old, new)


def patch_base(text):
    if MARKER in text:
        return text
    nl = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    text = replace_once(text, "local function EA_ACTIVE()\n    return CharacterCreate.selectedRaceID == 20 or ",
        MARKER + "\n"
        "local function EA_IS_RETAIL()\n"
        "    return type(CycleCharCustomization) == \"function\" and CycleCharCustomization(\"EA_RETAIL\", 1) == 1;\n"
        "end\n"
        "local function EA_ACTIVE()\n    return EA_IS_RETAIL() or CharacterCreate.selectedRaceID == 20 or ")
    text = replace_once(text, "((CharacterCreate.selectedRaceID == 20 or CharacterCreate.selectedRaceID == 46",
        "((EA_IS_RETAIL() or CharacterCreate.selectedRaceID == 20 or CharacterCreate.selectedRaceID == 46")
    text = replace_once(text, "local function ChoiceText(id, value, label)\n",
        "local function ChoiceText(id, value, label)\n"
        "    if EA_IS_RETAIL() then\n"
        "        local retailName = CycleCharCustomization(\"EA_CHOICE_NAME\", id, value);\n"
        "        if retailName and retailName ~= \"\" then return retailName; end\n"
        "    end\n")
    text = replace_once(text, "    CharacterCustomization_CloseChoices();\n    randomize();\n",
        "    CharacterCustomization_CloseChoices();\n"
        "    if EA_IS_RETAIL() then\n"
        "        PlaySound(\"gsCharacterCreationLook\");\n"
        "        CycleCharCustomization(\"EA_RANDOM\", 1);\n"
        "        CharacterCreate_UpdateHairCustomization();\n"
        "        return;\n"
        "    end\n"
        "    randomize();\n")
    return text.replace("\n", nl)


def patch_capacity(text):
    text = patch_base(text)
    marker = "-- Retail option capacity v2 (45-option Dracthyr)"
    if marker in text:
        return text
    nl = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    # Keep stock/legacy control behavior; allocate enough frames and enumerate the actual retail count.
    start = text.index(MARKER)
    prefix, panel = text[:start], text[start:]
    panel = replace_once(panel, "local function EA_REFRESH()\n", marker + "\n"
        "local function EA_LIMIT()\n"
        "    if EA_IS_RETAIL() then return math.min(64, CycleCharCustomization(\"EA_COUNT\", 1) or 29); end\n"
        "    return 29;\nend\n"
        "local function EA_REFRESH()\n")
    panel = panel.replace("for i=6,29 do", "for i=6,64 do")
    panel = panel.replace("for i=1,29 do", "for i=1,64 do")
    panel = panel.replace("for i = 1, 29 do", "for i = 1, EA_LIMIT() do")
    panel = panel.replace("EA_ACTIVE() and 29 or 5", "EA_ACTIVE() and EA_LIMIT() or 5")
    panel = panel.replace("local rowSpacing=optionCount>22 and 28 or 36;",
        "local rowSpacing=math.min(optionCount>22 and 28 or 36, math.max(18, math.floor((GetScreenHeight()-210)/rowsPerColumn))); ")
    # EA_GET returns nil beyond the model's option count, hiding those frames after a race switch.
    return (prefix + panel).replace("\n", nl)


def patch(text):
    text = patch_capacity(text)
    marker = "-- Creator mousewheel face zoom (wowdecomp)"
    if marker in text:
        return text
    nl = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    text = replace_once(text, "function CharacterCreate_OnLoad(self)\n",
        marker + "\nfunction CharacterCreate_OnLoad(self)\n"
        "    -- The viewport owns zoom; choice pickers keep their own wheel scrolling.\n"
        "    CharacterCreateFrame:EnableMouseWheel(true);\n"
        "    CharacterCreateFrame:SetScript(\"OnMouseWheel\", function(_, delta)\n"
        "        CycleCharCustomization(\"EA_ZOOM\", delta);\n"
        "    end);\n")
    return text.replace("\n", nl)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--tools", type=Path, default=Path(r"R:\Users\Zach\Documents\GitHub\EsteriaWoW\tools"),
                        help="EsteriaWoW tools folder (provides the StormLib wrapper)")
    parser.add_argument("--stage", type=Path, default=Path(__file__).with_name("staged"))
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    sys.path.insert(0, str(args.tools))
    from cars_mount_pack import DLL_DEFAULT, H, Storm  # noqa: E402

    storm = Storm(DLL_DEFAULT)
    args.stage.mkdir(parents=True, exist_ok=True)
    patched = {}
    for relative in ARCHIVES:
        path = args.client / relative
        handle = H()
        if not storm.dll.SFileOpenArchive(str(path), 0, 0x100, ctypes.byref(handle)):
            raise SystemExit("Cannot open " + str(path))
        try:
            original = storm.read(handle, ENTRY)
        finally:
            storm.dll.SFileCloseArchive(handle)
        result = patch(original.decode("utf-8")).encode("utf-8")
        (args.stage / (path.stem + "-CharacterCreate.lua")).write_bytes(result)
        patched[path] = (original, result)
        print(path.name, "already patched" if result == original else "patched: %d -> %d bytes" % (len(original), len(result)))

    if not args.apply:
        print("Dry run. Staged files are in", args.stage)
        return
    running = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
    if "wowclient" in running or "wow.exe" in running or "wow64.exe" in running or "eclipse" in running:
        raise SystemExit("Close the game first; the archives are in use.")
    backup = args.client / "_backups" / ("retail-glue-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
    backup.mkdir(parents=True)
    for path, (original, result) in patched.items():
        if result == original:
            continue
        shutil.copy2(path, backup / path.name)
        print("backup", backup / path.name, hashlib.sha256((backup / path.name).read_bytes()).hexdigest()[:16])
        storm.replace_archive_entries(path, {ENTRY: result})
        handle = H()
        storm.dll.SFileOpenArchive(str(path), 0, 0x100, ctypes.byref(handle))
        try:
            ok = storm.read(handle, ENTRY) == result
        finally:
            storm.dll.SFileCloseArchive(handle)
        print(path.name, "verified" if ok else "VERIFY FAILED")
        if not ok:
            raise SystemExit("Readback mismatch; restore from " + str(backup))


if __name__ == "__main__":
    main()
