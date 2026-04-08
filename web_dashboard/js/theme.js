// ============================================================
//  THEME TOGGLE — dark/light with localStorage persistence
// ============================================================
function toggleTheme() {
  const html = document.documentElement;
  if (html.getAttribute("data-theme") === "light") {
    html.setAttribute("data-theme", "dark");
    localStorage.setItem("theme", "dark");
  } else {
    html.setAttribute("data-theme", "light");
    localStorage.setItem("theme", "light");
  }
}

// Restore saved theme immediately on script load (avoid flash)
(function () {
  const saved = localStorage.getItem("theme");
  if (saved === "light") document.documentElement.setAttribute("data-theme", "light");
})();
