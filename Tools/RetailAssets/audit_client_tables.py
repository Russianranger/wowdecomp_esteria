"""Inspect string offsets in all existing client table providers without modifying them."""
import importlib.util
import json
import struct
import sys
from pathlib import Path

from native_import import TABLES, wdbc


def main():
    manifest = json.loads(Path(sys.argv[1]).read_text())
    spec = importlib.util.spec_from_file_location("audit_storm", Path(sys.argv[2]) / "tools/cars_mount_pack.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    storm = module.Storm(module.DLL_DEFAULT)
    for provider in manifest["baselines"]["client_providers"]:
        if provider.get("table") != "ItemDisplayInfo" or "archive" not in provider:
            continue
        handle = storm.open_archive(Path(provider["archive"]))
        try:
            data = storm.read(handle, "DBFilesClient\\ItemDisplayInfo.dbc")
        finally:
            storm.dll.SFileCloseArchive(handle)
        rows, strings = wdbc(data, 25)
        invalid = [(row[0], field, row[field]) for row in rows for field in TABLES["ItemDisplayInfo"][1]
                   if row[field] >= len(strings) or strings.find(b"\0", row[field]) < 0]
        print(Path(provider["archive"]).name, "rows", len(rows), "invalid offsets", len(invalid), "first", invalid[:2])


if __name__ == "__main__":
    main()
