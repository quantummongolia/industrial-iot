// ============================================================
//  AUTH — Mock phone verification with 7-day cache
//  ------------------------------------------------------------
//  Энэ файл нь index.html (dashboard) болон login.html-ийн аль
//  алинд ашиглагдана. localStorage-д verification expiration
//  timestamp хадгалж, 7 хоног баталгаажилтыг санана.
//
//  Public API:
//    isVerified()             — одоогоор нэвтэрсэн эсэхийг шалгана
//    sendVerificationCode()   — login form step 1 handler
//    verifyCode()             — login form step 2 handler
//    logout()                 — verification цэвэрлээд reload хийнэ
//    showLogin() / hideLogin()— index.html-ийн overlay toggle (optional)
//    setupLoginPage()         — standalone login.html-д зориулсан
// ============================================================

const VERIFY_KEY  = "phone_verified_until";
const VERIFY_DAYS = 7;
const VERIFY_MS   = VERIFY_DAYS * 24 * 60 * 60 * 1000;

// ---------- Core state ----------
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

// ---------- Login form handlers ----------
// Credentials-ийг js/login-form.js (submitCredentials) хариуцна.
// Энэ файл нь зөвхөн SMS код баталгаажуулж session үүсгэх үүрэгтэй.

function verifyCode() {
  const code  = document.getElementById("codeInput").value.trim();
  const errEl = document.getElementById("loginError2");
  if (!/^\d{6}$/.test(code)) {
    errEl.textContent = "6 оронтой тоог оруулна уу";
    return;
  }
  errEl.textContent = "";
  setVerified();

  // Хэрэв бид index.html дотор байгаа бол overlay-г хааж app-аа эхлүүлнэ.
  // Үгүй бол (standalone login.html) dashboard руу үсэрнэ.
  if (typeof hideLogin === "function" && document.getElementById("appShell")) {
    hideLogin();
  } else {
    location.replace("index.html");
  }
}

// ---------- Standalone login.html setup ----------
function setupLoginPage() {
  // Аль хэдийн verified бол dashboard руу шууд шилжүүлнэ.
  if (isVerified()) {
    location.replace("index.html");
    return;
  }
  // Шинэ form-ын Enter key, step switching бүгд js/login-form.js дотор.
  if (typeof setupLoginForm === "function") {
    setupLoginForm();
  }
}
