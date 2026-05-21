// ============================================================
//  FIRMWARE MANAGER — Remote OTA dashboard
//
//  Reads:
//    /firmware/teerem/latest    → release info written by teerem CI
//    /firmware/flowmeter/latest → release info written by flowmeter CI
//    /devices/{deviceId}        → per-device heartbeat + OTA status
//
//  Writes:
//    /commands/{deviceId}/pending → triggers ESP32 command stream
//
//  Multi-family logic:
//    Device бүр `family` талбартай heartbeat бичнэ ("teerem" эсвэл "flowmeter").
//    Хуучин heartbeat-уудтай (family-гүй) тохиолдолд deviceId prefix-аас гаргана.
//    Дашбоард device бүрд яг өөрийн family-ийн latest release-тэй харьцуулна.
// ============================================================

(function () {
  const STALE_AFTER_SEC = 90;       // last_heartbeat хэдэн секундээс хойш "offline"
  const KNOWN_FAMILIES = ["teerem", "flowmeter"];
  const _devices = {};              // deviceId -> latest data
  const _latestReleases = { teerem: null, flowmeter: null };

  function _now() { return Math.floor(Date.now() / 1000); }

  // Device firmware string-ийг release version-той харьцуулахдаа `-g<sha>` суффиксийг хасна.
  // CI release нь зөвхөн "MAJOR.MINOR.PATCH" (жишээ нь "0.0.1") бичдэг, харин ESP-ийн
  // firmware string нь git describe-аас "0.0.1-gacb8bac" гэх мэт SHA-тай ирдэг.
  function _baseVersion(s) {
    if (!s) return "";
    const i = s.indexOf("-g");
    return i >= 0 ? s.slice(0, i) : s;
  }

  // Firebase server timestamp нь millisecond, manual NTP-аас ирсэн нь second.
  // Зөв нэгжид auto-detect: ms бол > 1e12 (≈ 2001 он seconds-д = 1e9).
  function _toSeconds(unix) {
    if (!unix) return 0;
    return unix > 1e12 ? Math.floor(unix / 1000) : unix;
  }

  function _relTime(unix) {
    const sec = _toSeconds(unix);
    if (!sec) return "—";
    const d = _now() - sec;
    if (d < 0)       return "just now";  // server timestamp нь browser-аас илүү хурдан байж магадгүй
    if (d < 60)      return d + "s ago";
    if (d < 3600)    return Math.floor(d/60) + "m ago";
    if (d < 86400)   return Math.floor(d/3600) + "h ago";
    return Math.floor(d/86400) + "d ago";
  }

  // Device-ийн family-г заавал баталгаажуулна:
  //   1) heartbeat-д `family` field байгаа бол түүнийг
  //   2) Эс бөгөөс deviceId prefix-аас (teerem_xxx / flowmeter_xxx)
  //   3) Аль нь ч таарахгүй бол null (release-тэй харьцуулахгүй)
  function _resolveFamily(deviceId, dev) {
    if (dev && KNOWN_FAMILIES.includes(dev.family)) return dev.family;
    for (const fam of KNOWN_FAMILIES) {
      if (deviceId.startsWith(fam + "_")) return fam;
    }
    return null;
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
    const family = _resolveFamily(deviceId, dev);
    const release = family ? _latestReleases[family] : null;
    const isLatest = release && _baseVersion(dev.firmware) === release.version;
    // Offline шалгалтыг хийхгүй — команд /pending-д хадгалагдана, device онлайн
    // болсон даруйд (эсвэл аль хэдийн онлайн байгаа бол шууд) татаж авна.
    const canUpdate = release && !isLatest;
    const familyChip = family
      ? `<span class="text-[10px] text-text-dim font-medium uppercase tracking-wider ml-1">${family}</span>`
      : "";
    return `
      <div class="glass-card rounded-DEFAULT p-5" data-device="${deviceId}">
        <div class="flex items-start justify-between gap-3 mb-3">
          <div class="min-w-0">
            <div class="text-[11px] text-text-dim font-mono truncate">${deviceId}${familyChip}</div>
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

  // Header release card — хоёр family-ийн latest version-ыг нэг card-д харуулна.
  function _renderHeader() {
    const verEl  = document.getElementById("fwLatestVersion");
    const metaEl = document.getElementById("fwLatestMeta");
    if (!verEl || !metaEl) return;

    const parts = [];
    const metaParts = [];
    for (const fam of KNOWN_FAMILIES) {
      const r = _latestReleases[fam];
      if (r) {
        parts.push(`<span class="capitalize">${fam}</span> <span class="text-accent">v${r.version}</span>`);
        metaParts.push(`${fam}: ${_relTime(r.published_at)} · ${Math.round((r.size || 0)/1024)} KB`);
      } else {
        parts.push(`<span class="capitalize text-text-dim">${fam}</span> <span class="text-text-dim">—</span>`);
      }
    }
    verEl.innerHTML = parts.join('<span class="text-text-dim mx-2">·</span>');
    metaEl.textContent = metaParts.length ? metaParts.join("  ·  ") : "Release үүсээгүй байна (GitHub-д tag push хий)";
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

    _renderHeader();

    // On-latest counter — device бүрд өөрийн family-ийн latest-тай тулгана.
    let onLatest = 0;
    for (const [id, d] of entries) {
      const fam = _resolveFamily(id, d);
      const release = fam ? _latestReleases[fam] : null;
      if (release && _baseVersion(d.firmware) === release.version) onLatest++;
    }
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
      const dev = _devices[id];
      if (btn.classList.contains("fw-btn-ping"))   _pushCommand(id, "ping");
      if (btn.classList.contains("fw-btn-reboot")) {
        if (confirm(`${id} reboot хийх үү?`)) _pushCommand(id, "reboot");
      }
      if (btn.classList.contains("fw-btn-update")) {
        const fam = _resolveFamily(id, dev);
        const release = fam ? _latestReleases[fam] : null;
        if (!release) return;
        if (confirm(`${id} → v${release.version} update хийх үү?`))
          _pushCommand(id, "update", {
            version: release.version,
            url:     release.url,
            sha256:  release.sha256,
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

    // Хоёр family-ийн release feed-ийг тус бүрд нь сонсоно.
    for (const fam of KNOWN_FAMILIES) {
      db.ref(`/firmware/${fam}/latest`).on("value", (snap) => {
        _latestReleases[fam] = snap.val();
        _render();
      });
    }

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
