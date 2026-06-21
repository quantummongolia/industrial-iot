# Email OTP passwordless нэвтрэлт — Setup & Deploy

Дашбордын нэвтрэлтийг **6 оронтой имэйл код**-оор болгосон. Хүн → код, ESP → хуучин email/password (хэвээр).

Урсгал: имэйл оруул → код имэйлээр ирнэ → PWA дотор код бичнэ → нэвтэрнэ.
Browser handoff байхгүй тул **iOS PWA дээр ч ажиллана**.

---

## Архитектур

| Хэсэг | Файл |
|---|---|
| Cloud Functions (код үүсгэх/илгээх/шалгах) | `functions/index.js` |
| Илгээгч | Өөрийн Gmail (App Password, SMTP) |
| Код хадгалах (зөвхөн hash) | RTDB `/otp_codes` — `.read:false .write:false` |
| Allowlist (20-30 хүн) | `functions/index.js` → `ALLOWLIST` |
| Rules хамгаалалт | `auth.token.allowed === true` claim + ESP email fallback |
| Session хязгаар | 15 хоног (`index.html`, `auth_time`) |
| Login UI | `login.html`, `js/auth.js`, `js/login-form.js` |

---

## Нэг удаагийн тохиргоо

### 1. Blaze plan
Cloud Functions-д Blaze хэрэгтэй. Console → Upgrade → Blaze.
**Budget alert** ($1–5) тавь (хэтэрвэл мэдэгдэнэ).
> Танай хэмжээнд бодит зардал ~$0 (бүгд free tier дотор).

### 2. Gmail App Password → secret
2FA асаасан Gmail дээр App Password үүсгэ (https://myaccount.google.com/apppasswords), дараа нь:
```bash
firebase functions:secrets:set GMAIL_APP_PASSWORD
# асуухад 16 тэмдэгтийн App Password-оо paste хийнэ
```

### 3. Тохиргоо засах — `functions/index.js`
```js
const SENDER_EMAIL = "purewdorj0304@gmail.com";  // ← App Password үүсгэсэн Gmail
const ALLOWLIST = [
  "batbayar.ts@mandalresource.mn",
  // ... 20-30 хүн
];
```

### 4. Firebase Console шалгах
- Authentication → Sign-in method → **Email/Password идэвхтэй хэвээр** (ESP үүгээр нэвтэрдэг — БҮҮ унтраа).
- OTP-д нэмэлт provider асаах шаардлагагүй (custom token ашигладаг).

---

## Deploy

```bash
# 1. Functions dependencies
cd functions && npm install && cd ..

# 2. Functions deploy (secret-тэй)
firebase deploy --only functions

# 3. Rules deploy
firebase deploy --only database

# 4. Hosting deploy (UI)
npm run deploy   # эсвэл: firebase deploy --only hosting
```

> ⚠️ Rules deploy хийсний дараа **одоо нэвтэрсэн бүх хүн нэг удаа** дахин (OTP-ээр) нэвтэрнэ — учир нь хуучин session-д `allowed` claim байхгүй. Энэ нэг л удаа. Ирээдүйн deploy-д давтагдахгүй.

---

## Хүн нэмэх / хасах
`functions/index.js` → `ALLOWLIST` засаад:
```bash
firebase deploy --only functions
```
> Хасагдсан хүний **идэвхтэй session** 15 хоног хүртэл үргэлжилж магадгүй. Шууд таслах бол тухайн хэрэглэгчийн `allowed` claim-ийг устгаад token-ийг revoke хийнэ (Admin SDK).

---

## Troubleshooting

**`createCustomToken` signing алдаа** (`IAM ... signBlob`):
Functions-ийн service account-д эрх нэмнэ:
```bash
# Console → IAM → <project>@appspot.gserviceaccount.com (эсвэл compute SA)
# роль нэмэх:  Service Account Token Creator
```

**Код имэйл ирэхгүй / spam:**
- Gmail-аас org мэйл рүү заримдаа spam-д ордог. Spam/Junk шалга.
- Удаан давтагдвал илүү сайн deliverability бүхий провайдер (Resend/SendGrid) руу шилжиж болно.

**`functions/unauthenticated` эсвэл CORS:**
- Функцийн region `us-central1`, client default region таарч байгаа эсэхийг шалга.

---

## Аюулгүй байдлын тэмдэглэл
- Код **сервер дээр** үүснэ, RTDB-д зөвхөн **hash** хадгална.
- Хугацаа 10 мин, буруу оролдлого max 5, ашигласны дараа устгана (replay-ээс хамгаална).
- 60 сек cooldown (имэйл спамаас).
- Allowlist-д ороогүй имэйл рүү код **огт явахгүй**.
- (Нэмэлт) Rules-д 15 хоногийн хатуу backstop:
  `... && auth.token.auth_time * 1000 > now - 1296000000` — гэхдээ ESP branch-д бүү нэм.
