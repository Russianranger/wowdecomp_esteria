"""Exercise DBC conversion using synthetic rows; never read installed game data."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from extend_class_masks import convert_dbc


FORMATS = {
    "TalentTab": (24, (20,), 31),
    "SkillLineAbility": (14, (4, 6), 28),
    "SkillRaceClassInfo": (8, (3,), 15),
    "Faction": (57, (6, 7, 8, 9), 85),
}
STRINGS = b"\0alpha\0beta\0unchanged string block\0"


def fixture(table, count=4):
    columns, masks, _ = FORMATS[table]
    records = []
    values = (0, 0xFFFFFFFF, 0x80000000, 1)
    for row in range(count):
        record = [1000 * (row + 1) + field for field in range(columns)]
        for index, column in enumerate(masks):
            record[column] = values[(row + index) % len(values)]
        # Use actual relative string offsets in each localized text block.
        if table == "TalentTab":
            record[1:17] = [1] * 16
            record[23] = 7
        elif table == "Faction":
            record[23:39] = [1] * 16
            record[40:56] = [7] * 16
        records.append(record)
    header = struct.pack("<4s4I", b"WDBC", count, columns, 4 * columns, len(STRINGS))
    body = b"".join(struct.pack(f"<{columns}I", *record) for record in records)
    return header + body + STRINGS, records


class ExtendClassMasksTests(unittest.TestCase):
    def test_all_four_formats_preserve_rows_and_strings(self):
        for table, (columns, mask_columns, expanded_columns) in FORMATS.items():
            with self.subTest(table=table):
                source, records = fixture(table)
                original = source[:]
                output = convert_dbc(source, table)
                magic, count, fields, row_size, strings_size = struct.unpack_from("<4s4I", output)
                self.assertEqual((magic, count, fields, row_size, strings_size),
                                 (b"WDBC", len(records), expanded_columns, 4 * expanded_columns, len(STRINGS)))
                self.assertEqual(len(output) - len(source), len(records) * 28 * len(mask_columns))
                self.assertEqual(output[20 + count * row_size:], STRINGS)
                self.assertEqual(source, original)
                for row, record in enumerate(records):
                    expanded = struct.unpack_from(f"<{fields}I", output, 20 + row * row_size)
                    cursor = 0
                    for column in range(columns):
                        self.assertEqual(expanded[cursor], record[column])
                        cursor += 1
                        if column in mask_columns:
                            self.assertEqual(expanded[cursor:cursor + 7],
                                             (0xFFFFFFFF if record[column] == 0xFFFFFFFF else 0,) * 7)
                            cursor += 7
                    self.assertEqual(cursor, fields)

    def test_empty_tables_preserve_string_blocks(self):
        for table in FORMATS:
            with self.subTest(table=table):
                source, _ = fixture(table, count=0)
                result = convert_dbc(source, table)
                self.assertEqual(result[20:], STRINGS)
                self.assertEqual(struct.unpack_from("<I", result, 4)[0], 0)

    def test_rejects_invalid_headers_and_lengths(self):
        for table in FORMATS:
            source, _ = fixture(table)
            for corrupt in (b"", source[:4], source[:19], b"WDB2" + source[4:],
                            source[:-1], source + b"extra",
                            source[:16] + struct.pack("<I", len(STRINGS) + 1) + source[20:]):
                with self.subTest(table=table, length=len(corrupt)), self.assertRaises(ValueError):
                    convert_dbc(corrupt, table)

    def test_rejects_nonstock_and_already_extended_widths(self):
        for table, (columns, _, _) in FORMATS.items():
            source, _ = fixture(table)
            for fields, row_size in ((columns + 1, columns * 4), (columns, columns * 4 + 4)):
                corrupt = source[:8] + struct.pack("<2I", fields, row_size) + source[16:]
                with self.subTest(table=table, fields=fields, bytes=row_size), self.assertRaises(ValueError):
                    convert_dbc(corrupt, table)
            with self.subTest(table=table, already_extended=True), self.assertRaises(ValueError):
                convert_dbc(convert_dbc(source, table), table)

    def test_cli_refuses_to_overwrite_source_or_existing_output(self):
        script = Path(__file__).with_name("extend_class_masks.py")
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "TalentTab.dbc"
            existing = Path(directory) / "existing.dbc"
            data, _ = fixture("TalentTab")
            source.write_bytes(data)
            existing.write_bytes(b"keep existing file")
            for target in (source, existing):
                with self.subTest(target=target.name):
                    result = subprocess.run([sys.executable, str(script), str(source), str(target)],
                                            capture_output=True, text=True, timeout=30)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(source.read_bytes(), data)
                    self.assertEqual(existing.read_bytes(), b"keep existing file")


if __name__ == "__main__":
    unittest.main()
