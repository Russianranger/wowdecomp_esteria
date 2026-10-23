r"""Stage CreatureModelData/CreatureDisplayInfo rows for an M3 model.

Writes, under --output:
  client/DBFilesClient/*.dbc   effective client tables (patch-Z.MPQ) plus the new rows, for an MPQ
  server/*.dbc1-<tag>          rows-only WXL continuations for the server's data/dbc-continuations
  <tag>.sql                    world DB rows (creature_template_model, creature_model_info, a test creature)
"""
import argparse, struct, sys
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--archive", type=Path, default=Path(r"G:\3.3.5a - Dev\Data\patch-Z.MPQ"))
p.add_argument("--model-path", required=True)
p.add_argument("--model-id", type=int, required=True)
p.add_argument("--display-id", type=int, required=True)
p.add_argument("--creature-entry", type=int, required=True)
p.add_argument("--name", required=True)
p.add_argument("--tag", required=True)
p.add_argument("--scale", type=float, default=1.0)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--tools", type=Path, default=Path(r"R:\Users\Zach\Documents\GitHub\EsteriaWoW\tools"))
p.add_argument("--known-ids", type=Path, nargs="*", default=[Path(r"G:\3.3.5a - Dev\Data\dbc-continuations")])
a = p.parse_args()
sys.path.insert(0, str(a.tools))
from cars_mount_pack import DLL_DEFAULT, Storm  # noqa: E402

f32 = lambda v: struct.unpack("<I", struct.pack("<f", v))[0]
storm = Storm(DLL_DEFAULT)
h = storm.open_archive(a.archive)

def parse(data):
    magic, rc, fc, rs, ss = struct.unpack("<4sIIII", data[:20])
    assert magic == b"WDBC" and fc * 4 == rs
    return rc, fc, rs, data[20:20 + rc * rs], data[20 + rc * rs:20 + rc * rs + ss]

def ids(data):
    rc, fc, rs, rows, _ = parse(data)
    return {struct.unpack_from("<I", rows, i * rs)[0] for i in range(rc)}

def build(rc, fc, rs, rows, strings):
    return struct.pack("<4sIIII", b"WDBC", rc, fc, rs, len(strings)) + rows + strings

model = storm.read(h, r"DBFilesClient\CreatureModelData.dbc")
display = storm.read(h, r"DBFilesClient\CreatureDisplayInfo.dbc")
taken = {"model": ids(model), "display": ids(display)}
for folder in a.known_ids:
    for f in folder.glob("*.dbc1-*") if folder.is_dir() else []:
        kind = "model" if f.name.startswith("CreatureModelData") else "display" if f.name.startswith("CreatureDisplayInfo") else None
        if kind:
            taken[kind] |= ids(f.read_bytes())
assert a.model_id not in taken["model"], "model id in use"
assert a.display_id not in taken["display"], "display id in use"

path_bytes = a.model_path.encode("ascii") + b"\0"
mrc, mfc, mrs, mrows, mstr = parse(model)
drc, dfc, drs, drows, dstr = parse(display)
assert (mfc, dfc) == (28, 16)
name_off = len(mstr)
model_row = [0] * 28
model_row[0] = a.model_id
model_row[2] = name_off
model_row[4] = f32(a.scale)
for i, v in ((12, .4), (13, 1.0), (15, -.5), (16, -.5), (17, 0), (18, .5), (19, .5), (20, 1.0), (21, 1.0), (22, 1.0)):
    model_row[i] = f32(v)
display_row = [0] * 16
display_row[0], display_row[1], display_row[4], display_row[5] = a.display_id, a.model_id, f32(1.0), 255
mpack = lambda r: struct.pack("<28I", *r)
dpack = lambda r: struct.pack("<16I", *r)

out = a.output
(out / "client" / "DBFilesClient").mkdir(parents=True, exist_ok=True)
(out / "server").mkdir(parents=True, exist_ok=True)
(out / "client" / "DBFilesClient" / "CreatureModelData.dbc").write_bytes(build(mrc + 1, 28, mrs, mrows + mpack(model_row), mstr + path_bytes))
(out / "client" / "DBFilesClient" / "CreatureDisplayInfo.dbc").write_bytes(build(drc + 1, 16, drs, drows + dpack(display_row), dstr))
# Continuations carry only the new rows with their own string block (leading NUL, as in a stock table).
(out / "server" / f"CreatureModelData.dbc1-{a.tag}").write_bytes(build(1, 28, mrs, mpack([*model_row[:2], 1, *model_row[3:]]), b"\0" + path_bytes))
(out / "server" / f"CreatureDisplayInfo.dbc1-{a.tag}").write_bytes(build(1, 16, drs, dpack(display_row), b"\0"))
(out / f"{a.tag}.sql").write_text(f"""-- {a.name}: test creature for the M3 model (display {a.display_id}, model {a.model_id}).
DELETE FROM `creature_template` WHERE `entry`={a.creature_entry};
DELETE FROM `creature_template_model` WHERE `CreatureID`={a.creature_entry};
DELETE FROM `creature_model_info` WHERE `DisplayID`={a.display_id};
INSERT INTO `creature_model_info` (`DisplayID`,`BoundingRadius`,`CombatReach`,`Gender`,`DisplayID_Other_Gender`) VALUES ({a.display_id},0.4,1,2,0);
INSERT INTO `creature_template` (`entry`,`name`,`minlevel`,`maxlevel`,`faction`,`npcflag`,`speed_walk`,`speed_run`,`unit_class`,`unit_flags`,`type`,`flags_extra`)
VALUES ({a.creature_entry},'{a.name}',1,1,35,0,1,1.14286,1,0,1,0);
INSERT INTO `creature_template_model` (`CreatureID`,`Idx`,`CreatureDisplayID`,`DisplayScale`,`Probability`) VALUES ({a.creature_entry},0,{a.display_id},1,1);
""")
print("rows added: model", a.model_id, "display", a.display_id, "name offset", name_off)
