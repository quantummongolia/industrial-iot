// ============================================================
//  APP — Bootstrap entry point for index.html
//  Tailwind CSS compatible
// ============================================================

var _appBooted = false;

function bootApp() {
  if (_appBooted) return;
  _appBooted = true;

  setupSidebar();
  startClock();
  detectUserRole();
  initRealtime();
}

function showLogin() {
  var modal = document.getElementById("loginOverlay");
  var shell = document.getElementById("appShell");
  if (modal) {
    modal.classList.remove("hidden");
    modal.classList.add("flex");
  }
  if (shell) {
    shell.classList.add("hidden");
    shell.classList.remove("flex");
  }
  document.body.style.overflow = "hidden";
}

function hideLogin() {
  var modal = document.getElementById("loginOverlay");
  var shell = document.getElementById("appShell");
  if (modal) {
    modal.classList.add("hidden");
    modal.classList.remove("flex");
  }
  if (shell) {
    shell.classList.remove("hidden");
    shell.classList.add("flex");
  }
  document.body.style.overflow = "";
  bootApp();
}

function handleModalBackdrop(e) {
  // Dashboard login modal does not close on backdrop click (security)
}

document.addEventListener("DOMContentLoaded", function() {
  if (typeof setupLoginForm === "function") {
    setupLoginForm();
  }

  if (isVerified()) {
    hideLogin();
  } else {
    showLogin();
    setTimeout(function() {
      var firstInput = document.getElementById("usernameInput");
      if (firstInput) firstInput.focus();
    }, 80);
  }
});
