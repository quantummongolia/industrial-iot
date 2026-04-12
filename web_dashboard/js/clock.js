// ============================================================
//  CLOCK — Live HH:MM:SS display in the header
//  #headerTime элементийг секунд тутам шинэчилнэ.
// ============================================================

function tickClock() {
  const el = document.getElementById("headerTime");
  if (!el) return;
  const now = new Date();
  const h = String(now.getHours()).padStart(2, "0");
  const m = String(now.getMinutes()).padStart(2, "0");
  const s = String(now.getSeconds()).padStart(2, "0");
  el.textContent = `${h}:${m}:${s}`;
}

function startClock() {
  tickClock();
  setInterval(tickClock, 1000);
}
