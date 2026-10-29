#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde_json::{json, Value};
use std::io::{BufRead, BufReader, Write};
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::sync::Mutex;
use tauri::{Emitter, Manager};
use tauri_plugin_dialog::DialogExt;

#[derive(Default)]
struct Job(Mutex<Option<PathBuf>>);

fn resource_root(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    if cfg!(debug_assertions) {
        Ok(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join(".."))
    } else {
        app.path().resource_dir().map_err(|e| e.to_string())
    }
}

fn detect_retail() -> String {
    let mut candidates = vec![
        PathBuf::from("D:/Blizzard/World of Warcraft"),
        PathBuf::from("C:/Program Files (x86)/World of Warcraft"),
        PathBuf::from("C:/Program Files/World of Warcraft"),
        PathBuf::from("D:/Games/World of Warcraft"),
    ];
    #[cfg(windows)]
    for key in [
        "HKLM\\SOFTWARE\\WOW6432Node\\Blizzard Entertainment\\World of Warcraft",
        "HKLM\\SOFTWARE\\Blizzard Entertainment\\World of Warcraft",
    ] {
        use std::os::windows::process::CommandExt;
        if let Ok(output) = Command::new("reg.exe")
            .args(["query", key, "/v", "InstallPath"])
            .creation_flags(0x08000000)
            .output()
        {
            for line in String::from_utf8_lossy(&output.stdout).lines() {
                if let Some((_, path)) = line.split_once("REG_SZ") {
                    candidates.insert(0, PathBuf::from(path.trim()));
                }
            }
        }
    }
    for path in candidates {
        for root in [path.clone(), path.parent().unwrap_or(&path).to_path_buf()] {
            if let Ok(info) = std::fs::read_to_string(root.join(".build.info")) {
                let mut rows = info.lines();
                let columns: Vec<_> = rows
                    .next()
                    .unwrap_or("")
                    .split('|')
                    .map(|field| field.split('!').next().unwrap_or(""))
                    .collect();
                if let Some(product) = columns.iter().position(|name| *name == "Product") {
                    if rows.any(|row| row.split('|').nth(product) == Some("wow")) {
                        return root.to_string_lossy().into_owned();
                    }
                }
            }
        }
    }
    String::new()
}

#[tauri::command]
fn defaults(app: tauri::AppHandle) -> Result<Value, String> {
    let runtime = resource_root(&app)?.join("runtime");
    let data = app.path().app_local_data_dir().map_err(|e| e.to_string())?;
    let client = if PathBuf::from("G:/3.3.5a - Dev/Data").is_dir() {
        "G:/3.3.5a - Dev"
    } else {
        ""
    };
    let ini = std::fs::read_to_string(runtime.join("config/RetailCasc.ini"))
        .map_err(|e| e.to_string())?;
    let build = ini
        .lines()
        .find_map(|line| line.strip_prefix("build="))
        .unwrap_or("");
    Ok(json!({
        "client": client,
        "work": data.join("Projects/default"),
        "python": runtime.join("python/python.exe"),
        "probe": runtime.join("backend/RetailAssetProbe.exe"),
        "storm": runtime.join("backend/StormLib.dll"),
        "reader": runtime.join("reader"),
        "definitions": runtime.join("definitions"),
        "source_config": runtime.join("config/RetailCasc.ini"),
        "source_mode": "local", "retail_root": detect_retail(),
        "cdn_build": build, "cdn_region": "us", "source_locale": "2",
        "client_config": "", "keys_file": "", "locale": "enUS",
        "mapping_mode": "bundled", "mapping_file": runtime.join("mount-models.json"),
        "database_container": "ac-database", "export_dir": "", "auto_open_export": true,
        "download_timeout": 600, "archive_limit_mb": 1800, "default_mount_type": "flying", "animations": true
    }))
}

#[tauri::command]
async fn pick_directory(app: tauri::AppHandle) -> Result<Option<String>, String> {
    let picked =
        tauri::async_runtime::spawn_blocking(move || app.dialog().file().blocking_pick_folder())
            .await
            .map_err(|e| e.to_string())?;
    picked
        .map(|path| {
            path.into_path()
                .map(|path| path.to_string_lossy().into_owned())
                .map_err(|e| e.to_string())
        })
        .transpose()
}

fn export_folder(path: &str, settings: &Value) -> Result<PathBuf, String> {
    let work = settings["work"]
        .as_str()
        .ok_or("Project directory is missing")?;
    let export = settings["export_dir"].as_str().unwrap_or("");
    let root = if export.is_empty() {
        PathBuf::from(work).join("exports")
    } else {
        PathBuf::from(export)
    };
    let root = root.canonicalize().map_err(|e| e.to_string())?;
    let folder = PathBuf::from(path)
        .canonicalize()
        .map_err(|e| e.to_string())?;
    if folder.parent() != Some(root.as_path()) || !folder.join("receipt.json").is_file() {
        return Err(
            "The folder is not a verified export in this project's output directory".into(),
        );
    }
    Ok(folder)
}

#[tauri::command]
fn open_output_folder(path: String, settings: Value) -> Result<(), String> {
    let folder = export_folder(&path, &settings)?;
    let folder_text = folder.to_string_lossy();
    let explorer_path = if let Some(unc) = folder_text.strip_prefix(r"\\?\UNC\") {
        format!(r"\\{unc}")
    } else {
        folder_text
            .strip_prefix(r"\\?\")
            .unwrap_or(&folder_text)
            .to_owned()
    };
    Command::new("explorer.exe")
        .arg(explorer_path)
        .spawn()
        .map_err(|e| e.to_string())?;
    Ok(())
}

#[tauri::command]
fn pause_job(job: tauri::State<Job>) -> Result<(), String> {
    let current = job.0.lock().map_err(|e| e.to_string())?;
    let path = current.as_ref().ok_or("No running job")?;
    std::fs::write(path, b"pause").map_err(|e| e.to_string())
}

#[tauri::command]
fn start_job(app: tauri::AppHandle, action: String, mut request: Value) -> Result<(), String> {
    if ![
        "scan", "catalog", "save", "download", "export", "load", "doctor",
    ]
    .contains(&action.as_str())
    {
        return Err("Unknown operation".into());
    }
    let bundled = defaults(app.clone())?;
    for key in [
        "python",
        "probe",
        "storm",
        "reader",
        "definitions",
        "source_config",
    ] {
        request["settings"][key] = bundled[key].clone();
    }
    if request["settings"]["mapping_mode"] == "bundled" {
        request["settings"]["mapping_file"] = bundled["mapping_file"].clone();
    }
    let work = request["settings"]["work"]
        .as_str()
        .ok_or("Work directory is required")?;
    let path = PathBuf::from(work);
    if !path.is_absolute() {
        return Err("Work directory must be absolute".into());
    }
    let client = request["settings"]["client"]
        .as_str()
        .ok_or("Client directory is required")?;
    let normalize = |value: &str| value.replace('/', "\\").to_lowercase();
    let normalized_work = normalize(work);
    let normalized_client = normalize(client);
    if normalized_work == normalized_client
        || normalized_work.starts_with(&(normalized_client + "\\"))
    {
        return Err("Work directory must be outside the client".into());
    }
    let worker = resource_root(&app)?.join("worker.py");
    let python = request["settings"]["python"]
        .as_str()
        .unwrap_or("python")
        .to_owned();
    let pause = path.join("pause.request");
    {
        let job = app.state::<Job>();
        let mut current = job.0.lock().map_err(|e| e.to_string())?;
        if current.is_some() {
            return Err("Wait for the current operation to finish or pause".into());
        }
        *current = Some(pause.clone());
    }
    std::thread::spawn(move || {
        let run = || -> Result<(), String> {
            let mut command = Command::new(python);
            command.arg("-B").arg("-u").arg(worker).arg(action);
            command
                .stdin(Stdio::piped())
                .stdout(Stdio::piped())
                .stderr(Stdio::piped());
            #[cfg(windows)]
            {
                use std::os::windows::process::CommandExt;
                command.creation_flags(0x08000000);
            }
            let mut child = command
                .spawn()
                .map_err(|e| format!("Cannot start Python worker: {e}"))?;
            let mut input = child.stdin.take().ok_or("Worker stdin unavailable")?;
            input
                .write_all(request.to_string().as_bytes())
                .map_err(|e| e.to_string())?;
            drop(input);
            let errors = child.stderr.take().ok_or("Worker stderr unavailable")?;
            let error_reader = std::thread::spawn(move || {
                let mut tail = String::new();
                for line in BufReader::new(errors).lines().map_while(Result::ok) {
                    tail.push_str(&line);
                    tail.push('\n');
                    if tail.len() > 16000 {
                        tail = tail
                            .chars()
                            .rev()
                            .take(8000)
                            .collect::<String>()
                            .chars()
                            .rev()
                            .collect();
                    }
                }
                tail
            });
            let output = child.stdout.take().ok_or("Worker stdout unavailable")?;
            let mut reported_error = false;
            for line in BufReader::new(output).lines() {
                let line = line.map_err(|e| e.to_string())?;
                if let Ok(event) = serde_json::from_str::<Value>(&line) {
                    reported_error |= event["type"] == "error";
                    if event["type"] == "exported"
                        && request["settings"]["auto_open_export"] != false
                    {
                        if let Some(path) = event["path"].as_str() {
                            if let Err(error) =
                                open_output_folder(path.to_owned(), request["settings"].clone())
                            {
                                let _ = app.emit("studio-event", json!({"type": "progress",
                                    "message": format!("Export complete; could not open folder: {error}")}));
                            }
                        }
                    }
                    let _ = app.emit("studio-event", event);
                }
            }
            let status = child.wait().map_err(|e| e.to_string())?;
            let errors = error_reader.join().unwrap_or_default();
            if !status.success() && !reported_error {
                return Err(format!("Worker failed ({status}): {errors}"));
            }
            Ok(())
        };
        if let Err(error) = run() {
            let _ = app.emit("studio-event", json!({"type": "error", "message": error}));
        }
        let job = app.state::<Job>();
        if let Ok(mut current) = job.0.lock() {
            *current = None;
        }
        let _ = app.emit("studio-event", json!({"type": "idle"}));
    });
    Ok(())
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(Job::default())
        .invoke_handler(tauri::generate_handler![
            defaults,
            start_job,
            pause_job,
            pick_directory,
            open_output_folder
        ])
        .run(tauri::generate_context!())
        .expect("Cannot run Esteria Mount Studio");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_verified_exports_inside_the_configured_output_can_open() {
        let stamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let work =
            std::env::temp_dir().join(format!("mountstudio-folder-{}-{stamp}", std::process::id()));
        let folder = work.join("exports/one");
        std::fs::create_dir_all(&folder).unwrap();
        let settings = json!({"work": work, "export_dir": ""});
        assert!(export_folder(folder.to_str().unwrap(), &settings).is_err());
        std::fs::write(folder.join("receipt.json"), "{}").unwrap();
        assert_eq!(
            export_folder(folder.to_str().unwrap(), &settings).unwrap(),
            folder.canonicalize().unwrap()
        );
        assert!(export_folder(work.to_str().unwrap(), &settings).is_err());
        let other = json!({"work": work, "export_dir": work});
        assert!(export_folder(folder.to_str().unwrap(), &other).is_err());
        std::fs::remove_file(folder.join("receipt.json")).unwrap();
        std::fs::remove_dir(&folder).unwrap();
        std::fs::remove_dir(work.join("exports")).unwrap();
        std::fs::remove_dir(&work).unwrap();
    }
}
