"""Stage one explicit Wrath model-name replacement; never edit the input DBC."""
import argparse
import hashlib
import json
from pathlib import Path, PureWindowsPath
import struct

MAX_BYTES = 256 * 1024 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_bounded(path):
    with path.open("rb") as stream:
        data = stream.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ValueError("DBC exceeds the 256 MiB input budget")
    return data


def read_table(path, columns):
    data = read_bounded(path)
    if len(data) < 20:
        raise ValueError("Truncated DBC header")
    magic, count, fields, stride, strings = struct.unpack_from("<4s4I", data)
    if magic != b"WDBC" or fields != columns or stride != columns * 4:
        raise ValueError(f"Expected Wrath WDBC with {columns} columns")
    if count > 1_000_000 or strings > 128 * 1024 * 1024:
        raise ValueError("DBC exceeds record/string budgets")
    string_start = 20 + count * stride
    if len(data) != string_start + strings or not strings or data[string_start] != 0:
        raise ValueError("DBC data/string extent or empty-string entry is invalid")
    ids = set()
    for row in range(count):
        ident = struct.unpack_from("<I", data, 20 + row * stride)[0]
        if ident in ids:
            raise ValueError(f"Duplicate DBC record ID {ident}")
        ids.add(ident)
    return data, count, stride, string_start


def string_at(data, start, offset):
    position = start + offset
    if position >= len(data):
        raise ValueError("DBC model-name offset exceeds string block")
    end = data.find(b"\0", position)
    if end < 0:
        raise ValueError("Unterminated DBC model name")
    return data[position:end].decode("utf-8", errors="strict")


def stage(source, output_directory, model_id, model_path, display_source=None):
    source = source.resolve(strict=True)
    output_directory = output_directory.resolve()
    if output_directory.exists():
        raise ValueError("Output directory already exists; choose a fresh staging directory")
    if not 0 <= model_id <= 0x7fffffff:
        raise ValueError("Model ID exceeds the native signed record-ID range")
    virtual = PureWindowsPath(model_path.replace("/", "\\"))
    if (not model_path or "\0" in model_path or virtual.is_absolute()
            or virtual.drive or virtual.root or ".." in virtual.parts
            or ":" in model_path or virtual.suffix.lower() != ".m3"):
        raise ValueError("Use a relative client model path ending in .m3")
    model_path = str(virtual)
    encoded = model_path.encode("utf-8") + b"\0"
    if len(encoded) > 1024:
        raise ValueError("Model path exceeds the 1023-byte staging budget")
    data, count, stride, start = read_table(source, 28)
    selected = None
    m3_models = []
    for row in range(count):
        base = 20 + row * stride
        ident, _, offset = struct.unpack_from("<3I", data, base)
        name = string_at(data, start, offset)
        if name.lower().endswith(".m3"):
            m3_models.append({"modelId": ident, "path": name})
        if ident == model_id:
            selected = (base, name)
    if selected is None:
        raise ValueError(f"Model ID {model_id} is absent; no record is invented")
    affected = None
    display_identity = None
    if display_source:
        display_source = display_source.resolve(strict=True)
        displays, display_count, display_stride, _ = read_table(display_source, 16)
        affected = []
        for row in range(display_count):
            ident, reference = struct.unpack_from("<2I", displays, 20 + row * display_stride)
            if reference == model_id:
                affected.append(ident)
        display_identity = {"path": str(display_source), "sha256": digest(displays)}
    base, previous_path = selected
    output = bytearray(data)
    new_offset = len(data) - start
    if new_offset + len(encoded) > 128 * 1024 * 1024:
        raise ValueError("Replacement exceeds DBC string budget")
    struct.pack_into("<I", output, base + 8, new_offset)
    struct.pack_into("<I", output, 16, new_offset + len(encoded))
    output.extend(encoded)
    # Compare every pre-existing byte, excluding only the two expected words.
    allowed = set(range(16, 20)) | set(range(base + 8, base + 12))
    changes = [i for i, (old, new) in enumerate(zip(data, output)) if old != new]
    if any(i not in allowed for i in changes):
        raise ValueError("Unexpected DBC record mutation")
    report = {
        "source": {"path": str(source), "sha256": digest(data), "bytes": len(data)},
        "displaySource": display_identity, "modelId": model_id,
        "oldModelPath": previous_path, "newModelPath": model_path,
        "affectedDisplayIds": affected, "existingM3Models": m3_models,
        "recordCount": count, "changedExistingByteOffsets": changes,
        "appendedStringBytes": len(encoded), "outputSHA256": digest(output),
        "scope": "Staged model-name replacement only. Does not install, change display records/server data, resolve MPQs, or prove native rendering. All display records referencing this model ID share the replacement."
    }
    target = output_directory / "DBFilesClient" / "CreatureModelData.dbc"
    if target == source:
        raise ValueError("Input and output DBC must differ")
    output_directory.mkdir(parents=True, exist_ok=False)
    target.parent.mkdir()
    with target.open("xb") as stream:
        stream.write(output)
    if digest(read_bounded(target)) != report["outputSHA256"]:
        raise ValueError("Staged DBC readback mismatch")
    if digest(read_bounded(source)) != report["source"]["sha256"]:
        raise ValueError("Input DBC changed during staging")
    with (output_directory / "staging-report.json").open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dbc", required=True, type=Path)
    parser.add_argument("--display-dbc", type=Path)
    parser.add_argument("--model-id", required=True, type=int)
    parser.add_argument("--model-path", required=True)
    parser.add_argument("--output-directory", required=True, type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(stage(args.source_dbc, args.output_directory, args.model_id,
                               args.model_path, args.display_dbc), indent=2))
    except (ValueError, OSError) as error:
        parser.exit(1, f"DBC staging failed: {error}\n")


if __name__ == "__main__":
    main()
