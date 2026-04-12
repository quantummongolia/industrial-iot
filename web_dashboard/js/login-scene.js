// ============================================================
//  LOGIN SCENE — Square orbit along building edge
//  ------------------------------------------------------------
//  .orbit-ball нь .building-ийн ирмэгийн дагуу яг дөрвөлжин
//  замаар хөдөлнө. Байшин нь .building-wrap дотор inset-тэй
//  байрладаг тул building-ийн getBoundingClientRect + wrap-ийн
//  offset-г ашиглан wrap-харьцангуй координат тооцно.
// ============================================================

(function () {
  const SPEED = 140; // px/sec

  function initOrbit() {
    const wrap     = document.querySelector('.building-wrap');
    const building = document.querySelector('.building');
    const ball     = document.querySelector('.orbit-ball');
    if (!wrap || !building || !ball) return;

    // wrap болон building-ийн байрлалыг авна
    const wRect = wrap.getBoundingClientRect();
    const bRect = building.getBoundingClientRect();

    // building-ийн corner-ууд, wrap-харьцангуй координатаар
    const bx = bRect.left - wRect.left;  // building зүүн ирмэг
    const by = bRect.top  - wRect.top;   // building дээд ирмэг
    const bw = bRect.width;
    const bh = bRect.height;

    // Дөрвөлжин замын 4 булан
    // top-left → top-right → bottom-right → bottom-left → (loop)
    const corners = [
      { x: bx,      y: by },
      { x: bx + bw, y: by },
      { x: bx + bw, y: by + bh },
      { x: bx,      y: by + bh },
    ];

    const sides = corners.map((c, i) => {
      const next = corners[(i + 1) % corners.length];
      return Math.hypot(next.x - c.x, next.y - c.y);
    });
    const total = sides.reduce((a, b) => a + b, 0);

    let dist = 0;
    let last = null;

    function tick(ts) {
      if (last === null) last = ts;
      const dt = Math.min((ts - last) / 1000, 0.1);
      last = ts;

      dist = (dist + SPEED * dt) % total;

      let rem = dist;
      let seg = 0;
      while (rem > sides[seg] && seg < corners.length - 1) {
        rem -= sides[seg];
        seg++;
      }

      const from = corners[seg];
      const to   = corners[(seg + 1) % corners.length];
      const t    = sides[seg] > 0 ? rem / sides[seg] : 0;

      ball.style.left = (from.x + (to.x - from.x) * t) + 'px';
      ball.style.top  = (from.y + (to.y - from.y) * t) + 'px';

      requestAnimationFrame(tick);
    }

    requestAnimationFrame(tick);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initOrbit);
  } else {
    initOrbit();
  }
})();
