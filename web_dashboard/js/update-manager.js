// ============================================================
//  UPDATE MANAGER — Version manifest polling + update modal
//  ------------------------------------------------------------
//  Firebase deploy хийгдсэний дараа client автоматаар илрүүлж,
//  хэрэглэгчид modal харуулна:
//     [ Шинэчлэх ]   [ Дараа ]
//
//  State machine:
//   1. Every 60s → fetch /version.json (cache: 'no-store')
//   2. remoteVersion !== currentVersion bol → showModal()
//   3. "Шинэчлэх" → hard reload + SW unregister
//   4. "Дараа"    → snooze 1 цаг, modal хаана
//   5. Шинэ хувилбар илэрсэнээс 24+ цаг болсон бол →
//      зөвхөн "Шинэчлэх" товчтой force mode
// ============================================================

(function () {
  'use strict';

  // ---------- Config ----------
  const POLL_INTERVAL_MS   = 60 * 1000;         // 1 минут
  const SNOOZE_MS          = 60 * 60 * 1000;    // 1 цаг
  const FORCE_AFTER_MS     = 24 * 60 * 60 * 1000; // 24 цаг
  const VERSION_URL        = '/version.json';
  const SNOOZE_KEY         = '__update_snoozed_until';
  const FIRST_SEEN_KEY     = '__update_first_seen';

  // ---------- State ----------
  let currentVersion = null;
  let pollTimer      = null;
  let modalShown     = false;

  // ---------- Helpers ----------
  function getCurrentVersion() {
    const meta = document.querySelector('meta[name="app-version"]');
    return meta ? meta.getAttribute('content') : null;
  }

  function now() { return Date.now(); }

  function getSnoozedUntil() {
    const v = parseInt(localStorage.getItem(SNOOZE_KEY) || '0', 10);
    return isNaN(v) ? 0 : v;
  }

  function setSnoozedUntil(ts) {
    localStorage.setItem(SNOOZE_KEY, String(ts));
  }

  function clearSnooze() {
    localStorage.removeItem(SNOOZE_KEY);
    localStorage.removeItem(FIRST_SEEN_KEY);
  }

  function getFirstSeen(remoteVersion) {
    const key = FIRST_SEEN_KEY;
    const stored = localStorage.getItem(key);
    if (stored) {
      try {
        const obj = JSON.parse(stored);
        if (obj.version === remoteVersion) return obj.at;
      } catch {}
    }
    const ts = now();
    localStorage.setItem(key, JSON.stringify({ version: remoteVersion, at: ts }));
    return ts;
  }

  function isForceMode(remoteVersion) {
    const firstSeen = getFirstSeen(remoteVersion);
    return (now() - firstSeen) >= FORCE_AFTER_MS;
  }

  // ---------- Modal UI ----------
  function ensureModal() {
    if (document.getElementById('updateModal')) return;

    const modal = document.createElement('div');
    modal.id = 'updateModal';
    modal.style.cssText = `
      position: fixed; inset: 0; z-index: 99999;
      display: none; align-items: center; justify-content: center;
      padding: 16px;
    `;
    modal.innerHTML = `
      <div id="updateModalBackdrop" style="
        position: absolute; inset: 0;
        background: rgba(0, 0, 0, 0.5);
        backdrop-filter: blur(12px);
        -webkit-backdrop-filter: blur(12px);
      "></div>
      <div style="
        position: relative; z-index: 1;
        max-width: 400px; width: 100%;
        background: var(--color-bg-elevated, #1f1f25);
        border: 1px solid var(--color-border-strong, rgba(255,255,255,0.08));
        border-radius: 16px;
        padding: 28px 24px;
        box-shadow: 0 24px 48px rgba(0,0,0,0.4);
        text-align: center;
        font-family: 'Inter', -apple-system, BlinkMacSystemFont, sans-serif;
      ">
        <div style="
          width: 56px; height: 56px; margin: 0 auto 16px;
          border-radius: 50%;
          background: rgba(97, 149, 255, 0.12);
          display: flex; align-items: center; justify-content: center;
          color: var(--color-accent, #6195ff);
        ">
          <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M21 12a9 9 0 1 1-3-6.7L21 8"/>
            <polyline points="21 3 21 8 16 8"/>
          </svg>
        </div>
        <h3 style="
          font-size: 18px; font-weight: 700;
          color: var(--color-text-primary, #f5f5f7);
          margin: 0 0 8px;
        ">Шинэ хувилбар бэлэн</h3>
        <p style="
          font-size: 13px; line-height: 1.5;
          color: var(--color-text-soft, #a1a1aa);
          margin: 0 0 24px;
        " id="updateModalSubtitle">Сайжруулалт, алдаа засвар бэлэн болсон. Шинэчлэхэд хэдхэн секунд болно.</p>
        <div style="display: flex; gap: 8px;" id="updateModalButtons">
          <button id="updateModalLater" style="
            flex: 1; padding: 11px 16px;
            background: transparent;
            border: 1px solid var(--color-border-strong, rgba(255,255,255,0.08));
            color: var(--color-text-soft, #a1a1aa);
            border-radius: 10px;
            font-size: 13px; font-weight: 500; font-family: inherit;
            cursor: pointer; transition: background 0.15s;
          ">Дараа</button>
          <button id="updateModalRefresh" style="
            flex: 1; padding: 11px 16px;
            background: var(--color-accent, #6195ff);
            border: none;
            color: #fff;
            border-radius: 10px;
            font-size: 13px; font-weight: 600; font-family: inherit;
            cursor: pointer; transition: filter 0.15s;
          ">Шинэчлэх</button>
        </div>
      </div>
    `;
    document.body.appendChild(modal);

    document.getElementById('updateModalRefresh').onclick = doUpdate;
    document.getElementById('updateModalLater').onclick   = doSnooze;
  }

  function showModal(remoteVersion) {
    if (modalShown) return;
    ensureModal();
    const modal = document.getElementById('updateModal');
    modal.style.display = 'flex';
    modalShown = true;

    // 24+ цаг өнгөрсөн бол "Дараа" товчийг нуунa
    if (isForceMode(remoteVersion)) {
      const laterBtn = document.getElementById('updateModalLater');
      if (laterBtn) laterBtn.style.display = 'none';
      const subtitle = document.getElementById('updateModalSubtitle');
      if (subtitle) subtitle.textContent = 'Энэ хувилбарыг үргэлжлүүлэх боломжгүй. Шинэчилнэ үү.';
    }
  }

  function hideModal() {
    const modal = document.getElementById('updateModal');
    if (modal) modal.style.display = 'none';
    modalShown = false;
  }

  // ---------- Actions ----------
  async function doUpdate() {
    clearSnooze();

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

    // 3. Hard reload
    location.reload();
  }

  function doSnooze() {
    setSnoozedUntil(now() + SNOOZE_MS);
    hideModal();
  }

  // ---------- Polling ----------
  async function checkForUpdate() {
    try {
      const res = await fetch(VERSION_URL + '?t=' + now(), { cache: 'no-store' });
      if (!res.ok) return;
      const data = await res.json();
      const remote = data.version;
      if (!remote || remote === currentVersion) return;

      // Шинэ хувилбар илэрсэн
      const snoozedUntil = getSnoozedUntil();
      if (now() < snoozedUntil && !isForceMode(remote)) {
        // Хэрэглэгч snooze хийсэн, force mode ч биш
        return;
      }
      showModal(remote);
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

    // Tab-д fokus ирэх үед шалгах (хэрэглэгч бусад tab-аас буцсаны дараа)
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
