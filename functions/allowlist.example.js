// Энэ файлыг "allowlist.js" нэрээр хувилж, доорх утгуудаа засна.
//   cp functions/allowlist.example.js functions/allowlist.js
// allowlist.js нь .gitignore-д орсон тул GitHub-д орохгүй.

module.exports = {
  // Илгээгч Gmail (App Password үүсгэсэн хаяг)
  SENDER_EMAIL: "your-gmail@gmail.com",

  // Нэвтрэх эрхтэй хүмүүсийн имэйл
  ALLOWLIST: [
    "user1@mandalresource.mn",
    "user2@mandalresource.mn",
    // ...
  ],
};
