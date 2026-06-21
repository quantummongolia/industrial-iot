// ============================================================
//  AUTH — Firebase Email OTP (passwordless) нэвтрэлт
//  Код хүсэх/шалгах нь Cloud Function дээр (сервер) хийгдэнэ.
// ============================================================

function isVerified() {
  if (typeof firebase === "undefined" || !firebase.apps || !firebase.apps.length) return false;
  return !!firebase.auth().currentUser;
}

function logout() {
  if (typeof firebase !== "undefined" && firebase.apps && firebase.apps.length) {
    firebase.auth().signOut().finally(function () {
      location.replace("login.html");
    });
  } else {
    location.replace("login.html");
  }
}

function _fnErr(e) {
  if (e && e.code === "functions/unavailable") return "Сервертэй холбогдож чадсангүй";
  if (e && e.code === "functions/deadline-exceeded") return "Хугацаа хэтэрлээ — дахин оролдоно уу";
  if (e && e.code === "functions/internal") return "Серверийн алдаа — дахин оролдоно уу";
  if (e && e.message) return e.message;
  return "Алдаа гарлаа";
}

// 6 оронтой код хүсэх — имэйл рүү код илгээнэ
async function requestOtp(email) {
  if (typeof firebase === "undefined" || !firebase.apps || !firebase.apps.length) {
    return { ok: false, error: "Firebase холбогдоогүй байна" };
  }
  try {
    var call = firebase.functions().httpsCallable("requestOtp");
    await call({ email: String(email || "").trim().toLowerCase() });
    return { ok: true };
  } catch (e) {
    console.error("[auth] requestOtp error:", e);
    return { ok: false, error: _fnErr(e) };
  }
}

// Кодыг шалгаж нэвтрэх (custom token → signInWithCustomToken)
async function verifyOtp(email, code) {
  if (typeof firebase === "undefined" || !firebase.apps || !firebase.apps.length) {
    return { ok: false, error: "Firebase холбогдоогүй байна" };
  }
  try {
    await firebase.auth().setPersistence(firebase.auth.Auth.Persistence.LOCAL);
    var call = firebase.functions().httpsCallable("verifyOtp");
    var res = await call({
      email: String(email || "").trim().toLowerCase(),
      code: String(code || "").trim(),
    });
    var token = res && res.data && res.data.token;
    if (!token) return { ok: false, error: "Token буцаагүй" };
    await firebase.auth().signInWithCustomToken(token);
    return { ok: true };
  } catch (e) {
    console.error("[auth] verifyOtp error:", e);
    return { ok: false, error: _fnErr(e) };
  }
}
