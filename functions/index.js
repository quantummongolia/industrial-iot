/**
 * MRESOURCE — Email OTP passwordless нэвтрэлт (Cloud Functions v2)
 *
 *   requestOtp(email)       → allowlist шалгаад, 6 оронтой код үүсгэж,
 *                             hash-ийг RTDB-д хадгалаад, кодыг Gmail-ээр илгээнэ.
 *   verifyOtp(email, code)  → код зөв бол custom token буцаана (PWA дотор нэвтэрнэ).
 *
 * Код ҮҮСГЭХ ч, ШАЛГАХ ч хоёулаа ЭНД (сервер дээр) болно — browser-т хэзээ ч биш.
 * RTDB-д зөвхөн кодын HASH хадгална, plaintext код зөвхөн имэйл дотор л байна.
 *
 * Secret:  GMAIL_APP_PASSWORD   (firebase functions:secrets:set GMAIL_APP_PASSWORD)
 */

const { onCall, HttpsError } = require("firebase-functions/v2/https");
const { defineSecret } = require("firebase-functions/params");
const logger = require("firebase-functions/logger");
const admin = require("firebase-admin");
const crypto = require("crypto");
const nodemailer = require("nodemailer");

admin.initializeApp();

const GMAIL_APP_PASSWORD = defineSecret("GMAIL_APP_PASSWORD");

// ──────────────────────────────────────────────────────────────
//  ТОХИРГОО — энд засна
// ──────────────────────────────────────────────────────────────

// Илгээгч + allowlist нь НУУЦ файлд — functions/allowlist.js
// (gitignore-д орсон, GitHub-д ОРОХГҮЙ). Жишээ бүтэц: allowlist.example.js
const { SENDER_EMAIL, ALLOWLIST: RAW_ALLOWLIST } = require("./allowlist");
const SENDER_NAME = "MRESOURCE";

// Нэвтрэх эрхтэй хүмүүс — ЗӨВХӨН эдгээр имэйл код авч, нэвтэрнэ.
// Хүн нэмэх/хасах: allowlist.js засаад →  firebase deploy --only functions
const ALLOWLIST = RAW_ALLOWLIST.map((e) => e.trim().toLowerCase());

const CODE_TTL_MS = 10 * 60 * 1000; // код 10 минут хүчинтэй
const RESEND_COOLDOWN_MS = 60 * 1000; // 60 сек дотор дахин код хүсэхгүй
const MAX_ATTEMPTS = 5; // буруу оролдлогын дээд хязгаар

// ──────────────────────────────────────────────────────────────

// RTDB key-д '.' орж болохгүй → database.rules.json-ы admins node-той ИЖИЛ кодлоно
function emailKey(email) {
  return email.replace(/\./g, "_").replace(/@/g, "_at_");
}

function hashCode(code, email) {
  return crypto.createHash("sha256").update(code + ":" + email).digest("hex");
}

function isValidEmail(email) {
  return typeof email === "string" && /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email);
}

// ── 1. Код хүсэх ────────────────────────────────────────────────
exports.requestOtp = onCall(
  { secrets: [GMAIL_APP_PASSWORD], region: "us-central1", cors: true },
  async (request) => {
    const email = String((request.data && request.data.email) || "")
      .trim()
      .toLowerCase();

    if (!isValidEmail(email)) {
      throw new HttpsError("invalid-argument", "Зөв имэйл оруулна уу");
    }
    if (!ALLOWLIST.includes(email)) {
      // Эрхгүй имэйл рүү код ч явуулахгүй
      throw new HttpsError("permission-denied", "Энэ имэйл нэвтрэх эрхгүй байна");
    }

    const ref = admin.database().ref("otp_codes/" + emailKey(email));

    // Rate-limit: сүүлийн код 60 сек дотор бол дахин илгээхгүй
    const snap = await ref.get();
    if (snap.exists()) {
      const prev = snap.val();
      if (prev.createdAt && Date.now() - prev.createdAt < RESEND_COOLDOWN_MS) {
        throw new HttpsError(
          "resource-exhausted",
          "Дахин код хүсэхийн өмнө түр хүлээнэ үү"
        );
      }
    }

    const code = String(crypto.randomInt(0, 1000000)).padStart(6, "0");

    await ref.set({
      hash: hashCode(code, email), // зөвхөн hash хадгална
      expires: Date.now() + CODE_TTL_MS,
      attempts: 0,
      createdAt: Date.now(),
    });

    const transporter = nodemailer.createTransport({
      service: "gmail",
      auth: { user: SENDER_EMAIL, pass: GMAIL_APP_PASSWORD.value() },
    });

    await transporter.sendMail({
      from: `"${SENDER_NAME}" <${SENDER_EMAIL}>`,
      to: email,
      subject: "MRESOURCE — Нэвтрэх код",
      text:
        `Таны нэвтрэх код: ${code}\n\n` +
        `Энэ код 10 минутын дотор хүчинтэй.\n` +
        `Хэрэв та хүсээгүй бол энэ захиаг үл тоомсорлоно уу.`,
      html: otpEmailHtml(code),
    });

    logger.info("OTP sent", { email });
    return { ok: true };
  }
);

// ── 2. Код шалгах ───────────────────────────────────────────────
exports.verifyOtp = onCall(
  { region: "us-central1", cors: true },
  async (request) => {
    const email = String((request.data && request.data.email) || "")
      .trim()
      .toLowerCase();
    const code = String((request.data && request.data.code) || "").trim();

    if (!isValidEmail(email)) {
      throw new HttpsError("invalid-argument", "Зөв имэйл оруулна уу");
    }
    if (!/^\d{6}$/.test(code)) {
      throw new HttpsError("invalid-argument", "6 оронтой код оруулна уу");
    }

    const ref = admin.database().ref("otp_codes/" + emailKey(email));
    const snap = await ref.get();
    if (!snap.exists()) {
      throw new HttpsError("not-found", "Код олдсонгүй. Дахин код авна уу");
    }

    const rec = snap.val();

    if (Date.now() > rec.expires) {
      await ref.remove();
      throw new HttpsError(
        "deadline-exceeded",
        "Кодын хугацаа дууссан. Дахин код авна уу"
      );
    }
    if ((rec.attempts || 0) >= MAX_ATTEMPTS) {
      await ref.remove();
      throw new HttpsError(
        "resource-exhausted",
        "Хэт олон буруу оролдлого. Дахин код авна уу"
      );
    }

    // constant-time харьцуулалт
    const expected = String(rec.hash || "");
    const actual = hashCode(code, email);
    const match =
      expected.length === actual.length &&
      crypto.timingSafeEqual(Buffer.from(expected), Buffer.from(actual));

    if (!match) {
      await ref.update({ attempts: (rec.attempts || 0) + 1 });
      throw new HttpsError("permission-denied", "Код буруу байна");
    }

    // Код зөв → хэрэглэгчийг олох/үүсгэх, "allowed" claim, custom token
    let user;
    try {
      user = await admin.auth().getUserByEmail(email);
    } catch (e) {
      user = await admin.auth().createUser({ email, emailVerified: true });
    }

    await admin.auth().setCustomUserClaims(user.uid, { allowed: true });
    const token = await admin.auth().createCustomToken(user.uid);

    await ref.remove(); // код дахин ашиглагдахгүй (replay-ээс хамгаална)

    logger.info("OTP verified", { email, uid: user.uid });
    return { token };
  }
);

function otpEmailHtml(code) {
  return (
    '<div style="font-family:-apple-system,Segoe UI,Roboto,sans-serif;max-width:420px;' +
    'margin:0 auto;padding:32px 24px;background:#000;color:#f5f5f7;border-radius:16px;">' +
    '<h1 style="font-size:22px;margin:0 0 4px;">MRESOURCE</h1>' +
    '<p style="color:#a6a6aa;font-size:13px;margin:0 0 24px;">Real-time monitoring system</p>' +
    '<p style="font-size:15px;margin:0 0 12px;">Таны нэвтрэх код:</p>' +
    '<div style="font-size:38px;font-weight:700;letter-spacing:10px;background:#1c1c1e;' +
    'padding:18px;border-radius:12px;text-align:center;">' +
    code +
    "</div>" +
    '<p style="color:#a6a6aa;font-size:13px;margin:20px 0 0;">Код 10 минутын дотор хүчинтэй. ' +
    "Хэрэв та хүсээгүй бол энэ захиаг үл тоомсорлоно уу.</p>" +
    "</div>"
  );
}
