"""Reads Lumenveil's own settings window (engine/src/settings_panel.cpp) and the key tables of the ported config
(Engine/Source/Lumenveil/Core/config.cpp) and writes Engine/Source/Lumenveil/OptionsMeta.inc: for every control the
window has, the ini key it edits, the section it sits in, its label, format, help text and, for a choice, its names.

The in-game Video options pages (Engine/Source/Lumenveil/OptionsPages.cpp) are built from that table. Settings the window
does not show are still offered, under "Other", with a label made from the key.

    python GenerateOptions.py [path/to/settings_panel.cpp]

The default is the mod checkout next to this repository (Lumenveil/engine/src/settings_panel.cpp). The output is committed:
a build needs no mod checkout.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
CONFIG = os.path.join(ROOT, "Engine", "Source", "Lumenveil", "Core", "config.cpp")
OUT = os.path.join(ROOT, "Engine", "Source", "Lumenveil", "OptionsMeta.inc")
PANEL = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "..", "Lumenveil", "engine", "src", "settings_panel.cpp")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def member_to_key(text):
    """{"Key", &Config::member, ...} and {reliefclass::kInfo[...].key, &Config::member, ...} -> member: key."""
    table = {}
    for m in re.finditer(r'\{\s*"(\w+)"\s*,\s*&Config::(\w+)', text):
        table[m.group(2)] = m.group(1)
    # the relief class shares take their key from reliefclass::kInfo; the panel's labels name them by member
    return table


def split_args(s):
    """Top-level comma split of a call's argument text (strings and nested brackets respected)."""
    args, depth, cur, in_str, i = [], 0, [], False, 0
    while i < len(s):
        c = s[i]
        if in_str:
            cur.append(c)
            if c == "\\":
                i += 1
                cur.append(s[i])
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
            cur.append(c)
        elif c in "([{":
            depth += 1
            cur.append(c)
        elif c in ")]}":
            depth -= 1
            cur.append(c)
        elif c == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    if "".join(cur).strip():
        args.append("".join(cur).strip())
    return args


def literal(arg):
    """The text of one or more adjacent C string literals, or None."""
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', arg)
    if not parts:
        return None
    if re.sub(r'"(?:[^"\\]|\\.)*"', "", arg).strip():
        return None
    return "".join(parts).replace('\\"', '"').replace("\\n", " ")


def balanced_call(text, start):
    """The text inside the parentheses that open at text[start] == '('."""
    depth, i, in_str = 0, start, False
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\":
                i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[start + 1:i], i
        i += 1
    return text[start + 1:], len(text)


def name_arrays(text):
    arrays = {}
    for m in re.finditer(r"const char\* const (k\w+)\[\]\s*=\s*\{(.*?)\};", text, re.S):
        arrays[m.group(1)] = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))
    return arrays


def main():
    config = read(CONFIG)
    panel = read(PANEL)
    keys = member_to_key(config)
    arrays = name_arrays(panel)

    sections = [(m.start(), m.group(1)) for m in re.finditer(r'Section\s+\w+\(\s*"([^"]+)"', panel)]

    def section_at(pos):
        name = "Other"
        for start, title in sections:
            if start <= pos:
                name = title
        return name

    rows = []
    seen = set()
    for m in re.finditer(r"\b(Slider|Multiplier|Count|Toggle|Choice)\s*\(", panel):
        kind = m.group(1)
        if panel[max(0, m.start() - 5):m.start()].endswith(("bool ", "int ")):
            continue  # a definition
        body, _ = balanced_call(panel, m.end() - 1)
        args = split_args(body)
        if len(args) < 3:
            continue
        label = literal(args[0])
        member = re.match(r"^(?:m_)?c\.(\w+)$", args[1]) if args[1] else None
        if not label or not member:
            continue
        key = keys.get(member.group(1))
        if not key or key in seen:
            continue
        fmt, help_text, names = "", "", []
        if kind == "Slider":
            fmt = literal(args[4]) if len(args) > 4 else ""
            help_text = literal(args[5]) if len(args) > 5 else ""
        elif kind == "Multiplier":
            fmt, help_text = "%.2f", literal(args[2])
        elif kind == "Count":
            fmt, help_text = "%d", literal(args[4]) if len(args) > 4 else ""
        elif kind == "Toggle":
            help_text = literal(args[2])
        elif kind == "Choice":
            ident = re.sub(r"[^\w]", "", args[3]) if len(args) > 3 else ""
            names = arrays.get(ident, [])
            help_text = literal(args[4]) if len(args) > 4 else ""
        seen.add(key)
        rows.append((key, section_at(m.start()), label, fmt or "", " ".join((help_text or "").split()), names))

    def c_string(text):
        return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'

    lines = [
        "// Generated by Tools/LumenveilOptions/GenerateOptions.py from Lumenveil's settings window; do not edit.",
        "// { ini key, section, label, format, help, choice names separated by '|' }",
        "",
    ]
    for key, section, label, fmt, help_text, names in rows:
        lines.append("    { %s, %s, %s, %s, %s, %s }," % (c_string(key), c_string(section), c_string(label), c_string(fmt),
                                                         c_string(help_text), c_string("|".join(names))))
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    order = []
    for _, section, *_ in rows:
        if section not in order:
            order.append(section)
    print(f"{len(rows)} settings in {len(order)} sections: " + ", ".join(order))


if __name__ == "__main__":
    main()
