// ============================================================
//  UPDATE MANAGER — Version manifest polling + SILENT force update
//  ------------------------------------------------------------
//  Firebase deploy хийгдсэний дараа client автоматаар илрүүлж,
//  ЯМАР Ч modal/товчгүйгээр шууд хүчээр шинэчилнэ:
//     cache цэвэрлэх → SW unregister → hard reload
//
//  State machine:
//   1. Every 60s (+ tab focus) → fetch /version.json (cache: 'no-store')
//   2. remoteVersion !== currentVersion bol → forceUpdate() ШУУД
//   3. Reload давталтаас хамгаалах: тухайн remote хувилбарт нэг л
//      удаа reload хийнэ (sessionStorage guard)
// ============================================================

(function () {
  'use strict';

  // ---------- Config ----------
  const POLL_INTERVAL_MS = 60 * 1000;   // 1 минут
  const VERSION_URL      = '/version.json';
  const RELOADED_KEY     = '__update_reloaded_for'; // sessionStorage (loop guard)

  // ---------- State ----------
  let currentVersion = null;
  let pollTimer      = null;
  let updating       = false;

  // ---------- Helpers ----------
  function getCurrentVersion() {
    const meta = document.querySelector('meta[name="app-version"]');
    return meta ? meta.getAttribute('content') : null;
  }

  // ---------- Force update (silent) ----------
  async function forceUpdate(remoteVersion) {
    if (updating) return;
    updating = true;

    // Reload давталтаас хамгаалах: энэ remote хувилбарт нэг л удаа reload.
    try {
      if (sessionStorage.getItem(RELOADED_KEY) === remoteVersion) {
        console.warn('[update] already reloaded for', remoteVersion, '— meta/version.json зөрж байж магадгүй');
        return;
      }
      sessionStorage.setItem(RELOADED_KEY, remoteVersion);
    } catch {}

    // 1. Service worker-н бүх cache устгана
    if ('caches' in window) {
      try {
        const names = await caches.keys();
        await Promise.all(names.map(n => caches.delete(n)));
      } catch (e) { console.warn('[update] cache clear failed', e); }
    }

    // 2. SW unregister (шинэ SW суугах)
    if ('serviceWorker' in navigator) {
      try {
        const regs = await navigator.serviceWorker.getRegistrations();
        await Promise.all(regs.map(r => r.unregister()));
      } catch (e) { console.warn('[update] SW unregister failed', e); }
    }

    // 3. Hard reload — шууд, хэрэглэгчээс асуухгүй
    location.reload();
  }

  // ---------- Polling ----------
  async function checkForUpdate() {
    if (updating) return;
    try {
      const res = await fetch(VERSION_URL + '?t=' + Date.now(), { cache: 'no-store' });
      if (!res.ok) return;
      const data = await res.json();
      const remote = data.version;
      if (!remote || remote === currentVersion) return;
      forceUpdate(remote);
    } catch (e) {
      console.warn('[update] check failed', e);
    }
  }

  // ---------- Boot ----------
  function init() {
    currentVersion = getCurrentVersion();
    if (!currentVersion) {
      console.info('[update] no <meta name="app-version">, skipping');
      return;
    }
    // Эхний шалгалт (3 секундийн дараа — бусад инициализацад саад болохгүй)
    setTimeout(checkForUpdate, 3000);
    // Давтан шалгалт
    pollTimer = setInterval(checkForUpdate, POLL_INTERVAL_MS);

    // Tab-д фокус ирэх үед шалгах (хэрэглэгч бусад tab-аас буцсаны дараа)
    document.addEventListener('visibilitychange', function () {
      if (document.visibilityState === 'visible') checkForUpdate();
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
