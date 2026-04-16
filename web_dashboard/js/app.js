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
});
