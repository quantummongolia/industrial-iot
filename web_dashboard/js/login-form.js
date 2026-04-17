// ============================================================
//  LOGIN FORM — Email + Password нэвтрэлт
// ============================================================

async function submitCredentials() {
  var emailEl = document.getElementById("emailInput");
  var passEl  = document.getElementById("passwordInput");
  var btn     = document.getElementById("signInBtn");
  var err     = document.getElementById("loginError1");

  var email = (emailEl && emailEl.value || "").trim();
  var pass  = (passEl  && passEl.value  || "").trim();

  if (!email || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) {
    if (err) err.textContent = "Зөв email хаяг оруулна уу";
    if (emailEl) emailEl.focus();
    return;
  }
  if (!pass || pass.length < 4) {
    if (err) err.textContent = "Нууц үг 4 тэмдэгтээс богино байна";
    if (passEl) passEl.focus();
    return;
  }

  if (err) err.textContent = "";

  // Loading state
  if (btn) { btn.disabled = true; btn.textContent = "Шалгаж байна..."; }

  var result = await loginWithEmail(email, pass);

  if (result.ok) {
    location.replace("index.html");
  } else {
    if (err) err.textContent = result.error;
    if (btn) { btn.disabled = false; btn.textContent = "Sign In"; }
    if (passEl) { passEl.value = ""; passEl.focus(); }
  }
}

function togglePassword() {
  var wrap = document.getElementById("passwordWrap");
  var inp  = document.getElementById("passwordInput");
  if (!wrap || !inp) return;
  var revealed = wrap.classList.toggle("password-wrap-revealed");
  inp.type = revealed ? "text" : "password";
}

function setupLoginForm() {
  var emailEl = document.getElementById("emailInput");
  var passEl  = document.getElementById("passwordInput");

  if (emailEl) {
    emailEl.addEventListener("keydown", function(e) {
      if (e.key === "Enter") { e.preventDefault(); if (passEl) passEl.focus(); }
    });
  }
  if (passEl) {
    passEl.addEventListener("keydown", function(e) {
      if (e.key === "Enter") { e.preventDefault(); submitCredentials(); }
    });
  }
}
