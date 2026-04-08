// ============================================================
//  AUTH — Mock phone verification (7-day cache)
//  Used by both login.html and dashboard.html
// ============================================================
const VERIFY_KEY = "phone_verified_until";
const VERIFY_DAYS = 7;
const VERIFY_MS = VERIFY_DAYS * 24 * 60 * 60 * 1000;

function isVerified() {
  const until = parseInt(localStorage.getItem(VERIFY_KEY) || "0", 10);
  return until > Date.now();
}

function setVerified() {
  localStorage.setItem(VERIFY_KEY, String(Date.now() + VERIFY_MS));
}

function logout() {
  localStorage.removeItem(VERIFY_KEY);
  location.replace("login.html");
}

/**
 * Used by dashboard.html — if not verified, kick to login.html.
 * Returns true if user is verified (allow boot to continue).
 */
function requireAuth() {
  if (!isVerified()) {
    location.replace("login.html");
    return false;
  }
  return true;
}

// ----- Login form handlers (used by login.html) -----
function sendVerificationCode() {
  const phone = document.getElementById("phoneInput").value.trim();
  const errEl = document.getElementById("loginError1");
  if (phone.length < 6) {
    errEl.textContent = "Утасны дугаараа зөв оруулна уу";
    return;
  }
  errEl.textContent = "";
  document.getElementById("loginStep1").classList.remove("active");
  document.getElementById("loginStep2").classList.add("active");
  setTimeout(() => document.getElementById("codeInput").focus(), 50);
}

function verifyCode() {
  const code = document.getElementById("codeInput").value.trim();
  const errEl = document.getElementById("loginError2");
  if (!/^\d{6}$/.test(code)) {
    errEl.textContent = "6 оронтой тоог оруулна уу";
    return;
  }
  errEl.textContent = "";
  setVerified();
  location.replace("dashboard.html");
}

/**
 * Wire up Enter key + auto-redirect for login.html.
 * Call this only on the login page.
 */
function setupLoginPage() {
  // If already verified, skip the form entirely
  if (isVerified()) {
    location.replace("dashboard.html");
    return;
  }
  const phoneEl = document.getElementById("phoneInput");
  const codeEl = document.getElementById("codeInput");
  if (phoneEl) {
    phoneEl.addEventListener("keydown", e => {
      if (e.key === "Enter") sendVerificationCode();
    });
    phoneEl.focus();
  }
  if (codeEl) {
    codeEl.addEventListener("keydown", e => {
      if (e.key === "Enter") verifyCode();
    });
  }
}
