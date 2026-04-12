// ============================================================
//  SIDEBAR — Tab switching + nested submenu + mobile drawer
//  ------------------------------------------------------------
//  index.html-ийн sidebar бүтэцтэй нийцтэй:
//    .nav-item[data-tab]            — хэвийн tab шилжүүлэгч
//    .nav-item.has-submenu          — Process Monitoring dropdown (level 2)
//    .submenu-item.has-subsubmenu   — Дэд цэс (level 3)
//
//  Public API:
//    setupSidebar() — бүх click listener-ийг бүртгэнэ
// ============================================================

function setupSidebar() {
  const sidebar  = document.getElementById("sidebar");
  const backdrop = document.getElementById("sidebarBackdrop");
  const toggle   = document.getElementById("sidebarToggle");
  const titleEl  = document.getElementById("pageTitle");

  // ---------- Mobile drawer open/close ----------
  function closeSidebar() {
    sidebar.classList.remove("expanded");
    backdrop.classList.remove("active");
  }
  function openSidebar() {
    sidebar.classList.add("expanded");
    backdrop.classList.add("active");
  }
  if (toggle) {
    toggle.addEventListener("click", () => {
      if (sidebar.classList.contains("expanded")) closeSidebar();
      else openSidebar();
    });
  }
  if (backdrop) backdrop.addEventListener("click", closeSidebar);

  // ---------- Tab switching (leaf nav items only) ----------
  const tabItems = document.querySelectorAll(".nav-item:not(.has-submenu)");
  tabItems.forEach(item => {
    item.addEventListener("click", () => {
      const tab = item.dataset.tab;
      if (!tab) return;

      // active class-ыг бүх nav-item-аас ав
      document.querySelectorAll(".nav-item").forEach(i => i.classList.remove("active"));
      item.classList.add("active");

      // харгалзах tab-panel-ийг идэвхжүүл
      document.querySelectorAll(".tab-panel").forEach(p => p.classList.remove("active"));
      const panel = document.getElementById("tab-" + tab);
      if (panel) panel.classList.add("active");

      // page title-ийг шинэчил
      if (titleEl) titleEl.textContent = item.textContent.trim();

      // мобайл дээр drawer-ийг хаа
      if (window.matchMedia("(max-width: 700px)").matches) closeSidebar();
    });
  });

  // ---------- Level-2 submenu toggle (Process Monitoring) ----------
  document.querySelectorAll(".nav-item.has-submenu").forEach(item => {
    item.addEventListener("click", (e) => {
      e.stopPropagation();
      const id = item.dataset.submenu;
      const submenu = document.getElementById("submenu-" + id);
      if (!submenu) return;
      submenu.classList.toggle("open");
      item.classList.toggle("open");
    });
  });

  // ---------- Level-3 subsubmenu toggle ----------
  document.querySelectorAll(".submenu-item.has-subsubmenu").forEach(item => {
    item.addEventListener("click", (e) => {
      e.stopPropagation();
      const id = item.dataset.subsubmenu;
      const subsubmenu = document.getElementById("subsubmenu-" + id);
      if (!subsubmenu) return;
      subsubmenu.classList.toggle("open");
      item.classList.toggle("open");
    });
  });
}
