"""Regression checks for stock-compatible and expanded descriptor generation."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from gen_descriptors import build_tables, stock_descriptors, stock_initializer, write_tables


def stock_values(path, symbol):
    return [int(value, 0) for value in re.findall(
        r"0x[0-9A-Fa-f]+|\b\d+\b", stock_initializer(path, symbol))]


class ExtendedLimitsTests(unittest.TestCase):
    def test_stock_generation_matches_retained_tables(self):
        descriptors, shared, local, container = build_tables(25, 36)
        self.assertEqual(descriptors, stock_descriptors())
        for generated, path, symbol, count in (
            (shared, "WoW/Source/Object/Player.cpp", "CGPlayer::s_mirrorFields", 173),
            (local, "WoW/Source/Object/Player.cpp", "CGPlayer::s_localMirrorFields", 870),
            (container, "WoW/Source/Object/Container.cpp", "CGContainer::s_mirrorFields", 72),
        ):
            with self.subTest(symbol=symbol):
                self.assertEqual(len(generated), count)
                self.assertEqual(generated, stock_values(path, symbol))

    def test_extended_descriptors_cover_every_player_dword_once(self):
        descriptors, shared, local, container = build_tables(1000, 254)
        self.assertEqual(len(descriptors), 4115)
        fields = [index for _, offset, count, _, _ in descriptors
                  for index in range(offset, offset + count)]
        self.assertEqual(fields, list(range(6053)))
        self.assertEqual(descriptors[-1][1], 6052)
        self.assertEqual(len(shared), 5048)
        self.assertEqual(shared, list(range(2, 5050)))
        self.assertEqual(local, [index + 4875 for index in stock_values(
            "WoW/Source/Object/Player.cpp", "CGPlayer::s_localMirrorFields")])
        self.assertEqual(len(local), 870)
        self.assertEqual(container, list(range(2, 510)))

    def test_slot_boundaries_and_original_names(self):
        descriptors = {name: (offset, count, kind, flags)
                       for name, offset, count, kind, flags in build_tables(1000, 254)[0]}
        for slot in (0, 24, 25, 254, 255, 256, 999):
            for suffix, relative, count, kind, flags in (
                (1, 0, 1, 1, 0x40), (2, 1, 1, 1, 2),
                (3, 2, 2, 2, 2), (4 if slot == 0 else 5, 4, 1, 1, 2),
            ):
                name = f"PLAYER_QUEST_LOG_{slot + 1}_{suffix}"
                with self.subTest(name=name):
                    self.assertEqual(descriptors[name], (10 + 5 * slot + relative, count, kind, flags))

    def test_nonquest_descriptors_shift_without_other_changes(self):
        stock = stock_descriptors()
        extended = build_tables(1000, 254)[0]
        self.assertEqual(extended[:9], stock[:9])
        self.assertEqual(extended[4009:], [
            (name, offset + 4875, count, kind, flags)
            for name, offset, count, kind, flags in stock[109:]])

    def test_reject_unsupported_capacities(self):
        for quests, bags in ((24, 36), (1001, 36), (25, 35), (25, 255), (-1, 36), (25, -1)):
            with self.subTest(quests=quests, bags=bags), self.assertRaises(ValueError):
                build_tables(quests, bags)

    @unittest.skipUnless(shutil.which("cmake"), "CMake is not installed")
    def test_cmake_rejects_invalid_configuration(self):
        root = Path(__file__).resolve().parents[1]
        for settings, expected in (
            ({"QUEST_LOG_SLOTS": "1001"}, "must be between 25 and 1000"),
            ({"CONTAINER_SLOTS": "255"}, "must be between 36 and 254"),
            ({"QUEST_LOG_SLOTS": "1000"}, "Non-stock capacities require"),
            ({"EXTENDED_LIMITS": "ON", "QUEST_LOG_SLOTS": "1000", "MAX_UPDATE_MASKS": "193"},
             "WOWDECOMP_MAX_UPDATE_MASKS must be 194..255"),
            ({"QUEST_LOG_SLOTS": "25.5"}, "must be a positive integer"),
            ({"CONTAINER_SLOTS": "many"}, "must be a positive integer"),
            ({"MAX_UPDATE_MASKS": "many"}, "WOWDECOMP_MAX_UPDATE_MASKS must be 42..255"),
            ({"MAX_CLASSES": "31"}, "WOWDECOMP_MAX_CLASSES must be between 32 and 254"),
            ({"MAX_CLASSES": "255"}, "WOWDECOMP_MAX_CLASSES must be between 32 and 254"),
            ({"MAX_CLASSES": "254"}, "Non-stock class masks require"),
            ({"MAX_CLASSES": "many"}, "WOWDECOMP_MAX_CLASSES must be a positive integer"),
        ):
            with self.subTest(settings=settings), tempfile.TemporaryDirectory() as directory:
                result = subprocess.run(
                    [shutil.which("cmake"), *[
                        f"-DWOWDECOMP_{name}={value}" for name, value in settings.items()
                    ], "-P", str(root / "cmake/ExtendedLimits.cmake")],
                    cwd=directory, capture_output=True, text=True, timeout=30,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected, result.stdout + result.stderr)

    def test_export_manifest_and_generated_files(self):
        for quests, bags in ((25, 36), (255, 254), (256, 254), (1000, 254)):
            with self.subTest(quests=quests), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / "generated"
                write_tables(output, quests, bags)
                manifest = json.loads((output / "limits.json").read_text(encoding="utf-8"))
                delta = 5 * (quests - 25)
                self.assertEqual(manifest["quest_slots"], quests)
                self.assertEqual(manifest["container_slots"], bags)
                self.assertEqual(manifest["max_class_id"], 32)
                self.assertEqual(manifest["class_mask_words"], 1)
                self.assertEqual(manifest["active_player_dwords"], 1326 + delta)
                self.assertEqual(manifest["remote_player_dwords"], 324 + delta)
                self.assertEqual(manifest["required_mask_dwords"], (1326 + delta + 31) // 32)
                self.assertEqual(manifest["container_dwords"], 66 + 2 * bags)
                self.assertEqual(manifest["quest_remove_slot_bytes"], 1 if quests <= 255 else 2)
                self.assertEqual(manifest["quest_remove_opcode"], "CMSG_QUEST_LOG_REMOVE_QUEST")
                self.assertEqual(manifest["player_field_base_dword"], 148)
                self.assertEqual(manifest["player_fields"], [
                    dict(name=name, offset=148 + offset, count=count, type=kind, flags=flags)
                    for name, offset, count, kind, flags in build_tables(quests, bags)[0]])
                for filename in ("PlayerDescriptors.inc", "PlayerMirrorFields.inc",
                                 "PlayerLocalMirrorFields.inc", "ContainerMirrorFields.inc"):
                    content = (output / filename).read_text(encoding="utf-8")
                    self.assertTrue(content.startswith("// Generated by Tools/gen_descriptors.py;"))
                    self.assertTrue(content.endswith("\n"))
                    self.assertNotIn("\r", content)
                if quests == 1000:
                    self.assertEqual(manifest["active_player_dwords"], 6201)
                    self.assertEqual(manifest["remote_player_dwords"], 5199)
                    self.assertEqual(manifest["required_mask_dwords"], 194)

    def test_extended_class_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            write_tables(output, 1000, 254, max_classes=254)
            manifest = json.loads((output / "limits.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["max_class_id"], 254)
            self.assertEqual(manifest["class_mask_words"], 8)
            self.assertEqual(manifest["class_masks"], {
                "byte_order": "little-endian", "word_order": "least-significant first",
                "item_skill_talent_faction_bit": "classId - 1",
                "who_realm_tracking_bit": "classId",
            })
            for invalid in (31, 255):
                with self.subTest(classes=invalid), self.assertRaises(ValueError):
                    write_tables(output, 25, 36, max_classes=invalid)


if __name__ == "__main__":
    unittest.main()
