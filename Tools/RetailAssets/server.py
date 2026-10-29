"""Scoped access to Esteria's Docker-hosted world database; credentials stay in Docker."""
import json
import re
import subprocess


def query(sql, *, database="acore_world", container="ac-database", timeout=None):
    if database not in {"acore_world", "acore_characters"}:
        raise ValueError("Unsupported database")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,127}", container):
        raise ValueError("Invalid database container name")
    result = subprocess.run(
        ["docker", "exec", "-i", container, "sh", "-c",
         'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --skip-column-names '
         '--default-character-set=utf8mb4 ' + database],
        input=sql, text=True, encoding="utf-8", capture_output=True, check=True, timeout=timeout)
    return result.stdout.strip()


KEYS = {"item_template": "entry", "item_dbc": "ID", "itemdisplayinfo_dbc": "ID",
        "creature_template": "entry", "creature_template_model": "CreatureID", "creature_model_info": "DisplayID"}


def snapshot(tables=None):
    result = {}
    for table in (tables or ("item_template", "item_dbc", "itemdisplayinfo_dbc")):
        key = KEYS[table]
        result[table] = {
            "columns": query(f"SHOW COLUMNS FROM `{table}`;").splitlines(),
            "ids": [int(row) for row in query(f"SELECT `{key}` FROM `{table}` ORDER BY `{key}`;").splitlines()],
        }
    return result


if __name__ == "__main__":
    data = snapshot()
    print(json.dumps({table: {"count": len(value["ids"]), "max": max(value["ids"], default=0),
                               "columns": [row.split("\t")[0] for row in value["columns"]]}
                      for table, value in data.items()}, indent=2))
