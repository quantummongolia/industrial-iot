// ============================================================
//  STALE WATCH — Card бүрт "өгөгдөл хуучирсан" анхааруулга
//  ------------------------------------------------------------
//  Metric card-уудын утга 10 секундын турш шинэчлэгдэхгүй бол
//  card-ын баруун дээд буланд шар цэг гарч ирнэ. Утга шинэчлэгдмэгц
//  цэг алга болно.
//
//  Анимэйшнтэй card-ууд (fm-card / cs-card / wl-card) дээр нэмж
//  ".is-live" класс удирдана: өгөгдөл идэвхтэй ирж байгаа үед л
//  хөдөлгөөн явна. Card ажиллаагүй (өгөгдөл хараахан аваагүй) эсвэл
//  хуучирсан (шар тэмдэгтэй) үед хөдөлгөөн зогсоно.
//
//  Зөвхөн client-side: Firebase руу юу ч бичихгүй. Зөвхөн уг хэрэглэгчийн
//  browser дээр харагдана.
// ============================================================

(function () {
  'use strict';

  var STALE_AFTER_MS = 10 * 1000; // 10 секунд
  var CHECK_EVERY_MS = 1000;      // 1 секунд тутамд шалгана

  // Realtime-аар шинэчлэгддэг span ID-н pattern.
  // Шинэ metric нэмэгдэх үед энд таарах шинэ префикс нэмж болно.
  var METRIC_ID_RE = /^(fm[0-9]+|feedWater|teeremWeight|butluurWeight|wt[0-9]+|ulsLevel|bayanLevel|cleanWaterLevel|waterTankLevel|em(Power|Energy|CurrentA|CurrentB|CurrentC))/;

  var lastUpdate = new WeakMap(); // card → сүүлийн бодит шинэчлэлийн timestamp (undefined = өгөгдөл аваагүй)
  var initAt = new WeakMap();     // card → ажиглаж эхэлсэн агшин (badge grace-д хэрэглэнэ)

  function findMetricCards() {
    var cards = new Set();
    document.querySelectorAll('span[id]').forEach(function (span) {
      if (!METRIC_ID_RE.test(span.id)) return;
      var card = span.closest('.glass-card');
      if (card) cards.add(card);
    });
    return Array.from(cards);
  }

  function ensureBadge(card) {
    var badge = card.querySelector(':scope > .stale-badge');
    if (badge) return badge;

    if (getComputedStyle(card).position === 'static') {
      card.style.position = 'relative';
    }

    badge = document.createElement('div');
    badge.className = 'stale-badge';
    badge.title = 'Өгөгдөл 10+ секунд шинэчлэгдээгүй';
    badge.style.cssText = [
      'position:absolute',
      'top:12px',
      'right:12px',
      'width:18px',
      'height:18px',
      'display:none',
      'align-items:center',
      'justify-content:center',
      'color:var(--color-warning,#f59e0b)',
      'pointer-events:none',
      'z-index:9',
      'transition:opacity 150ms ease',
    ].join(';');
    badge.innerHTML =
      '<svg viewBox="0 0 24 24" width="18" height="18" fill="currentColor" aria-hidden="true">' +
        '<path d="M12 2L1 21h22L12 2zm0 4.5L19.5 19h-15L12 6.5z"/>' +
        '<rect x="11" y="10" width="2" height="5" rx="1" fill="currentColor"/>' +
        '<circle cx="12" cy="17" r="1" fill="currentColor"/>' +
      '</svg>';
    card.appendChild(badge);
    return badge;
  }

  function setStale(card, isStale) {
    var badge = ensureBadge(card);
    badge.style.display = isStale ? 'flex' : 'none';
  }

  // Анимэйшнтэй card дээр л нөлөөтэй — өгөгдөл идэвхтэй үед хөдөлгөөн.
  function setLive(card, live) {
    card.classList.toggle('is-live', !!live);
  }

  // Card-ын үндсэн утга (урсгал / чадал / түвшин) тэг үү? — readout-ийн
  // эхний span (.fm-val/.cs-val/.wl-val/.en-val дотор, unit span-аас өмнө).
  // Утга 0.00 бол хөдөлгөөн зогсоно (урсгалгүй/ажиллаагүй гэж үзнэ).
  function valueIsZero(card) {
    var span = card.querySelector(
      '.fm-val > span:first-child, .cs-val > span:first-child,' +
      '.wl-val > span:first-child, .en-val > span:first-child');
    if (!span) return false; // үндсэн readout олдоогүй бол хуучин зан хэвээр
    var v = parseFloat((span.textContent || '').replace(/[^0-9.\-]/g, ''));
    return !isNaN(v) && v === 0;
  }

  function touch(card) {
    lastUpdate.set(card, Date.now());
    setStale(card, false);
    setLive(card, !valueIsZero(card));
  }

  function check() {
    var now = Date.now();
    findMetricCards().forEach(function (card) {
      var ts = lastUpdate.get(card);
      if (ts != null) {
        // Өгөгдөл ирж байсан — хуучирвал шар тэмдэг + хөдөлгөөн зогсоно.
        var stale = (now - ts) > STALE_AFTER_MS;
        setStale(card, stale);
        setLive(card, !stale && !valueIsZero(card));
      } else {
        // Өгөгдөл хараахан аваагүй — хөдөлгөөнгүй; grace өнгөрвөл шар тэмдэг.
        setLive(card, false);
        var t0 = initAt.get(card);
        setStale(card, t0 != null && (now - t0) > STALE_AFTER_MS);
      }
    });
  }

  function observeCard(card) {
    var spans = card.querySelectorAll('span[id]');
    if (!spans.length) return;

    var obs = new MutationObserver(function () { touch(card); });
    spans.forEach(function (span) {
      if (!METRIC_ID_RE.test(span.id)) return;
      obs.observe(span, { childList: true, characterData: true, subtree: true });
    });

    // Init: badge-ийн 10 секундын grace эндээс эхэлнэ. Хөдөлгөөн нь
    // зөвхөн анхны бодит өгөгдөл ирэхэд (touch) эхэлнэ — өмнө нь хөдлөхгүй.
    initAt.set(card, Date.now());
    ensureBadge(card);
    setLive(card, false);
  }

  function init() {
    findMetricCards().forEach(observeCard);
    setInterval(check, CHECK_EVERY_MS);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
