// ============================================================
//  DASH-SCROLL — .dash-row мөрүүдийг хулганаар чирч гүйлгэх +
//  хоёр талын жижиг саарал сум товч (үлдсэн контент байвал гарч ирнэ).
//   - Хулганаар дарж чирэх → зүүн/баруун гүйлгэнэ (drag-to-scroll).
//   - Баруун талд илүү контент байвал → баруун сум.
//   - Зүүн талд гүйсэн байвал → зүүн сум.
//   - Touch (гар утас) дээр native гүйлгэлтийг хэвээр үлдээнэ.
// ============================================================

(function () {
  'use strict';

  var SVG_LEFT  = '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="15 18 9 12 15 6"></polyline></svg>';
  var SVG_RIGHT = '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"></polyline></svg>';

  function enhance(row) {
    if (row.dataset.scrollEnhanced) return;
    row.dataset.scrollEnhanced = '1';

    // .dash-row-wrap дотор боож сумнуудыг row-н ГАДНА (гүйдэггүй) байрлуулна.
    var wrap = document.createElement('div');
    wrap.className = 'dash-row-wrap';
    row.parentNode.insertBefore(wrap, row);
    wrap.appendChild(row);

    var left = document.createElement('button');
    left.type = 'button';
    left.className = 'dash-arrow dash-arrow-left';
    left.setAttribute('aria-label', 'Зүүн тийш гүйлгэх');
    left.innerHTML = SVG_LEFT;

    var right = document.createElement('button');
    right.type = 'button';
    right.className = 'dash-arrow dash-arrow-right';
    right.setAttribute('aria-label', 'Баруун тийш гүйлгэх');
    right.innerHTML = SVG_RIGHT;

    wrap.appendChild(left);
    wrap.appendChild(right);

    function step() { return Math.max(160, row.clientWidth * 0.8); }

    function update() {
      var max = row.scrollWidth - row.clientWidth;
      left.classList.toggle('show', row.scrollLeft > 2);
      right.classList.toggle('show', row.scrollLeft < max - 2);
    }

    left.addEventListener('click', function () {
      row.scrollBy({ left: -step(), behavior: 'smooth' });
    });
    right.addEventListener('click', function () {
      row.scrollBy({ left: step(), behavior: 'smooth' });
    });

    row.addEventListener('scroll', update, { passive: true });
    window.addEventListener('resize', update);
    if (window.ResizeObserver) new ResizeObserver(update).observe(row);
    update();
    setTimeout(update, 400); // layout/өгөгдөл орсны дараа дахин шалгах

    // ── Хулганаар чирч гүйлгэх (зөвхөн mouse; touch → native гүйлгэлт) ──
    var down = false, startX = 0, startLeft = 0, moved = false;

    row.addEventListener('pointerdown', function (e) {
      if (e.pointerType !== 'mouse' || e.button !== 0) return;
      if (e.target.closest('.dash-arrow')) return; // сум дээр дарвал чирэхгүй
      down = true; moved = false;
      startX = e.clientX; startLeft = row.scrollLeft;
      row.setPointerCapture(e.pointerId);
      row.classList.add('dragging');
    });

    row.addEventListener('pointermove', function (e) {
      if (!down) return;
      var dx = e.clientX - startX;
      if (Math.abs(dx) > 3) moved = true;
      row.scrollLeft = startLeft - dx;
    });

    function endDrag() {
      if (!down) return;
      down = false;
      row.classList.remove('dragging');
    }
    row.addEventListener('pointerup', endDrag);
    row.addEventListener('pointercancel', endDrag);

    // Чирсний дараах "click"-ийг залгиж card дотор санамсаргүй click болохоос сэргийлнэ.
    row.addEventListener('click', function (e) {
      if (moved) { e.preventDefault(); e.stopPropagation(); moved = false; }
    }, true);

    // Чирэх үед текст сонгогдохоос сэргийлнэ.
    row.addEventListener('dragstart', function (e) { e.preventDefault(); });
  }

  function init() {
    document.querySelectorAll('.dash-row').forEach(enhance);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
