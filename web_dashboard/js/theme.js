// ============================================================
//  THEME — Dark / light toggle with localStorage persistence
//  Шинэ дата-theme attribute-ийг <html> дээр тавина.
//  Сонголтыг хадгалж, дараагийн удаа ачаалахдаа сэргээнэ.
// ============================================================

(function restoreThemeEarly() {
  // IIFE — FOUC (flash of unstyled content) үүсгэхгүйн тулд
  // script load болсон даруйд ажиллана.
  const saved = localStorage.getItem("theme");
  if (saved === "light") {
    document.documentElement.setAttribute("data-theme", "light");
  }
})();

function toggleTheme() {
  const html = document.documentElement;
  const next = html.getAttribute("data-theme") === "light" ? "dark" : "light";
  html.setAttribute("data-theme", next);
  localStorage.setItem("theme", next);
}
