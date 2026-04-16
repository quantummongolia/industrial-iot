// ============================================================
//  AUTH — Mock phone verification with 7-day cache
// ============================================================

var VERIFY_KEY  = "phone_verified_until";
var VERIFY_DAYS = 7;
var VERIFY_MS   = VERIFY_DAYS * 24 * 60 * 60 * 1000;

function isVerified() {
  var until = parseInt(localStorage.getItem(VERIFY_KEY) || "0", 10);
  return until > Date.now();
}

function setVerified() {
  localStorage.setItem(VERIFY_KEY, String(Date.now() + VERIFY_MS));
}

function logout() {
  localStorage.removeItem(VERIFY_KEY);
  location.replace("login.html");
}

// Called from login-form.js after 6-digit code entered
function verifyCode() {
  var code  = document.getElementById("codeInput").value.trim();
  var errEl = document.getElementById("loginError2");
  if (!/^\d{6}$/.test(code)) {
    if (errEl) errEl.textContent = "6 оронтой тоог оруулна уу";
    return;
  }
  if (errEl) errEl.textContent = "";
  setVerified();
  // Always redirect to dashboard
  location.replace("index.html");
}
