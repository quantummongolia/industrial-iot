// ============================================================
//  AUTH — Firebase Authentication (email + password)
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

async function loginWithEmail(email, password) {
  if (typeof firebase === "undefined" || !firebase.apps || !firebase.apps.length) {
    return { ok: false, error: "Firebase холбогдоогүй байна" };
  }

  try {
    await firebase.auth().setPersistence(firebase.auth.Auth.Persistence.LOCAL);
    await firebase.auth().signInWithEmailAndPassword(email.trim(), password);
    return { ok: true };
  } catch (e) {
    console.error("[auth] sign-in error:", e);
    var msg = "Нэвтрэхэд алдаа гарлаа";
    switch (e.code) {
      case "auth/invalid-email":
        msg = "Зөв email хаяг оруулна уу"; break;
      case "auth/user-not-found":
        msg = "Бүртгэлтэй хэрэглэгч олдсонгүй"; break;
      case "auth/wrong-password":
      case "auth/invalid-credential":
        msg = "Email эсвэл нууц үг буруу"; break;
      case "auth/user-disabled":
        msg = "Хэрэглэгчийн эрх хаагдсан"; break;
      case "auth/too-many-requests":
        msg = "Хэт олон оролдлого — түр хүлээнэ үү"; break;
      case "auth/network-request-failed":
        msg = "Интернэт холболтоо шалгана уу"; break;
    }
    return { ok: false, error: msg };
  }
}
