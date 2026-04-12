// ============================================================
//  LOGIN FORM — Credentials step + SMS step controller
//  ------------------------------------------------------------
//  Үе шатууд:
//    Step 1 (credentials) — username + password → "Үргэлжлүүлэх"
//    Step 2 (sms)         — 6 оронтой код     → "Баталгаажуулах"
//
//  Энэ модуль нь DOM handler-уудыг л агуулна. Бодит auth логик
//  (verify code / set session) js/auth.js дотор байна.
//
//  Public API:
//    setupLoginForm()          — Enter key + toggle + submit bind
//    goToStep(1 | 2)           — үе шат хооронд шилжих
//    submitCredentials()       — Step 1 handler
//    submitSmsCode()           — Step 2 handler (auth.js → verifyCode)
//    togglePassword()          — нүд icon toggle
// ============================================================

// ---------- Step navigation ----------
function goToStep(step) {
  const s1 = document.getElementById("loginStep1");
  const s2 = document.getElementById("loginStep2");
  const d1 = document.getElementById("loginDot1");
  const d2 = document.getElementById("loginDot2");

  if (!s1 || !s2) return;

  if (step === 1) {
    s1.classList.add("active");
    s2.classList.remove("active");
    d1 && d1.classList.add("active");
    d2 && d2.classList.remove("active");
    setTimeout(() => {
      const u = document.getElementById("usernameInput");
      if (u) u.focus();
    }, 60);
  } else {
    s1.classList.remove("active");
    s2.classList.add("active");
    d1 && d1.classList.remove("active");
    d2 && d2.classList.add("active");
    setTimeout(() => {
      const c = document.getElementById("codeInput");
      if (c) c.focus();
    }, 60);
  }
}

// ---------- Step 1 — credentials ----------
// Одоогоор backend байхгүй тул mock: хоосон биш байхын хажуугаар
// бүх username/password-ыг хүлээн авна. Дараа нь энд API дуудна.
function submitCredentials() {
  const user = document.getElementById("usernameInput");
  const pass = document.getElementById("passwordInput");
  const err  = document.getElementById("loginError1");

  const u = (user && user.value || "").trim();
  const p = (pass && pass.value || "").trim();

  if (!u) {
    err.textContent = "Хэрэглэгчийн нэрээ оруулна уу";
    user && user.focus();
    return;
  }
  if (p.length < 4) {
    err.textContent = "Нууц үг 4 тэмдэгтээс богино байна";
    pass && pass.focus();
    return;
  }

  err.textContent = "";
  // TODO: Backend руу credential-ыг илгээх, амжилттай бол SMS явуулах
  goToStep(2);
}

// ---------- Step 2 — SMS ----------
// auth.js дахь verifyCode() нь аль хэдийн localStorage-ыг шинэчилж,
// hideLogin() эсвэл redirect хийнэ. Энд зөвхөн код шалгах хэсэг л.
function submitSmsCode() {
  // auth.js-ийн бэлэн функцийг шууд дуудах — давхардал гаргахгүй
  if (typeof verifyCode === "function") {
    verifyCode();
  }
}

// ---------- Password show/hide ----------
function togglePassword() {
  const wrap = document.getElementById("passwordWrap");
  const inp  = document.getElementById("passwordInput");
  if (!wrap || !inp) return;
  const revealed = wrap.classList.toggle("revealed");
  inp.type = revealed ? "text" : "password";
}

// ---------- Setup — called by auth.js setupLoginPage() ----------
function setupLoginForm() {
  const user = document.getElementById("usernameInput");
  const pass = document.getElementById("passwordInput");
  const code = document.getElementById("codeInput");

  // Enter key → submit
  if (user) {
    user.addEventListener("keydown", e => {
      if (e.key === "Enter") {
        e.preventDefault();
        pass && pass.focus();
      }
    });
  }
  if (pass) {
    pass.addEventListener("keydown", e => {
      if (e.key === "Enter") {
        e.preventDefault();
        submitCredentials();
      }
    });
  }
  if (code) {
    code.addEventListener("keydown", e => {
      if (e.key === "Enter") {
        e.preventDefault();
        submitSmsCode();
      }
    });
    // зөвхөн тоо авна
    code.addEventListener("input", () => {
      code.value = code.value.replace(/\D/g, "").slice(0, 6);
    });
  }

  // Эхэндээ step 1 идэвхжинэ
  goToStep(1);
}
