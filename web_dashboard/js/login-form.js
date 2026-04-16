// ============================================================
//  LOGIN FORM — Credentials step + SMS step controller
//  Tailwind CSS compatible
// ============================================================

function goToStep(step) {
  var s1 = document.getElementById("loginStep1");
  var s2 = document.getElementById("loginStep2");
  var d1 = document.getElementById("loginDot1");
  var d2 = document.getElementById("loginDot2");

  if (!s1 || !s2) return;

  if (step === 1) {
    s1.classList.add("active");
    s2.classList.remove("active");
    if (d1) { d1.classList.add("bg-accent", "scale-[1.4]"); d1.classList.remove("bg-border-strong"); }
    if (d2) { d2.classList.remove("bg-accent", "scale-[1.4]"); d2.classList.add("bg-border-strong"); }
    setTimeout(function() {
      var u = document.getElementById("usernameInput");
      if (u) u.focus();
    }, 60);
  } else {
    s1.classList.remove("active");
    s2.classList.add("active");
    if (d1) { d1.classList.remove("bg-accent", "scale-[1.4]"); d1.classList.add("bg-border-strong"); }
    if (d2) { d2.classList.add("bg-accent", "scale-[1.4]"); d2.classList.remove("bg-border-strong"); }
    setTimeout(function() {
      var c = document.getElementById("codeInput");
      if (c) c.focus();
    }, 60);
  }
}

function submitCredentials() {
  var user = document.getElementById("usernameInput");
  var pass = document.getElementById("passwordInput");
  var err  = document.getElementById("loginError1");

  var u = (user && user.value || "").trim();
  var p = (pass && pass.value || "").trim();

  if (!u) {
    if (err) err.textContent = "Хэрэглэгчийн нэрээ оруулна уу";
    if (user) user.focus();
    return;
  }
  if (!p || p.length < 4) {
    if (err) err.textContent = "Нууц үг 4 тэмдэгтээс богино байна";
    if (pass) pass.focus();
    return;
  }

  if (err) err.textContent = "";
  goToStep(2);
}

function submitSmsCode() {
  if (typeof verifyCode === "function") {
    verifyCode();
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
  var user = document.getElementById("usernameInput");
  var pass = document.getElementById("passwordInput");
  var code = document.getElementById("codeInput");

  if (user) {
    user.addEventListener("keydown", function(e) {
      if (e.key === "Enter") {
        e.preventDefault();
        if (pass) pass.focus();
      }
    });
  }
  if (pass) {
    pass.addEventListener("keydown", function(e) {
      if (e.key === "Enter") {
        e.preventDefault();
        submitCredentials();
      }
    });
  }
  if (code) {
    code.addEventListener("keydown", function(e) {
      if (e.key === "Enter") {
        e.preventDefault();
        submitSmsCode();
      }
    });
    code.addEventListener("input", function() {
      code.value = code.value.replace(/\D/g, "").slice(0, 6);
    });
  }

  goToStep(1);
}
