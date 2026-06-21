// ============================================================
//  LOGIN FORM — Email OTP (passwordless), 2 алхамт урсгал
//    Алхам 1: имэйл → код илгээх
//    Алхам 2: код → нэвтрэх
// ============================================================

var currentEmail = "";

// ── Алхам 1: имэйл илгээх ────────────────────────────────────
async function submitEmail() {
  var emailEl = document.getElementById("emailInput");
  var err = document.getElementById("loginError1");
  var btn = document.getElementById("sendCodeBtn");

  var email = (emailEl && emailEl.value || "").trim().toLowerCase();

  if (!email || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) {
    if (err) err.textContent = "Зөв имэйл оруулна уу";
    if (emailEl) emailEl.focus();
    return;
  }

  if (err) err.textContent = "";
  if (btn) { btn.disabled = true; btn.textContent = "Илгээж байна..."; }

  try {
    var result = await requestOtp(email);
    if (result.ok) {
      currentEmail = email;
      _showStep("code");
      var hint = document.getElementById("codeHint");
      if (hint) hint.textContent = email + " рүү код илгээлээ";
      var codeEl = document.getElementById("codeInput");
      if (codeEl) codeEl.focus();
    } else {
      if (err) err.textContent = result.error;
    }
  } catch (e) {
    if (err) err.textContent = "Алдаа: " + (e.message || e);
  } finally {
    if (btn) { btn.disabled = false; btn.textContent = "Код авах"; }
  }
}

// ── Алхам 2: код шалгах ──────────────────────────────────────
async function submitCode() {
  var codeEl = document.getElementById("codeInput");
  var err = document.getElementById("loginError2");
  var btn = document.getElementById("verifyBtn");

  var code = (codeEl && codeEl.value || "").trim();

  if (!/^\d{6}$/.test(code)) {
    if (err) err.textContent = "6 оронтой код оруулна уу";
    if (codeEl) codeEl.focus();
    return;
  }

  if (err) err.textContent = "";
  if (btn) { btn.disabled = true; btn.textContent = "Шалгаж байна..."; }

  try {
    var result = await verifyOtp(currentEmail, code);
    if (result.ok) {
      location.replace("index.html");
    } else {
      if (err) err.textContent = result.error;
      if (btn) { btn.disabled = false; btn.textContent = "Баталгаажуулах"; }
      if (codeEl) { codeEl.value = ""; codeEl.focus(); }
    }
  } catch (e) {
    if (err) err.textContent = "Алдаа: " + (e.message || e);
    if (btn) { btn.disabled = false; btn.textContent = "Баталгаажуулах"; }
  }
}

// Имэйл алхам руу буцах
function resetToEmail() {
  _showStep("email");
  var codeEl = document.getElementById("codeInput");
  if (codeEl) codeEl.value = "";
  var err2 = document.getElementById("loginError2");
  if (err2) err2.textContent = "";
  var emailEl = document.getElementById("emailInput");
  if (emailEl) emailEl.focus();
}

function _showStep(step) {
  var stepEmail = document.getElementById("stepEmail");
  var stepCode = document.getElementById("stepCode");
  if (stepEmail) stepEmail.style.display = (step === "email") ? "" : "none";
  if (stepCode) stepCode.style.display = (step === "code") ? "" : "none";
}

function setupLoginForm() {
  var emailEl = document.getElementById("emailInput");
  var codeEl = document.getElementById("codeInput");

  if (emailEl) {
    emailEl.addEventListener("keydown", function (e) {
      if (e.key === "Enter") { e.preventDefault(); submitEmail(); }
    });
  }
  if (codeEl) {
    codeEl.addEventListener("keydown", function (e) {
      if (e.key === "Enter") { e.preventDefault(); submitCode(); }
    });
    // зөвхөн тоо
    codeEl.addEventListener("input", function () {
      codeEl.value = codeEl.value.replace(/\D/g, "").slice(0, 6);
    });
  }
}
