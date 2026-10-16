"""Convert existing 12340 WDBC class masks to the opt-in 256-bit format.

No rows/content are added. Output must be a different file; source files are never
overwritten. UINT32_MAX expands to all words for legacy unrestricted masks.
"""
import argparse
from pathlib import Path
import struct

# Stock field counts and zero-based class-mask columns (before expansion).
TABLES = {
    "TalentTab": (24, (20,)),
    "SkillLineAbility": (14, (4, 6)),
    "SkillRaceClassInfo": (8, (3,)),
    "Faction": (57, (6, 7, 8, 9)),
}


def convert_dbc(data, table):
    columns, masks = TABLES[table]
    if len(data) < 20:
        raise ValueError("Truncated WDBC header")
    magic, count, fields, row_size, strings_size = struct.unpack_from("<4s4I", data)
    if magic != b"WDBC":
        raise ValueError("Only 12340 WDBC files are supported")
    if fields != columns or row_size != 4 * columns:
        raise ValueError(f"{table} requires stock {columns}-column/{4 * columns}-byte records; input may already be extended")
    if len(data) != 20 + count * row_size + strings_size:
        raise ValueError("WDBC length does not match its header")
    expanded_fields = columns + 7 * len(masks)
    result = bytearray(struct.pack("<4s4I", magic, count, expanded_fields, 4 * expanded_fields, strings_size))
    for index in range(count):
        record = struct.unpack_from(f"<{columns}I", data, 20 + index * row_size)
        expanded = []
        for column, value in enumerate(record):
            expanded.append(value)
            if column in masks:
                expanded.extend([0xFFFFFFFF if value == 0xFFFFFFFF else 0] * 7)
        result.extend(struct.pack(f"<{expanded_fields}I", *expanded))
    # String offsets are relative to this unchanged block, not the file start.
    result.extend(data[20 + count * row_size:])
    return bytes(result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--table", choices=TABLES, help="Defaults to source filename without .dbc")
    args = parser.parse_args()
    table = args.table or args.source.stem
    if table not in TABLES:
        parser.error("Specify --table for a nonstandard source filename")
    if args.source.resolve() == args.output.resolve():
        parser.error("Source and output must be different files")
    converted = convert_dbc(args.source.read_bytes(), table)
    # Refuse to overwrite any existing output, including another input table.
    with args.output.open("xb") as output:
        output.write(converted)
