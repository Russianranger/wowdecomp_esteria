"""Render the real interface with a saved inventory and a test-only Tauri IPC adapter."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--chrome", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--settings-page", action="store_true")
    args = parser.parse_args()
    project = json.loads(args.project.read_text(encoding="utf-8"))
    if not project["mounts"]:
        raise ValueError("Preview requires a real scanned inventory")
    project["settings"].update({
        "source_mode": "local", "retail_root": "", "source_locale": "2",
        "cdn_build": project["source"]["build"], "cdn_region": "us", "client_config": "", "keys_file": "",
        "export_dir": "", "auto_open_export": True, "archive_limit_mb": 1800, "download_timeout": 600,
        "mapping_mode": "bundled", "mapping_file": "runtime/mount-models.json", "database_container": "ac-database",
        "default_mount_type": "flying", "animations": True})
    ui = Path(__file__).resolve().parents[1] / "ui"
    adapter = """
window.__TAURI__ = {
  core: {invoke: async (command, request) => {
    if (command === 'defaults') return previewProject.settings;
    window.previewLastRequest = request;
  }},
  event: {listen: async (name, handler) => {
    window.previewHandler = handler;
    setTimeout(() => handler({payload: {type: 'project', project: previewProject}}), 10);
  }}
};
window.addEventListener('load', () => setTimeout(() => {
  try {
    document.getElementById('settings-toggle').click();
    if (document.getElementById('settings').hidden || !document.getElementById('workspace').hidden)
      throw Error('Settings is not a separate page');
    const sourceMode = document.getElementById('source_mode');
    if (sourceMode.value !== 'local') throw Error('Installed Retail is not the default');
    sourceMode.value = 'cdn'; sourceMode.dispatchEvent(new Event('change'));
    if (!document.querySelector('[data-source="local"]').hidden
      || document.querySelector('[data-source="cdn"]').hidden) throw Error('Source settings did not switch');
    sourceMode.value = 'local'; sourceMode.dispatchEvent(new Event('change'));
    document.getElementById('settings-save').click();
    const preferences = JSON.parse(localStorage.getItem('mount-studio-settings'));
    if (preferences.source_mode !== 'local' || !preferences.auto_open_export)
      throw Error('Settings did not persist');
    document.getElementById('workspace-tab').click();
    const theme = document.getElementById('theme-toggle');
    if (document.documentElement.dataset.theme !== 'dark') throw Error('Dark mode is not the default');
    theme.click();
    if (document.documentElement.dataset.theme !== 'light') throw Error('Light mode toggle failed');
    theme.click();
    if (localStorage.getItem('mount-studio-theme') !== 'dark') throw Error('Theme was not persisted');
    const type = document.getElementById('mount-type');
    const filter = document.getElementById('filter');
    for (const value of ['ground', 'flying', 'water', 'all']) {
      type.value = value;
      type.dispatchEvent(new Event('input'));
      const expected = previewProject.mounts.filter(row => value === 'all' || row.mount_types.includes(value));
      const actual = document.querySelectorAll('#mounts tr').length;
      if (actual !== Math.min(expected.length, 80)) throw Error('Mount type filter failed: ' + value);
    }
    if (!document.querySelector('#mounts tr')) throw Error('No inventory rows rendered');
    document.querySelector('.mount-name').click();
    if (!document.querySelector('#details h2')) throw Error('Mount details did not open');
    const search = document.getElementById('search');
    search.value = 'this-mount-does-not-exist-184749';
    search.dispatchEvent(new Event('input'));
    if (document.querySelector('#mounts tr')) throw Error('Search did not filter rows');
    search.value = '';
    search.dispatchEvent(new Event('input'));
    filter.value = 'protected';
    filter.dispatchEvent(new Event('input'));
    if ([...document.querySelectorAll('#mounts input')].some(input => !input.disabled))
      throw Error('Protected mount selection is enabled');
    filter.value = 'all';
    filter.dispatchEvent(new Event('input'));
    search.value = 'Big Blizzard Bear';
    search.dispatchEvent(new Event('input'));
    if (previewProject.mounts.some(row => row.display === 27567)) {
      if (document.querySelectorAll('#mounts tr').length !== 1) throw Error('Bear variants were not grouped');
    }
    search.value = '';
    type.value = 'flying';
    type.dispatchEvent(new Event('input'));
    document.getElementById('select-matched').click();
    if (project.mounts.some(row => row.selected && !row.mount_types.includes('flying')))
      throw Error('Select only filtered selected another mount type');
    if (!project.mounts.some(row => row.selected)) throw Error('No flying mounts were selected');
    document.querySelector('.mount-name').click();
    const active = project.mounts.find(row => row.key === currentKey);
    const previousStatus = active.status;
    active.status = 'acquiring';
    sourceMode.value = 'local'; render();
    if (document.querySelector('tr.active .status-acquiring')?.textContent !== 'Reading local')
      throw Error('Local read was not identified as local');
    sourceMode.value = 'cdn'; render();
    if (document.querySelector('tr.active .status-acquiring')?.textContent !== 'CDN / cache')
      throw Error('CDN/cache access was not identified');
    sourceMode.value = 'local'; active.status = previousStatus; render();
    const ready = project.mounts.filter(row => row.selected && row.status === 'validated').length;
    const selected = project.mounts.filter(row => row.selected).length;
    if (ready > 0 && ready < selected) {
      document.getElementById('export').click();
      if (!document.getElementById('export-dialog').open) throw Error('Partial export dialog did not open');
      document.getElementById('export-ready').click();
      if (!window.previewLastRequest?.request.ready_only) throw Error('Completed-only export was not requested');
      if (project.mounts.filter(row => row.selected).length !== selected) throw Error('Export changed selections');
    }
    window.previewHandler({payload: {type: 'error', message: 'Expected export explanation'}});
    window.previewHandler({payload: {type: 'idle'}});
    if (document.getElementById('message').textContent !== 'Expected export explanation'
      || document.getElementById('state').textContent !== 'Needs attention') throw Error('Error was overwritten');
    setBusy(true); setBusy(false);
    document.getElementById('log').textContent = '';
    log(ready + ' completed mounts ready for MPQ export.');
    if (previewSettingsPage) showPage('settings');
    document.body.dataset.testResult = 'passed';
  } catch (error) { document.body.dataset.testResult = String(error); }
}, 100));
"""
    data = json.dumps(project, ensure_ascii=True).replace("</", "<\\/")
    html = (ui / "index.html").read_text(encoding="utf-8")
    html = html.replace('<script src="theme.js"></script>',
                        "<script>" + (ui / "theme.js").read_text(encoding="utf-8") + "</script>")
    html = html.replace('<link rel="stylesheet" href="style.css">',
                        "<style>" + (ui / "style.css").read_text() + "</style>")
    html = html.replace('<script src="app.js" defer></script>', "")
    flag = json.dumps(args.settings_page)
    html = html.replace("</body>", f"<script>const previewSettingsPage={flag};const previewProject={data};"
                        f"{adapter}</script><script>"
                        + (ui / "app.js").read_text(encoding="utf-8") + "</script></body>")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mount-studio-ui-") as temp:
        page = Path(temp) / "index.html"
        page.write_text(html, encoding="utf-8")
        result = subprocess.run([str(args.chrome), "--headless", "--disable-gpu", "--no-first-run",
                                 "--no-default-browser-check", f"--user-data-dir={temp}/profile",
                                 "--window-size=1380,1100", "--virtual-time-budget=2000", "--dump-dom",
                                 f"--screenshot={args.output.resolve()}", page.as_uri()],
                                capture_output=True, text=True, encoding="utf-8", timeout=40)
        if result.returncode or 'data-test-result="passed"' not in result.stdout:
            raise RuntimeError("Browser UI smoke failed: " + result.stderr[-2000:])
    print("UI checks passed: inventory, filters, theme persistence, completed-only export and error retention.")
    print(args.output.resolve())


if __name__ == "__main__":
    main()
