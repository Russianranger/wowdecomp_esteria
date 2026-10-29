"use strict";

const $ = (id) => document.getElementById(id);
const settingKeys = ["client", "work", "locale", "client_config", "source_mode", "retail_root", "source_locale",
  "cdn_build", "cdn_region", "keys_file", "export_dir", "auto_open_export", "archive_limit_mb", "download_timeout",
  "mapping_mode", "mapping_file", "database_container", "default_mount_type", "animations"];
const componentKeys = ["python", "probe", "storm", "reader", "definitions", "source_config"];
let bundledSettings = {};
let project = { mounts: [], catalog: [], assets: {}, source: null };
let busy = false;
let hasError = false;
let dirty = false;
let currentKey = null;
let page = 0;
const pageSize = 80;
const api = window.__TAURI__;

function node(tag, value, className) {
  const element = document.createElement(tag);
  if (value !== undefined) element.textContent = value;
  if (className) element.className = className;
  return element;
}

function settings() {
  const values = { ...bundledSettings };
  for (const key of settingKeys) {
    const input = $(key);
    values[key] = input.type === "checkbox" ? input.checked
      : input.type === "number" ? input.valueAsNumber : input.value.trim();
  }
  return values;
}

function saveSettings() {
  for (const input of document.querySelectorAll("#settings input")) {
    if (!input.checkValidity()) {
      showPage("settings");
      input.reportValidity();
      return false;
    }
  }
  localStorage.setItem("mount-studio-settings", JSON.stringify(settings()));
  document.documentElement.dataset.motion = $("animations").checked ? "on" : "off";
  syncSettings();
  $("settings-message").textContent = "Settings saved.";
  return true;
}

function showPage(page) {
  const setup = page === "settings";
  $("settings").hidden = !setup;
  $("workspace").hidden = setup;
  $("settings-toggle").setAttribute("aria-current", setup ? "page" : "false");
  $("workspace-tab").setAttribute("aria-current", setup ? "false" : "page");
}

function syncSettings() {
  const mode = $("source_mode").value;
  document.querySelectorAll("[data-source]").forEach((row) => { row.hidden = row.dataset.source !== mode; });
  document.querySelectorAll("[data-mapping]").forEach((row) => {
    row.hidden = row.dataset.mapping !== $("mapping_mode").value;
  });
  $("source-location").textContent = mode === "local"
    ? `Installed Retail · ${$("retail_root").value || "Choose an installation in Settings"}`
    : `Blizzard CDN · ${$("cdn_region").value.toUpperCase()} · ${$("cdn_build").value}`;
}

function log(message, error = false) {
  if (error) hasError = true;
  document.body.dataset.error = String(hasError);
  $("message").textContent = message;
  $("settings-message").textContent = message;
  $("state").textContent = hasError ? "Needs attention" : (busy ? "Working" : "Ready");
  const lines = $("log").textContent.split("\n").slice(-150);
  lines.push(`${new Date().toLocaleTimeString()}  ${message}`);
  $("log").textContent = lines.join("\n");
  $("log").scrollTop = $("log").scrollHeight;
}

function setBusy(value) {
  busy = value;
  if (value) hasError = false;
  document.body.dataset.busy = String(value);
  document.body.dataset.error = String(hasError);
  document.querySelectorAll("[data-action], #select-matched, #clear, #settings input, #settings select, "
    + "#settings-save, [data-browse]")
    .forEach((element) => { element.disabled = value; });
  $("pause").disabled = !value;
  $("state").textContent = hasError ? "Needs attention" : (value ? "Working" : "Ready");
  render();
}

function filtered() {
  const query = $("search").value.toLocaleLowerCase();
  const filter = $("filter").value;
  const mountType = $("mount-type").value;
  return project.mounts.filter((row) => {
    const variants = (row.spells || []).map((variant) => `${variant.id} ${variant.name}`).join(" ");
    const matches = `${row.name} ${variants} ${row.spell} ${row.display} ${row.model_path} ${row.source?.model ?? ""}`
      .toLocaleLowerCase().includes(query);
    return matches && (mountType === "all" || row.mount_types?.includes(mountType))
      && (filter === "all" || (filter === "selected" ? row.selected
      : filter === "unmatched" ? ["unmatched", "ambiguous"].includes(row.status) : row.status === filter));
  });
}

function render() {
  const rows = filtered();
  if (!rows.some((row) => row.key === currentKey)) currentKey = null;
  page = Math.max(0, Math.min(page, Math.ceil(rows.length / pageSize) - 1));
  const body = $("mounts");
  body.replaceChildren();
  rows.slice(page * pageSize, (page + 1) * pageSize).forEach((row) => {
    const tr = node("tr");
    tr.dataset.status = row.status;
    if (row.key === currentKey) tr.className = "active";
    const selection = node("input");
    selection.type = "checkbox";
    selection.checked = row.selected;
    selection.disabled = busy || row.protected || !row.source;
    selection.setAttribute("aria-label", `Include ${row.name}`);
    selection.addEventListener("change", () => { row.selected = selection.checked; dirty = true; render(); });
    const use = node("td");
    use.append(selection);
    const name = node("td");
    const button = node("button", row.name || "Unnamed mount", "mount-name");
    button.addEventListener("click", () => { currentKey = row.key; render(); });
    const spellLabel = row.spells?.length > 1 ? `${row.spells.length} spell variants` : `Spell ${row.spell}`;
    name.append(button, node("span", `${spellLabel} · Display ${row.display}`, "secondary"));
    const adv = row.status === "validated"
      ? (row.advfly?.length ? `${row.advfly.length} clips` : "None authored") : "Unchecked";
    const status = row.status === "acquiring"
      ? ($("source_mode").value === "local" ? "Reading local" : "CDN / cache")
      : row.status === "validated" ? "Ready" : row.status;
    tr.append(use, name, node("td", (row.mount_types || []).join(" / ")), node("td", row.source?.model ?? "—"),
      node("td", status, `status-${row.status}`), node("td", adv));
    body.append(tr);
  });
  const selected = project.mounts.filter((row) => row.selected);
  const validated = selected.filter((row) => row.status === "validated").length;
  const spellCount = project.mounts.reduce((count, row) => count + (row.spells?.length || 1), 0);
  $("counts").textContent = `${project.mounts.length} unique appearances · ${spellCount} spell variants · `
    + `${selected.length} selected · `
    + `${validated} ready to export · ${selected.length - validated} unfinished · `
    + `${project.mounts.filter((row) => row.protected).length} protected`
    + (dirty ? " · Unsaved choices" : "");
  $("empty").hidden = rows.length > 0;
  $("empty").textContent = project.mounts.length ? "No mounts match this filter."
    : "Scan the client to list mount spells and appearances.";
  const first = rows.length ? page * pageSize + 1 : 0;
  $("page").textContent = `${first}–${Math.min((page + 1) * pageSize, rows.length)} of ${rows.length}`;
  $("previous").disabled = page === 0;
  $("next").disabled = (page + 1) * pageSize >= rows.length;
  $("progress").max = Math.max(selected.length, 1);
  $("progress").value = validated;
  $("export").disabled = busy || validated === 0;
  $("export").textContent = validated ? `Export MPQ · ${validated} ready` : "Export MPQ";
  $("export").title = validated ? `${validated} completed mounts can be packaged` : "Acquire mounts before exporting";
  $("source-pin").textContent = project.source
    ? `${project.source.product} · ${project.source.build}` : "No retail build pinned";
  $("export-path").textContent = project.last_export ? `Last export: ${project.last_export}` : "";
  $("open-output").hidden = !project.last_export;
  renderDetails();
}

function renderDetails() {
  const panel = $("details");
  panel.replaceChildren();
  const row = project.mounts.find((item) => item.key === currentKey);
  if (!row) { panel.append(node("p", "Select a mount to review its replacement.")); return; }
  panel.append(node("h2", row.name));
  if (row.error) panel.append(node("p", row.error, "detail-error"));
  const dl = node("dl");
  for (const [key, value] of [["Current model", row.model_path || "Unresolved"],
    ["Mount types", (row.mount_types || []).join(", ")],
    ["Current textures", row.textures.filter(Boolean).join(", ") || "None"],
    ["Retail appearance", row.source ? `${row.source.name} / display ${row.source.display}` : "No match chosen"],
    ["Animation data", row.advfly?.length ? `AdvFly IDs: ${row.advfly.join(", ")}` : "Not audited or none authored"],
    ["Original files", row.files?.length ?? 0]]) {
    dl.append(node("dt", key), node("dd", value));
  }
  panel.append(dl);
  if (row.spells?.length) {
    const variants = node("details");
    variants.append(node("summary", `${row.spells.length} shared spell variant${row.spells.length === 1 ? "" : "s"}`));
    const list = node("ul");
    row.spells.forEach((variant) => list.append(node("li", `${variant.id}: ${variant.name}`)));
    variants.append(list);
    panel.append(variants);
  }
  if (row.protected) {
    panel.append(node("p", "Protected: custom spell or native/custom model. This tool cannot replace it."));
    return;
  }
  const label = node("label", "Retail counterpart");
  const select = node("select");
  select.disabled = busy;
  const none = node("option", "Choose an appearance…");
  none.value = "";
  select.append(none);
  // Matching candidates come first, with the full catalog available for reviewed substitutions.
  const candidates = [...(row.candidates || []), ...project.catalog];
  const seen = new Set();
  for (const source of candidates) {
    const key = `${source.mount}:${source.display}`;
    if (seen.has(key)) continue;
    seen.add(key);
    const option = node("option", `${source.name || `Mount ${source.mount}`} — display ${source.display}`);
    option.value = key;
    option.selected = source.mount === row.source?.mount && source.display === row.source?.display;
    select.append(option);
  }
  select.addEventListener("change", () => {
    row.source = project.catalog.find((source) => `${source.mount}:${source.display}` === select.value) || null;
    row.selected = Boolean(row.source);
    row.status = row.source ? "matched" : "unmatched";
    row.files = []; row.advfly = []; row.error = "";
    dirty = true;
    render();
  });
  label.append(select);
  panel.append(node("p", ""), label);
  panel.append(node("p", "Model data is retained unchanged. "
    + "A validated package still needs in-game riding and flight checks."));
}

async function run(action, options = {}) {
  if (!api || busy) return;
  if (!saveSettings()) { showPage("settings"); return; }
  if (action === "export" && !options.ready_only) {
    const selected = project.mounts.filter((row) => row.selected && !row.protected);
    const ready = selected.filter((row) => row.status === "validated");
    if (!ready.length) { log("No completed mounts selected. Acquire at least one mount first.", true); return; }
    if (ready.length < selected.length) {
      $("export-summary").textContent = `${ready.length} of ${selected.length} selected mounts are ready. `
        + `${selected.length - ready.length} are unfinished, paused, or failed.`;
      $("export-ready").textContent = `Export ${ready.length} completed mounts`;
      $("export-dialog").showModal();
      return;
    }
  }
  if ((action === "scan" || action === "catalog") && dirty
    && !window.confirm("Rescanning or rematching resets unsaved choices. Continue?")) return;
  const config = settings();
  localStorage.setItem("mount-studio-settings", JSON.stringify(config));
  const edits = project.mounts.map((row) => ({ key: row.key, selected: row.selected,
    mount: row.source?.mount, display: row.source?.display }));
  setBusy(true);
  log(`Starting ${action}…`);
  try { await api.core.invoke("start_job", { action, request: { settings: config, edits, ...options } }); }
  catch (error) { setBusy(false); log(String(error), true); }
}

document.querySelectorAll("[data-action]").forEach((button) => {
  button.addEventListener("click", () => run(button.dataset.action));
});
$("export-cancel").addEventListener("click", () => $("export-dialog").close());
$("export-ready").addEventListener("click", () => {
  $("export-dialog").close();
  run("export", { ready_only: true });
});
$("export-resume").addEventListener("click", () => {
  $("export-dialog").close();
  run("download");
});
function updateThemeButton() {
  const dark = document.documentElement.dataset.theme === "dark";
  $("theme-toggle").textContent = dark ? "Light mode" : "Dark mode";
  $("theme-toggle").setAttribute("aria-label", `Switch to ${dark ? "light" : "dark"} mode`);
  $("theme-toggle").setAttribute("aria-pressed", String(dark));
  $("theme-setting").value = dark ? "dark" : "light";
}
$("theme-toggle").addEventListener("click", () => {
  const theme = document.documentElement.dataset.theme === "dark" ? "light" : "dark";
  document.documentElement.dataset.theme = theme;
  localStorage.setItem("mount-studio-theme", theme);
  updateThemeButton();
});
updateThemeButton();
$("settings-toggle").addEventListener("click", () => {
  showPage("settings");
});
$("workspace-tab").addEventListener("click", () => showPage("workspace"));
$("settings-save").addEventListener("click", saveSettings);
for (const id of ["source_mode", "mapping_mode"]) $(id).addEventListener("change", syncSettings);
$("theme-setting").addEventListener("change", () => {
  document.documentElement.dataset.theme = $("theme-setting").value;
  localStorage.setItem("mount-studio-theme", $("theme-setting").value);
  updateThemeButton();
});
$("animations").addEventListener("change", () => {
  document.documentElement.dataset.motion = $("animations").checked ? "on" : "off";
});
document.querySelectorAll("[data-browse]").forEach((button) => button.addEventListener("click", async () => {
  try {
    const path = await api.core.invoke("pick_directory");
    if (path) $(button.dataset.browse).value = path;
    syncSettings();
  } catch (error) { log(String(error), true); }
}));
$("open-output").addEventListener("click", async () => {
  try { await api.core.invoke("open_output_folder", { path: project.last_export, settings: settings() }); }
  catch (error) { log(String(error), true); }
});
$("pause").addEventListener("click", async () => {
  try { await api.core.invoke("pause_job"); log("Pause requested. Waiting for the current file or archive."); }
  catch (error) { log(String(error), true); }
});
for (const id of ["search", "filter", "mount-type"]) $(id).addEventListener("input", () => {
  if (id === "mount-type") localStorage.setItem("mount-studio-type", $(id).value);
  page = 0; render();
});
$("previous").addEventListener("click", () => { page--; render(); });
$("next").addEventListener("click", () => { page++; render(); });
$("select-matched").addEventListener("click", () => {
  const included = new Set(filtered().filter((row) => !row.protected && row.source).map((row) => row.key));
  project.mounts.forEach((row) => { row.selected = included.has(row.key); });
  dirty = true; render();
});
$("clear").addEventListener("click", () => {
  filtered().forEach((row) => { row.selected = false; }); dirty = true; render();
});
window.addEventListener("beforeunload", (event) => {
  if (dirty || busy) { event.preventDefault(); event.returnValue = ""; }
});

async function initialize() {
  if (!api) { log("Open this interface through the Tauri desktop app.", true); setBusy(true); return; }
  let saved = {};
  try { saved = JSON.parse(localStorage.getItem("mount-studio-settings") || "{}"); } catch { /* Use defaults. */ }
  const defaults = await api.core.invoke("defaults");
  bundledSettings = defaults;
  settingKeys.forEach((key) => {
    const value = saved[key] ?? defaults[key] ?? "";
    if ($(key).type === "checkbox") $(key).checked = Boolean(value);
    else $(key).value = value;
  });
  if ($("mapping_mode").value === "bundled") $("mapping_file").value = defaults.mapping_file;
  componentKeys.forEach((key) => {
    $("components").append(node("dt", key), node("dd", defaults[key]));
  });
  syncSettings();
  document.documentElement.dataset.motion = $("animations").checked ? "on" : "off";
  const savedType = localStorage.getItem("mount-studio-type");
  if (["all", "flying", "ground", "water"].includes(savedType)) $("mount-type").value = savedType;
  else $("mount-type").value = $("default_mount_type").value;
  await api.event.listen("studio-event", ({ payload }) => {
    if (payload.type === "project") { project = payload.project; dirty = false; render(); }
    if (payload.type === "mount") {
      const index = project.mounts.findIndex((row) => row.key === payload.mount.key);
      if (index >= 0) project.mounts[index] = payload.mount;
      render();
    }
    if (payload.type === "progress") log(payload.message);
    if (payload.type === "error") log(payload.message, true);
    if (payload.type === "idle") setBusy(false);
  });
  render();
}
initialize().catch((error) => log(String(error), true));
