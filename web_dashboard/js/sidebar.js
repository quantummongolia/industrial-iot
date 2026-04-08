// ============================================================
//  SIDEBAR — Tab switching + mobile drawer
// ============================================================
function setupSidebar() {
  const items = document.querySelectorAll(".nav-item[data-tab]");
  const sidebar = document.getElementById("sidebar");
  const backdrop = document.getElementById("sidebarBackdrop");
  const toggle = document.getElementById("sidebarToggle");

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

  const titleEl = document.getElementById("pageTitle");

  function activateTab(tab) {
    let activeItem = null;
    items.forEach(i => {
      const match = i.dataset.tab === tab;
      i.classList.toggle("active", match);
      if (match) activeItem = i;
    });
    document.querySelectorAll(".tab-panel").forEach(p => {
      p.classList.toggle("active", p.id === "tab-" + tab);
    });
    if (titleEl && activeItem) {
      // Use the trimmed text content of the active nav item as the page title
      titleEl.textContent = activeItem.textContent.trim();
    }
    localStorage.setItem("lastTab", tab);
    if (window.matchMedia("(max-width: 700px)").matches) closeSidebar();
  }

  items.forEach(item => {
    item.addEventListener("click", () => activateTab(item.dataset.tab));
  });

  // Restore last visited tab if it still exists, else default
  const last = localStorage.getItem("lastTab");
  if (last && document.getElementById("tab-" + last)) {
    activateTab(last);
  }
}
