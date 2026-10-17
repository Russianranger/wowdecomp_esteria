import copy
import json
import sqlite3
import tempfile
import unittest
from pathlib import Path

import native_import as native


class NativeImportTests(unittest.TestCase):
    def test_allocation_preserves_ids_and_avoids_every_namespace(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ids.sqlite"
            first = native.allocate(path, "build:one", {1000000, 1000001}, {160000})
            self.assertEqual(first, (1000002, 160001))
            self.assertEqual(native.allocate(path, "build:one", {1000002}, {160001}), first)
            self.assertEqual(native.allocate(path, "build:two", set(), set()), (1000000, 160000))
            third = native.allocate(path, "build:three", set(), set())
            self.assertNotIn(third[0], {1000000, 1000002})
            self.assertNotIn(third[1], {160000, 160001})

    def test_pack_table_composition_preserves_both_string_pools(self):
        with tempfile.TemporaryDirectory() as directory:
            packs = []
            for number, name in ((10, "first"), (20, "second")):
                root = Path(directory) / name
                (root / "tables").mkdir(parents=True)
                row = [number] + [0] * 24
                (root / "tables/itemdisplayinfo.dbc").write_bytes(native.make_wdbc(25, [row], {(0, 1): name}))
                packs.append(root)
            combined = native.combine_tables(packs, "ItemDisplayInfo")
            rows, strings = native.wdbc(combined, 25)
            self.assertEqual([row[0] for row in rows], [10, 20])
            self.assertEqual([strings[row[1]:strings.index(0, row[1])].decode() for row in rows], ["first", "second"])
            with self.assertRaises(ValueError):
                native.combine_tables([packs[0], packs[0]], "ItemDisplayInfo")

    def test_original_payload_change_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "originals").mkdir()
            original = path / "originals/42.bin"
            original.write_bytes(b"original-CASC-data")
            source = {"build": "0" * 32, "product": "wow"}
            target = {"item_id": 1000000, "display_id": 160000, "records": {
                "item_template": {"entry": 1000000}, "item_dbc": {"ID": 1000000}, "itemdisplayinfo_dbc": {"ID": 160000}}}
            manifest = {"schema": 1, "slug": "test", "source_key": "source", "source": source, "target": target,
                        "definition_sha256": native.digest(json.dumps({"source": source, "target": target}, sort_keys=True).encode()),
                        "files": {"originals/42.bin": {"sha256": native.digest(original.read_bytes()), "size": original.stat().st_size}}}
            native.save_json(path / "manifest.json", manifest)
            (path / "server.sql").write_text(native.server_sql(manifest))
            self.assertEqual(native.load_pack(path)["slug"], "test")
            original.write_bytes(b"converted-data")
            with self.assertRaises(ValueError):
                native.load_pack(path)


if __name__ == "__main__":
    unittest.main()
