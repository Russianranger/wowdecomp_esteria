r"""Import several SC2 unit models into the client/server test setup.

Reads a units JSON ([{"key","name","path"}], path relative to ...\base.sc2assets\assets\ inside a mod such as
mods\liberty.sc2mod), fetches each .m3, its textures and animation banks from a local or online SC2 CASC with
sc2fetch, then writes: staged source files, client DBC tables (existing archive tables + new rows), rows-only
server continuations and one SQL file. Packing/installing is a separate step (pack_sc2_test.py).
"""
import argparse, json, struct, subprocess, sys, tempfile
from pathlib import Path

BS = chr(92)
p = argparse.ArgumentParser()
p.add_argument("--units", type=Path, required=True)
p.add_argument("--sc2", required=True, help="sc2fetch storage argument, e.g. local:E:/Blizzard/StarCraft II")
p.add_argument("--fetch", type=Path, required=True)
p.add_argument("--index", type=Path, required=True, help="file list from 'sc2fetch <storage> list \"\"'")
p.add_argument("--inspect", type=Path, required=True)
p.add_argument("--source", type=Path, required=True, help="staged source root holding Assets/")
p.add_argument("--client-archive", type=Path, required=True, help="archive whose DBC tables get the new rows")
p.add_argument("--output", type=Path, required=True)
p.add_argument("--first-model-id", type=int, default=2290100)
p.add_argument("--first-display-id", type=int, default=290100)
p.add_argument("--first-entry", type=int, default=9990100)
p.add_argument("--tag", default="sc2units")
p.add_argument("--tools", type=Path, default=Path(r"R:\Users\Zach\Documents\GitHub\EsteriaWoW\tools"))
a = p.parse_args()
sys.path.insert(0, str(a.tools))
from cars_mount_pack import DLL_DEFAULT, Storm  # noqa: E402

ASSETS = BS + "assets" + BS
units = json.loads(a.units.read_text())
index = {}
for line in a.index.read_text(encoding="utf-8", errors="replace").splitlines():
    name = line.split("\t")[0]
    low = name.lower()
    if ASSETS in low:
        index.setdefault(low.split(ASSETS, 1)[1], []).append(name)


def mod_of(name):
    return name.split(BS + "base.sc2assets")[0]


def preferred(hits, like=None):
    return (next((h for h in hits if like and mod_of(h) == mod_of(like)), None)
            or next((h for h in hits if "liberty.sc2mod" in h or "core.sc2mod" in h), None) or hits[0])


def fetch(pairs):
    if not pairs:
        return set()
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False, newline="") as f:
        f.write("".join(f"{c}\t{o}\n" for c, o in pairs))
    out = subprocess.run([str(a.fetch), a.sc2, "getmany", f.name], capture_output=True, text=True).stdout
    return {l.split("\t")[1] for l in out.splitlines() if l.startswith("ok")}


def candidates(model):  # port of ASECM3::AnimationBankCandidates
    path = model.replace("/", BS)
    folder = path[:path.rfind(BS) + 1]
    container = folder[:-1]
    parent = container[:container.rfind(BS) + 1] if BS in container else ""
    stem = path[path.rfind(BS) + 1:path.rfind(".")]
    out = []
    while stem:
        bank = stem + "_requiredanims"
        out += [folder + bank + ".m3a", parent + bank + BS + bank + ".m3a", parent + bank + ".m3a"]
        if "_" not in stem:
            break
        stem = stem[:stem.rfind("_")]
    return out


# 1) models
pairs, models = [], {}
for u in units:
    key = u["path"].lower().replace("/", BS)
    hits = index.get(key)
    if not hits:
        print("MISSING model", u["key"], u["path"])
        continue
    full = preferred(hits)
    dest = a.source / "Assets" / Path(*key.split(BS))
    dest.parent.mkdir(parents=True, exist_ok=True)
    pairs.append((full, str(dest)))
    models[u["key"]] = dict(u, full=full, dest=dest, rel=key)
done = fetch(pairs)
models = {k: m for k, m in models.items() if str(m["dest"]) in done}

# 2) textures and banks
report = {}
inspected = {}
for m in models.values():
    res = subprocess.run([str(a.inspect), str(m["dest"])], capture_output=True, text=True)
    try:
        for f in json.loads(res.stdout)["files"]:
            inspected[Path(f["path"]).resolve()] = f
    except (ValueError, KeyError):
        pass
tex_pairs, bank_pairs, seen = [], [], set()
for k, m in models.items():
    f = inspected.get(m["dest"].resolve())
    if not f or not f.get("ok"):
        report.setdefault(k, []).append("inspect failed")
        continue
    m["bitmaps"] = sorted({l["bitmap"] for l in f["textureLayers"] if l["bitmap"]})
    for bm in m["bitmaps"]:
        ref = bm.replace("/", BS)
        low = ref.lower()
        hits = index.get(low[len("assets" + BS):] if low.startswith("assets" + BS) else low)
        if not hits:
            report.setdefault(k, []).append("missing texture " + bm)
            continue
        dest = a.source / Path(*ref.split(BS))
        dest.parent.mkdir(parents=True, exist_ok=True)
        if not dest.exists() and str(dest).lower() not in seen:
            seen.add(str(dest).lower())
            tex_pairs.append((preferred(hits, m["full"]), str(dest)))
    for cand in candidates(m["rel"]):
        hits = index.get(cand)
        if hits:
            dest = a.source / "Assets" / Path(*cand.split(BS))
            dest.parent.mkdir(parents=True, exist_ok=True)
            bank_pairs.append((hits[0], str(dest)))
            m.setdefault("banks", []).append(cand)
got = fetch(tex_pairs + bank_pairs)
for c, o in tex_pairs + bank_pairs:
    if o not in got:
        report.setdefault("fetch", []).append(c)

# 3) DBC rows and SQL
storm = Storm(DLL_DEFAULT)
h = storm.open_archive(a.client_archive)
model_tab = storm.read(h, r"DBFilesClient\CreatureModelData.dbc")
disp_tab = storm.read(h, r"DBFilesClient\CreatureDisplayInfo.dbc")
f32 = lambda v: struct.unpack("<I", struct.pack("<f", v))[0]


def parse(d):
    _, rc, fc, rs, ss = struct.unpack("<4sIIII", d[:20])
    return rc, fc, rs, d[20:20 + rc * rs], d[20 + rc * rs:20 + rc * rs + ss]


def build(rc, fc, rs, rows, strings):
    return struct.pack("<4sIIII", b"WDBC", rc, fc, rs, len(strings)) + rows + strings


mrc, mfc, mrs, mrows, mstr = parse(model_tab)
drc, dfc, drs, drows, dstr = parse(disp_tab)
assert (mfc, dfc) == (28, 16)
used_m = {struct.unpack_from("<I", mrows, i * mrs)[0] for i in range(mrc)}
used_d = {struct.unpack_from("<I", drows, i * drs)[0] for i in range(drc)}
newm = newd = cmrows = cdrows = b""
cstr = b"\0"
sql, manifest = [], []
for i, (k, m) in enumerate(models.items()):
    mid, did, entry = a.first_model_id + i, a.first_display_id + i, a.first_entry + i
    assert mid not in used_m and did not in used_d, (mid, did)
    path = "SC2" + BS + m["rel"]
    pb = path.encode("ascii") + b"\0"
    row = [0] * 28
    row[0], row[4] = mid, f32(1.0)
    for j, v in ((12, .4), (13, 1.0), (15, -.5), (16, -.5), (17, 0), (18, .5), (19, .5), (20, 1.0), (21, 1.0), (22, 1.0)):
        row[j] = f32(v)
    drow = [0] * 16
    drow[0], drow[1], drow[4], drow[5] = did, mid, f32(1.0), 255
    crow = list(row)
    row[2], crow[2] = len(mstr), len(cstr)
    mstr += pb
    cstr += pb
    newm += struct.pack("<28I", *row)
    cmrows += struct.pack("<28I", *crow)
    newd += struct.pack("<16I", *drow)
    cdrows += struct.pack("<16I", *drow)
    manifest.append(dict(key=k, name=m["name"], model=path, modelId=mid, displayId=did, entry=entry,
                         banks=m.get("banks", []), textures=m.get("bitmaps", [])))
    nm = m["name"].replace("'", "''")
    sql.append(
        f"DELETE FROM `creature_template` WHERE `entry`={entry};\n"
        f"DELETE FROM `creature_template_model` WHERE `CreatureID`={entry};\n"
        f"DELETE FROM `creature_model_info` WHERE `DisplayID`={did};\n"
        f"INSERT INTO `creature_model_info` (`DisplayID`,`BoundingRadius`,`CombatReach`,`Gender`,`DisplayID_Other_Gender`) VALUES ({did},0.4,1,2,0);\n"
        f"INSERT INTO `creature_template` (`entry`,`name`,`minlevel`,`maxlevel`,`faction`,`npcflag`,`speed_walk`,`speed_run`,`unit_class`,`unit_flags`,`type`,`flags_extra`) "
        f"VALUES ({entry},'SC2 {nm}',1,1,35,0,1,1.14286,1,0,1,0);\n"
        f"INSERT INTO `creature_template_model` (`CreatureID`,`Idx`,`CreatureDisplayID`,`DisplayScale`,`Probability`) VALUES ({entry},0,{did},1,1);\n")

out = a.output
(out / "client" / "DBFilesClient").mkdir(parents=True, exist_ok=True)
(out / "server").mkdir(parents=True, exist_ok=True)
n = len(models)
(out / "client" / "DBFilesClient" / "CreatureModelData.dbc").write_bytes(build(mrc + n, 28, mrs, mrows + newm, mstr))
(out / "client" / "DBFilesClient" / "CreatureDisplayInfo.dbc").write_bytes(build(drc + n, 16, drs, drows + newd, dstr))
(out / "server" / f"CreatureModelData.dbc1-{a.tag}").write_bytes(build(n, 28, mrs, cmrows, cstr))
(out / "server" / f"CreatureDisplayInfo.dbc1-{a.tag}").write_bytes(build(n, 16, drs, cdrows, b"\0"))
(out / f"{a.tag}.sql").write_text("".join(sql))
(out / "manifest.json").write_text(json.dumps(dict(units=manifest, problems=report), indent=1))
print(f"{n} units staged; problems: {json.dumps(report)}")
