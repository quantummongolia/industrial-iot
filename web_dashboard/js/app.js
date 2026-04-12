// ============================================================
//  APP — Bootstrap entry point for index.html
//  ------------------------------------------------------------
//  Ачаалах дараалал:
//    1. DOMContentLoaded
//    2. setupLoginForm() — Enter key, step switching, validation
//    3. isVerified() → true  → hideLogin() → bootApp()
//                    → false → showLogin()
//    4. bootApp() нь зөвхөн нэг удаа ажиллаж:
//       setupSidebar() → startClock() → detectUserRole() → initRealtime()
// ============================================================

let _appBooted = false;

function bootApp() {
  if (_appBooted) return;
  _appBooted = true;

  setupSidebar();
  startClock();
  detectUserRole();
  initRealtime();
}

// showLogin / hideLogin — modal toggle (index.html дотор)
function showLogin() {
  const modal = document.getElementById("loginOverlay");
  const shell  = document.getElementById("appShell");
  if (modal) modal.classList.remove("hidden");
  if (shell) shell.style.display = "none";
  document.body.style.overflow = "hidden";
}

function hideLogin() {
  const modal = document.getElementById("loginOverlay");
  const shell  = document.getElementById("appShell");
  if (modal) modal.classList.add("hidden");
  if (shell) shell.style.display = "flex";
  document.body.style.overflow = "";
  bootApp();
}

function handleModalBackdrop(e) {
  // index.html-д backdrop дарахад login хаахгүй (аюулгүйн үүднээс)
}

document.addEventListener("DOMContentLoaded", () => {
  if (typeof setupLoginForm === "function") {
    setupLoginForm();
  }

  if (isVerified()) {
    hideLogin();
  } else {
    showLogin();
    setTimeout(() => {
      const firstInput = document.getElementById("usernameInput");
      if (firstInput) firstInput.focus();
    }, 80);
  }
});
