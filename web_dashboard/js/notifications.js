// ============================================================
//  NOTIFICATIONS — баруун дээд буланд гардаг dropdown панель
//  ------------------------------------------------------------
//  Public API:
//    Notifications.push({ title, message, type, time })
//      type: 'info' | 'success' | 'warning' | 'error'  (default: 'info')
//      time: Date.now() (default: одоо)
//    Notifications.clear()           — бүгдийг устгана
//    Notifications.markAllRead()     — улаан цэгийг арилгана
//
//  toggleNotifPanel(force?)          — bell товчны onclick-ээс дуудагдана
//
//  Хадгалалт: session дотор санах ой (refresh-д устана).
// ============================================================

(function () {
  'use strict';

  // ---------- State ----------
  var items = [];  // [{ id, title, message, type, time, read }]
  var nextId = 1;

  // ---------- DOM helpers ----------
  function $(id) { return document.getElementById(id); }

  function isOpen() {
    var p = $('notifPanel');
    return p && !p.classList.contains('hidden');
  }

  function openPanel() {
    var p = $('notifPanel');
    if (!p) return;
    p.classList.remove('hidden');
    markAllRead();
  }

  function closePanel() {
    var p = $('notifPanel');
    if (p) p.classList.add('hidden');
  }

  // ---------- State mutators ----------
  function push(opts) {
    opts = opts || {};
    var item = {
      id:      nextId++,
      title:   opts.title   || '',
      message: opts.message || '',
      type:    opts.type    || 'info',
      time:    opts.time    || Date.now(),
      read:    false,
    };
    items.unshift(item); // хамгийн шинэ нь дээр
    renderList();
    updateDot();

    // Toast: app нээлттэй үед хэрэглэгчид шууд харагдана
    if (typeof window.showToast === 'function') {
      var toastMsg = item.title
        ? (item.title + (item.message ? ' — ' + item.message : ''))
        : item.message;
      window.showToast(toastMsg, item.type === 'error' ? 'error' : item.type);
    }
    return item.id;
  }

  function clear() {
    items = [];
    renderList();
    updateDot();
  }

  function markAllRead() {
    var changed = false;
    for (var i = 0; i < items.length; i++) {
      if (!items[i].read) { items[i].read = true; changed = true; }
    }
    if (changed) {
      renderList();
      updateDot();
    }
  }

  function unreadCount() {
    var n = 0;
    for (var i = 0; i < items.length; i++) if (!items[i].read) n++;
    return n;
  }

  // ---------- Rendering ----------
  function updateDot() {
    var dot = $('notifDot');
    if (!dot) return;
    if (unreadCount() > 0) {
      dot.classList.remove('hidden');
    } else {
      dot.classList.add('hidden');
    }
  }

  // Харьцангуй цаг ("саяхан", "2 мин өмнө", "1 цаг өмнө", ...)
  function relTime(ts) {
    var diff = Math.max(0, Date.now() - ts);
    var sec = Math.floor(diff / 1000);
    if (sec < 30)         return 'саяхан';
    if (sec < 60)         return sec + ' сек өмнө';
    var min = Math.floor(sec / 60);
    if (min < 60)         return min + ' мин өмнө';
    var hr = Math.floor(min / 60);
    if (hr < 24)          return hr + ' цаг өмнө';
    var day = Math.floor(hr / 24);
    if (day < 7)          return day + ' өдрийн өмнө';
    var d = new Date(ts);
    return d.toLocaleDateString();
  }

  // Indicator dot өнгө (type-аар)
  var TYPE_DOT = {
    info:    'background:var(--color-accent,#6195ff);',
    success: 'background:var(--color-success,#22c55e);',
    warning: 'background:var(--color-warning,#f59e0b);',
    error:   'background:var(--color-danger,#ef4444);',
  };

  function escapeHtml(s) {
    if (s == null) return '';
    return String(s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  }

  function renderEmpty() {
    return (
      '<div class="flex flex-col items-center justify-center px-4 py-10 text-center">' +
        '<div class="w-10 h-10 rounded-full bg-bg-elevated flex items-center justify-center text-text-dim mb-3">' +
          '<svg class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" ' +
          'stroke-linecap="round" stroke-linejoin="round">' +
            '<path d="M18 8A6 6 0 0 0 6 8c0 7-3 9-3 9h18s-3-2-3-9"/>' +
            '<path d="M13.73 21a2 2 0 0 1-3.46 0"/>' +
          '</svg>' +
        '</div>' +
        '<div class="text-[13px] font-medium text-text-soft mb-1">Мэдэгдэл байхгүй</div>' +
        '<div class="text-[11px] text-text-dim">Шинэ мэдэгдэл ирэхэд энд харагдана</div>' +
      '</div>'
    );
  }

  function renderItem(item) {
    var dotStyle = TYPE_DOT[item.type] || TYPE_DOT.info;
    var unreadBg = item.read ? '' : 'background: rgba(97,149,255,0.04);';
    return (
      '<div class="flex gap-3 px-4 py-3 border-b border-border-base" style="' + unreadBg + '">' +
        '<div class="w-2 h-2 rounded-full mt-1.5 shrink-0" style="' + dotStyle + '"></div>' +
        '<div class="flex-1 min-w-0">' +
          (item.title
            ? '<div class="text-[13px] font-semibold text-text-primary">' + escapeHtml(item.title) + '</div>'
            : '') +
          (item.message
            ? '<div class="text-[12px] text-text-soft mt-0.5">' + escapeHtml(item.message) + '</div>'
            : '') +
          '<div class="text-[11px] text-text-dim mt-1">' + escapeHtml(relTime(item.time)) + '</div>' +
        '</div>' +
      '</div>'
    );
  }

  function renderList() {
    var list = $('notifList');
    if (!list) return;
    if (!items.length) {
      list.innerHTML = renderEmpty();
      return;
    }
    var html = '';
    for (var i = 0; i < items.length; i++) html += renderItem(items[i]);
    list.innerHTML = html;
  }

  // ---------- Public API ----------
  window.toggleNotifPanel = function (force) {
    if (force === true)  return openPanel();
    if (force === false) return closePanel();
    if (isOpen()) closePanel(); else openPanel();
  };

  window.Notifications = {
    push:         push,
    clear:        clear,
    markAllRead:  markAllRead,
    list:         function () { return items.slice(); },
    unreadCount:  unreadCount,
  };

  // ---------- Event listeners ----------
  // Click outside хаах
  document.addEventListener('click', function (e) {
    if (!isOpen()) return;
    var panel = $('notifPanel');
    var btn   = $('notifBtn');
    if (panel && panel.contains(e.target)) return;
    if (btn   && btn.contains(e.target))   return;
    closePanel();
  });

  // Esc-р хаах
  document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape' && isOpen()) closePanel();
  });

  // Эхний render
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', renderList);
  } else {
    renderList();
  }
})();
