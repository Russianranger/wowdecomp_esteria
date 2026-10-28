"""The skyriding ability spells for speedflying: one definition, written to both sides.

usage: speedflying_abilities.py --client "G:/3.3.5a - Dev" --sql <module>/data/sql/db-world/20261028_speedflying_abilities.sql

Client: appends the four Spell rows, their SpellIcon rows and the retail icon aliases to the active supplemental
registry (Data/Retail/RetailCasc.ini `tables=` / `aliases=`). Re-running replaces our rows and leaves the others.
native_import.py rebuilds that registry from its packs on every install, so run this again after one.
Server: writes the spell_dbc + spell_script_names SQL (spell_dbc's columns are Spell.dbc's fields, in order).
"""
import argparse, struct
from pathlib import Path

FIELDS = 234
STRINGS = [i for start in (136, 153, 170, 187) for i in range(start, start + 16)]
LANG_MASK = 16712190            # the locale-mask word after each string block, as stock rows carry it
TOOLTIP_MASK = 16712188

# id, icon id, retail icon FileDataID, icon name, cooldown ms, name, description
ABILITIES = [
    (300100, 600010, 4640492, "Ability_DragonRiding_Launch01", 3000, "Lift Off",
     "Launch into the air from the ground on a flying mount."),
    (300101, 600011, 4640498, "Ability_DragonRiding_UpwardFlap01", 1000, "Skyward Ascent",
     "Flap powerfully upward. Costs one boost stack."),
    (300102, 600012, 4640477, "Ability_DragonRiding_BarrelRoll01", 1000, "Whirling Surge",
     "Barrel roll forward with a burst of speed. Costs one boost stack."),
    (300103, 600013, 4640486, "Ability_DragonRiding_DragonridingGliding01", 1000, "Flap",
     "Beat your wings to gain a little height."),
]
SPELL_IDS = {a[0] for a in ABILITIES}
ICON_IDS = {a[1] for a in ABILITIES}
ICON_FILES = {a[2] for a in ABILITIES}


def spell_row(spell_id, icon_id, cooldown, name, description):
    row = [0] * FIELDS
    row[0] = spell_id
    row[4] = 0x01010010         # ABILITY | NOT_SHAPESHIFTED | ALLOW_WHILE_MOUNTED
    row[28] = 1                 # instant
    row[29] = cooldown
    row[46] = 1                 # self
    row[68] = 0xFFFFFFFF        # no equipped item class
    row[71] = 3                 # SPELL_EFFECT_DUMMY: mod-mana-speedflying's spell_speedflying_ability
    row[74] = 1
    row[86] = 1                 # TARGET_UNIT_CASTER
    row[133] = icon_id
    row[136], row[152] = name, LANG_MASK
    row[170], row[186] = description, LANG_MASK
    row[203] = TOOLTIP_MASK
    row[216] = row[217] = row[218] = 0x3F800000  # effect damage multipliers 1.0
    row[225] = 1                # physical
    return row


def read_wdbc(path, fields):
    if not path.exists():
        return []
    d = path.read_bytes()
    assert d[:4] == b"WDBC", path
    n, f, rs, ss = struct.unpack_from("<4I", d, 4)
    assert f == fields and rs == f * 4, (path, f, rs)
    pool = d[20 + n * rs:]
    strings = set(STRINGS) if fields == FIELDS else {1}
    rows = []
    for i in range(n):
        r = list(struct.unpack_from(f"<{f}I", d, 20 + i * rs))
        for j in strings:
            if r[j]:
                r[j] = pool[r[j]:pool.index(0, r[j])].decode("utf-8")
        rows.append(r)
    return rows


def write_wdbc(path, fields, rows):
    pool, offsets = bytearray(b"\0"), {"": 0}
    body = bytearray()
    for r in rows:
        out = []
        for v in r:
            if isinstance(v, str):
                if v not in offsets:
                    offsets[v] = len(pool)
                    pool += v.encode("utf-8") + b"\0"
                v = offsets[v]
            out.append(v)
        body += struct.pack(f"<{fields}I", *out)
    path.write_bytes(b"WDBC" + struct.pack("<4I", len(rows), fields, fields * 4, len(pool)) + body + pool)


def registry(client):
    ini = client / "Data/Retail/RetailCasc.ini"
    values = dict(line.split("=", 1) for line in ini.read_text(encoding="ascii").splitlines() if "=" in line)
    root = ini.parent
    return root / values["tables"], root / values["aliases"]


def install_client(client):
    tables, aliases = registry(client)
    spells = [r for r in read_wdbc(tables / "spell.dbc", FIELDS) if r[0] not in SPELL_IDS]
    icons = [r for r in read_wdbc(tables / "spellicon.dbc", 2) if r[0] not in ICON_IDS]
    for spell_id, icon_id, file_id, icon, cooldown, name, description in ABILITIES:
        spells.append(spell_row(spell_id, icon_id, cooldown, name, description))
        icons.append([icon_id, "Interface\\Icons\\" + icon])
    write_wdbc(tables / "spell.dbc", FIELDS, spells)
    write_wdbc(tables / "spellicon.dbc", 2, icons)

    lines = [l for l in aliases.read_text(encoding="ascii").splitlines()
             if l and int(l.split(";")[0]) not in ICON_FILES] if aliases.exists() else []
    lines += [f"{file_id};Interface\\Icons\\{icon}.blp" for _, _, file_id, icon, *_ in ABILITIES]
    aliases.write_text("".join(l + "\n" for l in lines), encoding="ascii")

    # self-check: what was written reads back
    back = {r[0]: r for r in read_wdbc(tables / "spell.dbc", FIELDS)}
    assert all(back[a[0]][136] == a[5] and back[a[0]][133] == a[1] for a in ABILITIES)
    print(f"client registry {tables}: {len(spells)} spell rows, {len(icons)} icon rows")


def sql_value(v):
    return "'" + v.replace("'", "''") + "'" if isinstance(v, str) else str(v if v != 0xFFFFFFFF else -1)


def write_sql(path, columns):
    lines = ["-- Esteria: the skyriding abilities on speedflying. Generated by wowdecomp Tools/RetailAssets/speedflying_abilities.py;",
             "-- the client gets the same rows through its supplemental registry. Re-runnable.",
             f"DELETE FROM `spell_dbc` WHERE `ID` IN ({', '.join(str(i) for i in sorted(SPELL_IDS))});",
             f"DELETE FROM `spell_script_names` WHERE `spell_id` IN ({', '.join(str(i) for i in sorted(SPELL_IDS))});"]
    for spell_id, icon_id, _, _, cooldown, name, description in ABILITIES:
        row = spell_row(spell_id, icon_id, cooldown, name, description)
        used = [i for i, v in enumerate(row) if v not in (0, "")]
        lines.append(f"INSERT INTO `spell_dbc` ({', '.join('`' + columns[i] + '`' for i in used)}) VALUES "
                     f"({', '.join(sql_value(row[i]) for i in used)});")
        lines.append(f"INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES ({spell_id}, 'spell_speedflying_ability');")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"server sql {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--client", type=Path)
    parser.add_argument("--sql", type=Path)
    parser.add_argument("--columns", type=Path, help="spell_dbc column names, one per line (SHOW COLUMNS order)")
    args = parser.parse_args()
    if args.client:
        install_client(args.client)
    if args.sql:
        columns = args.columns.read_text(encoding="ascii").split()
        assert len(columns) == FIELDS and columns[0] == "ID" and columns[136] == "Name_Lang_enUS", "unexpected spell_dbc layout"
        write_sql(args.sql, columns)


if __name__ == "__main__":
    main()
