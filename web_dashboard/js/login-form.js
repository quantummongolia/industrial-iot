// ============================================================
//  LOGIN FORM — Email + Password нэвтрэлт
// ============================================================

async function submitCredentials() {
  console.log("[login] submitCredentials called");
  var emailEl = document.getElementById("emailInput");
  var passEl  = document.getElementById("passwordInput");
  var btn     = document.getElementById("signInBtn");
  var err     = document.getElementById("loginError1");

  var email = (emailEl && emailEl.value || "").trim();
  var pass  = (passEl  && passEl.value  || "").trim();

  console.log("[login] email:", email, "pass length:", pass.length);

  if (!email || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) {
    if (err) err.textContent = "Зөв email хаяг оруулна уу";
    if (emailEl) emailEl.focus();
    return;
  }
  if (!pass || pass.length < 6) {
    if (err) err.textContent = "Нууц үг 6 тэмдэгтээс богино байна";
    if (passEl) passEl.focus();
    return;
  }

  if (err) err.textContent = "";

  if (btn) { btn.disabled = true; btn.textContent = "Шалгаж байна..."; }

  try {
    var result = await loginWithEmail(email, pass);
    console.log("[login] result:", result);

    if (result.ok) {
      location.replace("index.html");
    } else {
      if (err) err.textContent = result.error;
      if (btn) { btn.disabled = false; btn.textContent = "Sign In"; }
      if (passEl) { passEl.value = ""; passEl.focus(); }
    }
  } catch (e) {
    console.error("[login] unexpected error:", e);
    if (err) err.textContent = "Алдаа: " + (e.message || e);
    if (btn) { btn.disabled = false; btn.textContent = "Sign In"; }
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
