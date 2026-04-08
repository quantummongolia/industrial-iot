// ============================================================
//  IP DETECTION — Public IP fetch + role badge update
//  Requires ALLOWED_IPS array from config.js (optional)
// ============================================================
async function detectUserRole() {
  const badge = document.getElementById("userBadge");
  if (!badge) return;
  try {
    const res = await fetch("https://api.ipify.org?format=json");
    const data = await res.json();
    const ip = data.ip;
    const allowed = (typeof ALLOWED_IPS !== "undefined" ? ALLOWED_IPS : []);
    if (allowed.includes(ip)) {
      badge.textContent = "INTERNAL · " + ip;
      badge.classList.add("internal");
    } else {
      badge.textContent = "EXTERNAL · " + ip;
      badge.classList.add("external");
    }
  } catch (e) {
    badge.textContent = "IP UNKNOWN";
  }
}
