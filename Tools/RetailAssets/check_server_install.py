"""Exercise MySQL installation in isolated disposable tables; no game rows change."""
import copy
import json
import sys
import uuid
from pathlib import Path

import native_import as native


def isolated_sql(manifest, prefix):
    sql = native.server_sql(manifest)
    for name in (*manifest["target"]["records"], "esteria_retail_imports"):
        sql = sql.replace(f"`{name}`", f"`{prefix}{name}`")
    return sql


def main():
    manifest = json.loads((Path(sys.argv[1]) / "manifest.json").read_text())
    prefix = "_native_check_" + uuid.uuid4().hex[:12] + "_"
    setup = "\n".join(f"CREATE TABLE `{prefix}{name}` LIKE `{name}`;"
                       for name in manifest["target"]["records"])
    count = f"SELECT CONCAT('installed=',COUNT(*)) FROM `{prefix}item_template`;"
    try:
        result = native.query(setup + isolated_sql(manifest, prefix) + count + isolated_sql(manifest, prefix) + count)
        if result.splitlines() != ["installed=1", "installed=1"]:
            raise AssertionError(result)
        collision = copy.deepcopy(manifest)
        collision["source_key"] += ":conflict"
        collision["target"]["records"]["item_dbc"]["ID"] += 1
        collision["target"]["records"]["itemdisplayinfo_dbc"]["ID"] += 1
        sql = isolated_sql(collision, prefix) + "SELECT 'UNSAFE_CONTINUATION';"
        try:
            native.query(sql)
        except native.subprocess.CalledProcessError as error:
            if "UNSAFE_CONTINUATION" in error.stdout or "_native_retail_guard" not in error.stderr:
                raise AssertionError(error.stderr)
        else:
            raise AssertionError("Conflicting import did not abort")
        if native.query(count) != "installed=1":
            raise AssertionError("Collision changed existing content")
        for table in ("item_dbc", "itemdisplayinfo_dbc"):
            if native.query(f"SELECT COUNT(*) FROM `{prefix}{table}`;") != "1":
                raise AssertionError("Collision did not roll back partial additions")
    finally:
        native.query("DROP TABLE IF EXISTS " + ",".join(f"`{prefix}{name}`" for name in
                     (*manifest["target"]["records"], "esteria_retail_imports")) + ";")
    print("MySQL install, idempotence and collision-abort checks passed in isolated tables; game tables unchanged.")


if __name__ == "__main__":
    main()
