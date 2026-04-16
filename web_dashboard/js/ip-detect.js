// ============================================================
//  IP DETECTION — Public IP fetch + role badge
//  Shows loading skeleton while fetching
// ============================================================

async function detectUserRole() {
  var badge = document.getElementById("userBadge");
  if (!badge) return;

  try {
    var res  = await fetch("https://api.ipify.org?format=json");
    var data = await res.json();
    var ip   = data.ip;
    var allowed = (typeof ALLOWED_IPS !== "undefined" ? ALLOWED_IPS : []);

    if (allowed.includes(ip)) {
      badge.textContent = "INTERNAL \u00b7 " + ip;
      badge.classList.add("text-success", "border-success/30");
    } else {
      badge.textContent = "EXTERNAL \u00b7 " + ip;
      badge.classList.add("text-accent", "border-accent-border");
    }
  } catch (e) {
    badge.textContent = "IP UNKNOWN";
  }
}
