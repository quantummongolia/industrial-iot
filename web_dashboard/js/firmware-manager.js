// ============================================================
//  FIRMWARE MANAGER — Remote OTA dashboard
//
//  Reads:
//    /firmware/teerem/latest  → release info written by GitHub Actions
//    /devices/{deviceId}      → per-device heartbeat + OTA status
//
//  Writes:
//    /commands/{deviceId}/pending → triggers ESP32 command stream
// ============================================================

(function () {
  const STALE_AFTER_SEC = 90;       // last_heartbeat хэдэн секундээс хойш "offline"
  const _devices = {};              // deviceId -> latest data
  let _latestRelease = null;

  function _now() { return Math.floor(Date.now() / 1000); }

  function _relTime(unixSec) {
    if (!unixSec) return "—";
    const d = _now() - unixSec;
    if (d < 60)     return d + "s ago";
    if (d < 3600)   return Math.floor(d/60) + "m ago";
    if (d < 86400)  return Math.floor(d/3600) + "h ago";
    return Math.floor(d/86400) + "d ago";
  }

  function _statusColor(dev) {
    if (!dev.last_heartbeat || _now() - dev.last_heartbeat > STALE_AFTER_SEC) {
      return { color: "text-text-dim", bg: "bg-text-faint", label: "offline" };
    }
    switch (dev.status) {
      case "rolled_back": return { color: "text-warn",  bg: "bg-warn",  label: "rolled back" };
      case "validating":  return { color: "text-accent",bg: "bg-accent",label: "validating" };
      case "running":     return { color: "text-success",bg:"bg-success",label: "running" };
      default:            return { color: "text-text-soft", bg: "bg-text-soft", label: dev.status || "?" };
    }
  }

  function _otaStageRow(dev) {
    const s = dev.ota_status;
    if (!s || !s.stage) return "";
    const stageBadge = s.stage === "failed" ? "bg-danger/20 text-danger"
                     : s.stage === "rebooting" || s.stage === "downloading"
                       ? "bg-accent/20 text-accent"
                       : "bg-success/20 text-success";
    const pct = Math.max(0, Math.min(100, s.progress || 0));
    return `
      <div class="mt-3 pt-3 border-t border-border-base">
        <div class="flex items-center justify-between text-[11px] mb-1.5">
          <span class="px-2 py-0.5 rounded-full font-semibold uppercase tracking-wider ${stageBadge}">
            ${s.stage}
          </span>
          <span class="text-text-dim tabular-nums">${pct}%</span>
        </div>
        <div class="h-1 rounded-full bg-bg-card overflow-hidden">
          <div class="h-full bg-accent transition-all duration-300" style="width:${pct}%"></div>
        </div>
        <div class="text-[11px] text-text-soft mt-1.5 truncate" title="${s.message || ""}">${s.message || ""}</div>
      </div>`;
  }

  function _deviceCard(deviceId, dev) {
    const st = _statusColor(dev);
    const isLatest = _latestRelease && dev.firmware === _latestRelease.version;
    // Offline шалгалтыг хийхгүй — команд /pending-д хадгалагдана, device онлайн
    // болсон даруйд (эсвэл аль хэдийн онлайн байгаа бол шууд) татаж авна.
    const canUpdate = _latestRelease && !isLatest;
    return `
      <div class="glass-card rounded-DEFAULT p-5" data-device="${deviceId}">
        <div class="flex items-start justify-between gap-3 mb-3">
          <div class="min-w-0">
            <div class="text-[11px] text-text-dim font-mono truncate">${deviceId}</div>
            <div class="text-[18px] font-bold text-text-primary tabular-nums mt-0.5">
              v${dev.firmware || "?"}
              ${isLatest ? '<span class="text-[11px] text-success ml-1 font-medium">latest</span>' : ""}
            </div>
          </div>
          <div class="flex items-center gap-1.5 shrink-0 text-[11px] ${st.color} font-medium">
            <div class="w-1.5 h-1.5 rounded-full ${st.bg}"></div>
            ${st.label}
          </div>
        </div>

        <div class="grid grid-cols-3 gap-2 text-[11px] mb-3">
          <div>
            <div class="text-text-dim">Last seen</div>
            <div class="text-text-soft font-medium">${_relTime(dev.last_heartbeat)}</div>
          </div>
          <div>
            <div class="text-text-dim">Heap</div>
            <div class="text-text-soft font-medium tabular-nums">${dev.free_heap ? Math.round(dev.free_heap/1024)+" KB" : "—"}</div>
          </div>
          <div>
            <div class="text-text-dim">RSSI</div>
            <div class="text-text-soft font-medium tabular-nums">${dev.rssi || "—"} dBm</div>
          </div>
        </div>

        <div class="flex items-center justify-between gap-2">
          <div class="text-[10px] text-text-dim font-mono">${dev.partition || "?"} · ${dev.reset_reason || "—"}</div>
          <div class="flex gap-1.5">
            <button class="fw-btn-ping px-2.5 py-1 text-[11px] rounded-md bg-bg-card text-text-soft hover:bg-bg-elevated transition" data-device="${deviceId}">Ping</button>
            <button class="fw-btn-reboot px-2.5 py-1 text-[11px] rounded-md bg-bg-card text-text-soft hover:bg-warn/20 hover:text-warn transition" data-device="${deviceId}">Reboot</button>
            <button class="fw-btn-update px-3 py-1 text-[11px] rounded-md font-semibold transition ${
              canUpdate
                ? "bg-accent text-white hover:bg-accent/90"
                : "bg-bg-card text-text-dim cursor-not-allowed"
            }" data-device="${deviceId}" ${canUpdate ? "" : "disabled"}>Update</button>
          </div>
        </div>

        ${_otaStageRow(dev)}
      </div>`;
  }

  function _render() {
    const container = document.getElementById("fwDeviceList");
    if (!container) return;

    const entries = Object.entries(_devices);
    if (entries.length === 0) {
      container.innerHTML = `<div class="glass-card rounded-DEFAULT p-6 text-center text-text-soft text-sm">Device олдсонгүй.</div>`;
    } else {
      container.innerHTML = entries
        .sort(([a], [b]) => a.localeCompare(b))
        .map(([id, d]) => _deviceCard(id, d))
        .join("");
    }

    // Latest version header
    const verEl  = document.getElementById("fwLatestVersion");
    const metaEl = document.getElementById("fwLatestMeta");
    if (_latestRelease) {
      verEl.textContent = "v" + _latestRelease.version;
      metaEl.textContent = `${_relTime(_latestRelease.published_at)} · ${Math.round((_latestRelease.size || 0)/1024)} KB`;
    } else {
      verEl.textContent = "—";
      metaEl.textContent = "Release үүсээгүй байна (GitHub-д tag push хий)";
    }

    // On-latest counter
    const onLatest = _latestRelease
      ? entries.filter(([, d]) => d.firmware === _latestRelease.version).length
      : 0;
    document.getElementById("fwDeviceOnLatest").textContent = onLatest;
    document.getElementById("fwDeviceTotal").textContent    = entries.length;
  }

  function _wireButtons() {
    const container = document.getElementById("fwDeviceList");
    if (!container || container._wired) return;
    container._wired = true;
    container.addEventListener("click", (e) => {
      const btn = e.target.closest("button[data-device]");
      if (!btn) return;
      const id = btn.dataset.device;
      if (btn.classList.contains("fw-btn-ping"))   _pushCommand(id, "ping");
      if (btn.classList.contains("fw-btn-reboot")) {
        if (confirm(`${id} reboot хийх үү?`)) _pushCommand(id, "reboot");
      }
      if (btn.classList.contains("fw-btn-update")) {
        if (!_latestRelease) return;
        if (confirm(`${id} → v${_latestRelease.version} update хийх үү?`))
          _pushCommand(id, "update", {
            version: _latestRelease.version,
            url:     _latestRelease.url,
            sha256:  _latestRelease.sha256,
          });
      }
    });
  }

  function _pushCommand(deviceId, action, params = {}) {
    if (typeof firebase === "undefined" || !firebase.apps?.length) return;
    const db = firebase.database();
    const cmdId = `cmd_${Date.now()}_${Math.random().toString(36).slice(2, 8)}`;
    const user = firebase.auth().currentUser;
    const payload = {
      id:        cmdId,
      action,
      issued_at: _now(),
      issued_by: user?.email || "unknown",
      ...params,
    };
    db.ref(`/commands/${deviceId}/pending`).set(payload)
      .then(() => {
        if (typeof showToast === "function") showToast(`${action} → ${deviceId}`, "info");
      })
      .catch((err) => {
        if (typeof showToast === "function") showToast(`Алдаа: ${err.message}`, "error");
      });
  }

  function initFirmwareManager() {
    if (typeof firebase === "undefined" || !firebase.apps?.length) return;
    const db = firebase.database();

    db.ref("/firmware/teerem/latest").on("value", (snap) => {
      _latestRelease = snap.val();
      _render();
    });

    db.ref("/devices").on("value", (snap) => {
      const val = snap.val() || {};
      Object.keys(_devices).forEach((k) => { if (!val[k]) delete _devices[k]; });
      Object.entries(val).forEach(([id, data]) => { _devices[id] = data; });
      _render();
      _wireButtons();
    });

    // Realtime тоо minute тутамд "_relTime"-г шинэчилнэ
    setInterval(_render, 30000);
  }

  // Auth бэлэн болсны дараа эхлүүлнэ. auth.js firebase.auth().onAuthStateChanged-тэй
  // зэрэгцэн ажиллаж онгой devices node-руу хоосон уншихаас сэргийлнэ.
  function _waitForAuthThenInit() {
    if (typeof firebase === "undefined" || !firebase.apps?.length) {
      setTimeout(_waitForAuthThenInit, 200);
      return;
    }
    firebase.auth().onAuthStateChanged((u) => {
      if (u) initFirmwareManager();
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", _waitForAuthThenInit);
  } else {
    _waitForAuthThenInit();
  }
})();
