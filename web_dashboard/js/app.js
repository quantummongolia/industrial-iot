// ============================================================
//  APP — Bootstrap entry point for index.html
//  Auth guard is in <head>, so by the time this runs
//  the user is already verified.
// ============================================================

document.addEventListener("DOMContentLoaded", function() {
  setupSidebar();
  startClock();
  detectUserRole();
  initRealtime();
  syncNavbarHeight();
});

// Keep sidebar/content top offset in sync with navbar's actual height
function syncNavbarHeight() {
  var navbar  = document.getElementById("topNavbar");
  var shell   = document.getElementById("appShell");
  var sidebar = document.getElementById("sidebar");
  if (!navbar) return;

  function update() {
    var h = navbar.offsetHeight + "px";
    if (shell)   shell.style.paddingTop = h;
    if (sidebar) sidebar.style.top = h;
  }

  update();
  window.addEventListener("resize", update);
  new ResizeObserver(update).observe(navbar);
}
