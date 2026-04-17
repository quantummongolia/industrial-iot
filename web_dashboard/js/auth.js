// ============================================================
//  AUTH — Firestore хэрэглэгч баталгаажуулалт + 7 хоногийн session
// ============================================================

var VERIFY_KEY  = "phone_verified_until";
var VERIFY_DAYS = 7;
var VERIFY_MS   = VERIFY_DAYS * 24 * 60 * 60 * 1000;

function isVerified() {
  var until = parseInt(localStorage.getItem(VERIFY_KEY) || "0", 10);
  return until > Date.now();
}

function setVerified(email) {
  localStorage.setItem(VERIFY_KEY, String(Date.now() + VERIFY_MS));
  if (email) localStorage.setItem("current_user_email", email);
}

function logout() {
  localStorage.removeItem(VERIFY_KEY);
  localStorage.removeItem("current_user_email");
  location.replace("login.html");
}

// SHA-256 hash (Web Crypto API — native, library хэрэггүй)
async function sha256(message) {
  var msgBuffer = new TextEncoder().encode(message);
  var hashBuffer = await crypto.subtle.digest("SHA-256", msgBuffer);
  var hashArray = Array.from(new Uint8Array(hashBuffer));
  return hashArray.map(function(b) { return b.toString(16).padStart(2, "0"); }).join("");
}

// Firestore-с email-ээр хэрэглэгч хайж, нууц үг шалгана
async function loginWithEmail(email, password) {
  if (typeof firebase === "undefined" || !firebase.apps || !firebase.apps.length) {
    return { ok: false, error: "Firebase холбогдоогүй байна" };
  }

  try {
    var db = firebase.firestore();
    var snapshot = await db.collection("users")
      .where("email", "==", email.toLowerCase().trim())
      .limit(1)
      .get();

    if (snapshot.empty) {
      return { ok: false, error: "Бүртгэлтэй хэрэглэгч олдсонгүй" };
    }

    var userData = snapshot.docs[0].data();
    var inputHash = await sha256(password);

    if (inputHash !== userData.passwordHash) {
      return { ok: false, error: "Нууц үг буруу байна" };
    }

    setVerified(email);
    return { ok: true };

  } catch (e) {
    console.error("[auth] Firestore error:", e);
    return { ok: false, error: "Сервертэй холбогдоход алдаа гарлаа" };
  }
}
