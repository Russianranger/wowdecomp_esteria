import contextlib
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import worker as w


def chunk(tag, data):
    return tag + struct.pack("<I", len(data)) + data


def model(action=1628):
    data = bytearray(0x140 + 64)
    data[:4] = b"MD20"
    struct.pack_into("<II", data, 0x1C, 1, 0x140)
    struct.pack_into("<H", data, 0x140, action)
    return chunk(b"MD21", data)


class WorkerTests(unittest.TestCase):
    def test_dependency_graph_keeps_all_variations_and_parent(self):
        data = model() + chunk(b"SFID", struct.pack("<II", 12, 13))
        data += chunk(b"TXID", struct.pack("<II", 14, 0)) + chunk(b"SKID", struct.pack("<I", 15))
        data += chunk(b"AFID", struct.pack("<HHIHHI", 1524, 0, 16, 1524, 1, 17))
        refs, actions = w.dependencies(data, "m2")
        self.assertEqual(set(refs), {(12, "skin"), (13, "skin"), (14, "blp"),
                                     (15, "skel"), (16, "anim"), (17, "anim")})
        self.assertEqual(actions, {1524, 1628})
        refs, _ = w.dependencies(chunk(b"SKPD", struct.pack("<III", 0, 0, 20)), "skel")
        self.assertEqual(refs, [(20, "skel")])

    def test_reject_truncated_chunks_and_external_spans(self):
        for data in (b"AFID", chunk(b"AFID", b"bad"), b"AFID" + struct.pack("<I", 100) + b"x",
                     chunk(b"SFID", b"x"), model() + model()):
            with self.subTest(data=data[:12]), self.assertRaises(ValueError):
                w.dependencies(data, "m2")
        data = bytearray(64)
        data[:4] = b"MD20"
        struct.pack_into("<II", data, 0x1C, 8, 32)
        with self.assertRaises(ValueError):
            w.dependencies(bytes(data), "m2")

    def test_matching_uses_spell_and_exact_variant_never_name(self):
        rows = [{"spell": 458, "display": 20, "protected": False},
                {"spell": 459, "display": 99, "protected": False},
                {"spell": 999, "display": 20, "protected": True}]
        catalog = [{"mount": 1, "spell": 458, "display": 19}, {"mount": 1, "spell": 458, "display": 20},
                   {"mount": 2, "spell": 459, "display": 21}, {"mount": 2, "spell": 459, "display": 22}]
        w.match_mounts(rows, catalog)
        self.assertEqual(rows[0]["source"]["display"], 20)
        self.assertEqual(rows[1]["status"], "ambiguous")
        self.assertFalse(rows[1]["selected"])
        self.assertNotIn("selected", rows[2])

    def test_custom_spells_and_native_models_protected(self):
        spell = [0] * 234
        spell[0], spell[95], spell[110], spell[136] = 458, 78, 20, 1
        display, model_row = [0] * 16, [0] * 28
        display[0], display[1] = 20, 30
        model_row[0], model_row[2] = 30, 1
        rows = w.resolve_mounts({458: spell}, b"\0Horse\0", {20: display}, b"\0",
                                {30: model_row}, b"\0Creature\\Horse\\Horse.m2\0", {458}, {"20": [20]})
        self.assertFalse(rows[0]["protected"])
        rows = w.resolve_mounts({458: spell}, b"\0Horse\0", {20: display}, b"\0",
                                {30: model_row}, b"\0retail\\100.m2\0", {458}, {"20": [20]})
        self.assertTrue(rows[0]["protected"])

    def test_archive_order_includes_multichar_suffixes_and_full_paths(self):
        with tempfile.TemporaryDirectory() as root:
            client = Path(root)
            (client / "Data/enUS").mkdir(parents=True)
            for name in ("common.MPQ", "patch.MPQ", "patch-Z.MPQ", "Patch-ZzzCustom.MPQ", "patch-A.MPQ"):
                (client / "Data" / name).touch()
            (client / "Data/enUS/patch-enUS-Z.MPQ").touch()
            ordered = [p.name for p in w.archive_order(client, "enUS")]
            self.assertEqual(ordered, ["common.MPQ", "patch.MPQ", "patch-enUS-Z.MPQ",
                                       "patch-A.MPQ", "patch-Z.MPQ", "Patch-ZzzCustom.MPQ"])

    def test_wdbc_rejects_duplicates_and_invalid_strings(self):
        blob = struct.pack("<4s4I4I", b"WDBC", 2, 2, 8, 1, 1, 0, 1, 0) + b"\0"
        with self.assertRaises(ValueError):
            w.wdbc(blob, 2)
        with self.assertRaises(ValueError):
            w.text(b"\0", 1)

    def test_resume_hashes_originals_and_refetches_corruption(self):
        with tempfile.TemporaryDirectory() as root:
            studio = w.Studio({"settings": {"work": str(Path(root) / "work"),
                                             "client": str(Path(root) / "client")}})
            calls = []
            def fetch(config, name, destination):
                calls.append(name)
                destination.write_bytes(b"source bytes")
            with patch.object(studio, "probe", side_effect=fetch), contextlib.redirect_stdout(io.StringIO()):
                path = studio.fetch("config", 42, "blp")
                self.assertEqual(studio.fetch("config", 42, "blp"), path)
                path.write_bytes(b"corrupted")
                studio.fetch("config", 42, "blp")
            self.assertEqual(calls, ["retail/42.blp", "retail/42.blp"])
            self.assertEqual(path.read_bytes(), b"source bytes")

    def test_work_directory_cannot_be_client_or_ancestor(self):
        with tempfile.TemporaryDirectory() as root:
            for work in (root, str(Path(root) / "client"), str(Path(root) / "client/work")):
                with self.subTest(work=work), self.assertRaises(ValueError):
                    w.Studio({"settings": {"work": work, "client": str(Path(root) / "client")}})

    def test_pause_prevents_acquisition(self):
        with tempfile.TemporaryDirectory() as root:
            studio = w.Studio({"settings": {"work": str(Path(root) / "work"),
                                             "client": str(Path(root) / "client")}})
            studio.pause.touch()
            with self.assertRaises(w.Paused):
                studio.fetch("config", 42, "m2")

    def test_changing_pin_rejected_before_assets_reused(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            config = root / "source.ini"
            config.write_text("mode=online\nproduct=wow\nbuild=" + "a" * 32 + "\n")
            studio = w.Studio({"settings": {"work": str(root / "work"), "client": str(root / "client"),
                                             "source_config": str(config), "source_mode": "cdn"}})
            studio.source()
            config.write_text("mode=online\nproduct=wow\nbuild=" + "b" * 32 + "\n")
            with self.assertRaises(ValueError):
                studio.source()

    def test_local_source_detects_parent_build_and_never_enables_cdn(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            retail = root / "World of Warcraft"
            (retail / "Data").mkdir(parents=True)
            (retail / "_retail_").mkdir()
            (retail / ".build.info").write_text(
                "Product!STRING:0|Active!DEC:1|Build Key!HEX:16|Version!STRING:0\n"
                "wow|1|" + "a" * 32 + "|12.1.5.12345\n")
            studio = w.Studio({"settings": {"work": str(root / "work"), "client": str(root / "client"),
                                             "retail_root": str(retail / "_retail_")}})
            config = studio.source().read_text()
            self.assertIn("mode=local", config)
            self.assertIn(retail.as_posix(), config)
            self.assertNotIn("region=", config)
            self.assertNotIn("cdn=", config)
            self.assertEqual(studio.project["source"]["build"], "a" * 32)
            self.assertEqual(studio.project["source_info"]["version"], "12.1.5.12345")
            (retail / ".build.info").write_text(
                "Product!STRING:0|Active!DEC:1|Build Key!HEX:16|Version!STRING:0\n"
                "wow|1|" + "b" * 32 + "|12.1.6.12346\n")
            with self.assertRaisesRegex(ValueError, "another Retail build"):
                studio.source()

    def test_local_default_rejects_forever_and_missing_install_without_network_fallback(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            retail = root / "Retail"
            (retail / "Data").mkdir(parents=True)
            (retail / ".build.info").write_text(
                "Product!STRING:0|Active!DEC:1|Build Key!HEX:16\n"
                "wow_classic_beta|1|" + "a" * 32 + "\n")
            studio = w.Studio({"settings": {"work": str(root / "work"), "client": str(root / "client"),
                                             "retail_root": str(retail)}})
            with patch.object(w.urllib.request, "urlopen") as network:
                with self.assertRaisesRegex(ValueError, "different product"):
                    studio.source()
                studio.settings["retail_root"] = ""
                with self.assertRaisesRegex(ValueError, "Choose your installed Retail folder"):
                    studio.source()
                network.assert_not_called()

    def test_local_and_cdn_can_share_only_the_exact_same_build(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            retail = root / "Retail"
            (retail / "Data").mkdir(parents=True)
            (retail / ".build.info").write_text(
                "Product!STRING:0|Active!DEC:1|Build Key!HEX:16\nwow|1|" + "a" * 32 + "\n")
            ini = root / "RetailCasc.ini"
            ini.write_text("product=wow\nbuild=" + "a" * 32 + "\n")
            studio = w.Studio({"settings": {"work": str(root / "work"), "client": str(root / "client"),
                                             "retail_root": str(retail), "source_config": str(ini)}})
            studio.source()
            studio.settings["source_mode"] = "cdn"
            self.assertIn("mode=online", studio.source().read_text())
            studio.settings["cdn_build"] = "b" * 32
            with self.assertRaisesRegex(ValueError, "another Retail build"):
                studio.source()

    def test_bundled_mount_mappings_work_without_docker(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "mount-models.json"
            path.write_text(json.dumps({"31319": [27567], "284": [2404]}))
            with patch.object(w.subprocess, "run") as process:
                mapping = w.creature_models([31319, 999], {"mapping_mode": "bundled", "mapping_file": str(path)})
            process.assert_not_called()
            self.assertEqual(mapping, {"31319": [27567]})
            path.write_text(json.dumps({"31319": ["not an ID"]}))
            with self.assertRaisesRegex(ValueError, "Invalid mount display"):
                w.creature_models([31319], {"mapping_mode": "file", "mapping_file": str(path)})

    def test_settings_reject_unsafe_output_and_out_of_range_limits(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            with self.assertRaisesRegex(ValueError, "outside the target client"):
                w.Studio({"settings": {"work": str(root / "work"), "client": str(root / "client"),
                                        "export_dir": str(root / "client/Data")}})
            with self.assertRaisesRegex(ValueError, "outside the Retail"):
                w.Studio({"settings": {"work": str(root / "Retail/cache"), "client": str(root / "client"),
                                        "retail_root": str(root / "Retail")}})
            with self.assertRaises(ValueError):
                w.bounded(1900, 1800, 128, 1800, "Maximum MPQ size")
            with self.assertRaisesRegex(ValueError, "whole number"):
                w.bounded(600.5, 600, 30, 3600, "Timeout")
            (root / "Retail/Data").mkdir(parents=True)
            (root / "Retail/.build.info").touch()
            with self.assertRaisesRegex(ValueError, "installation to finish"):
                w.local_source_info(str(root / "Retail"))

    def test_export_rejects_shared_custom_display_and_exclusions(self):
        with tempfile.TemporaryDirectory() as root:
            studio = w.Studio({"settings": {"work": str(Path(root) / "work"),
                                             "client": str(Path(root) / "client")}})
            selected = {"selected": True, "protected": False, "status": "validated", "display": 20,
                        "source": {"model": 42}, "files": []}
            for protected in (True, False):
                studio.project["mounts"] = [selected, {"selected": False, "protected": protected, "display": 20}]
                with patch.object(studio, "source"), self.assertRaisesRegex(ValueError, "shared"):
                    studio.export()

    def test_incomplete_export_explains_ready_count_and_partial_option(self):
        with tempfile.TemporaryDirectory() as root:
            studio = w.Studio({"settings": {"work": str(Path(root) / "work"),
                                             "client": str(Path(root) / "client")}})
            studio.project["mounts"] = [
                {"selected": True, "protected": False, "status": "validated"},
                {"selected": True, "protected": False, "status": "paused"}]
            with self.assertRaisesRegex(ValueError, "1 of 2 selected mounts are ready"):
                studio.export()
            studio.project["mounts"].pop(0)
            with self.assertRaisesRegex(ValueError, "No selected mounts are ready"):
                studio.export(ready_only=True)

    def test_completed_only_export_retains_unfinished_selections_and_records_skips(self):
        with tempfile.TemporaryDirectory() as root:
            studio = w.Studio({"settings": {"work": str(Path(root) / "work"),
                                             "client": str(Path(root) / "client"), "storm": "test.dll"}})
            originals = studio.work / "originals"
            originals.mkdir()
            asset = originals / "42.bin"
            asset.write_bytes(b"original bytes")
            source = {"model": 42, "textures": [0, 0, 0], "scale": 1}
            ready = {"name": "Ready", "display": 20, "source": source, "selected": True,
                     "protected": False, "status": "validated", "files": ["42"]}
            paused = {"name": "Paused", "display": 21, "source": source, "selected": True,
                      "protected": False, "status": "paused", "files": ["999"]}
            studio.project.update({"source": {"product": "wow", "build": "a" * 32, "locale": "2"},
                                   "mounts": [ready, paused], "assets": {"42": {"sha256": w.file_hash(asset)}}})
            packed = []
            def pack(path, entries):
                packed.extend(name for name, _, _ in entries)
                path.write_bytes(b"test archive")
            with (patch.object(studio, "source"), patch.object(studio, "verify_baseline"),
                  patch.object(w, "Storm") as storm, contextlib.redirect_stdout(io.StringIO())):
                storm.return_value.pack.side_effect = pack
                studio.export(ready_only=True)
            receipt = json.loads((Path(studio.project["last_export"]) / "receipt.json").read_text())
            self.assertEqual(len(receipt["mounts"]), 1)
            self.assertEqual(receipt["export_mode"], "completed-only")
            self.assertEqual(receipt["skipped_mounts"], [{"display": 21, "name": "Paused", "status": "paused"}])
            self.assertEqual(len(packed), 2)
            self.assertFalse(any("999" in name for name in packed))
            self.assertTrue(paused["selected"])
            self.assertEqual(paused["status"], "paused")

    def test_selection_changes_invalidate_previous_validation(self):
        with tempfile.TemporaryDirectory() as root:
            studio = w.Studio({"settings": {"work": str(Path(root) / "work"),
                                             "client": str(Path(root) / "client")}})
            a, b = {"mount": 1, "display": 2}, {"mount": 3, "display": 4}
            row = {"key": "458:20", "source": a, "protected": False, "status": "validated", "files": [42]}
            studio.project.update({"mounts": [row], "catalog": [a, b]})
            studio.selections({"edits": [{"key": row["key"], "mount": 3, "display": 4, "selected": True}]})
            self.assertEqual(row["status"], "matched")
            self.assertEqual(row["files"], [])
            self.assertEqual(row["source"], b)

    def test_original_extraction_stays_strict_but_metadata_uses_table_policy(self):
        with tempfile.TemporaryDirectory() as root:
            studio = w.Studio({"settings": {"work": str(Path(root) / "work"), "probe": "probe.exe",
                                             "client": str(Path(root) / "client")}})
            completed = type("Result", (), {"returncode": 0})()
            with patch.object(w.subprocess, "run", return_value=completed) as run:
                studio.probe("config.ini", "retail/42.m2", Path(root) / "model")
                self.assertIn("--extract", run.call_args.args[0])
                self.assertEqual(run.call_args.kwargs["env"]["WOW_RETAIL_BUSY_NOTICE"], "0")
                studio.probe("config.ini", "retail/921760.db2", Path(root) / "table")
                self.assertIn("--extract-table", run.call_args.args[0])

    def test_old_helper_gets_actionable_error_before_network(self):
        with tempfile.TemporaryDirectory() as root:
            studio = w.Studio({"settings": {"work": str(Path(root) / "work"), "probe": "probe.exe",
                                             "client": str(Path(root) / "client")}})
            completed = type("Result", (), {"returncode": 2, "stdout": ""})()
            with (patch.object(w.subprocess, "run", return_value=completed),
                  self.assertRaisesRegex(ValueError, "Rebuild")):
                studio.catalog()

    def test_export_baseline_detects_table_changes(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            (root / "client/Data").mkdir(parents=True)
            studio = w.Studio({"settings": {"work": str(root / "work"), "client": str(root / "client"),
                                             "locale": "enUS", "storm": "test.dll"}})
            table = root / "test.dbc"
            table.write_bytes(b"baseline")
            studio.project["archive_order"] = []
            studio.project["providers"] = [{"path": str(table), "table": "Spell", "sha256": w.file_hash(table)}]
            studio.project["creature_ids"], studio.project["creature_models"] = [1], {"1": [2]}
            with patch.object(w, "Storm"), patch.object(w, "creature_models", return_value={"1": [2]}):
                studio.verify_baseline()
                table.write_bytes(b"changed while downloading")
                with self.assertRaisesRegex(ValueError, "changed since scan"):
                    studio.verify_baseline()

    def test_ground_flying_and_water_classification_uses_spell_auras(self):
        for aura, expected in ((32, "ground"), (201, "flying"), (207, "flying"), (209, "flying"), (58, "water")):
            spell = [0] * 234
            spell[95:98] = [78, aura, 32]
            self.assertEqual(w.mount_type(spell), expected)

    def test_shared_creature_spell_variants_become_one_real_display(self):
        spells = {}
        for spell_id, aura in ((58983, 4), (58997, 32), (58999, 32), (64992, 32), (64993, 32)):
            spell = [0] * 234
            spell[0], spell[95], spell[96], spell[110], spell[136] = spell_id, 78, aura, 31319, 1
            spells[spell_id] = spell
        rows = w.resolve_mounts(spells, b"\0Big Blizzard Bear\0", {}, b"\0", {}, b"\0",
                                set(spells), {"31319": [27567]})
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["display"], 27567)
        self.assertEqual(rows[0]["creatures"], [31319])
        self.assertEqual(len(rows[0]["spells"]), 5)
        self.assertEqual(rows[0]["mount_types"], ["ground"])
        w.match_mounts(rows, [{"mount": 243, "spell": 58999, "display": 27567}])
        self.assertEqual(rows[0]["status"], "matched")
        self.assertFalse(rows[0]["selected"])

    def test_different_appearances_stay_separate_and_shared_custom_spells_protect_group(self):
        spells = {}
        for spell_id, creature in ((458, 1), (459, 2), (900001, 1)):
            spell = [0] * 234
            spell[0], spell[95], spell[110], spell[136] = spell_id, 78, creature, 1
            spells[spell_id] = spell
        rows = w.resolve_mounts(spells, b"\0Same mount name\0", {}, b"\0", {}, b"\0",
                                {458, 459}, {"1": [20], "2": [21]})
        self.assertEqual(len(rows), 2)
        self.assertTrue(next(row for row in rows if row["display"] == 20)["protected"])
        self.assertFalse(next(row for row in rows if row["display"] == 21)["protected"])

    def test_legacy_choices_preserve_one_matched_alias_and_conflicts_require_review(self):
        source = {"mount": 243, "display": 27567}
        row = {"display": 27567, "creatures": [31319], "spells": [{"id": 58983}, {"id": 58997}],
               "protected": False}
        previous = [{"spell": 58983, "display": 31319, "source": source, "selected": True,
                     "status": "validated", "files": [42]},
                    {"spell": 58997, "display": 31319, "source": None, "selected": False}]
        w.restore_choices([row], previous, legacy=True)
        self.assertEqual(row["files"], [42])
        self.assertTrue(row["selected"])
        previous[1].update(source={"mount": 999, "display": 888}, status="matched")
        w.restore_choices([row], previous, legacy=True)
        self.assertEqual(row["status"], "ambiguous")
        self.assertFalse(row["selected"])


@unittest.skipUnless(os.environ.get("STUDIO_STORMLIB"), "Set STUDIO_STORMLIB for actual compressed MPQ roundtrip")
class StormTests(unittest.TestCase):
    def test_real_compressed_archive_readback(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            original = root / "42.bin"
            original.write_bytes(b"unchanged-original" * 5000)
            storm = w.Storm(os.environ["STUDIO_STORMLIB"])
            archive = root / "test.MPQ"
            name = "EsteriaRetail\\wow-test\\2\\42.bin"
            storm.pack(archive, [(name, original, w.file_hash(original))])
            self.assertLess(archive.stat().st_size, original.stat().st_size)
            with storm.archive(archive) as handle:
                self.assertEqual(storm.read(handle, name), original.read_bytes())


if __name__ == "__main__":
    unittest.main()
