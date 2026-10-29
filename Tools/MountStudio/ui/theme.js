"use strict";
try {
  document.documentElement.dataset.theme = localStorage.getItem("mount-studio-theme") || "dark";
  const settings = JSON.parse(localStorage.getItem("mount-studio-settings") || "{}");
  document.documentElement.dataset.motion = settings.animations === false ? "off" : "on";
} catch {
  document.documentElement.dataset.theme = "dark";
}
