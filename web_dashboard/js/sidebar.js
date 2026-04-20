// ============================================================
//  SIDEBAR — Tab switching + nested submenu + mobile drawer
//  Tailwind CSS compatible version
// ============================================================

function setupSidebar() {
  var sidebar  = document.getElementById("sidebar");
  var backdrop = document.getElementById("sidebarBackdrop");
  var toggle   = document.getElementById("sidebarToggle");
  var navTitleEl = document.getElementById("navPageTitle");

  var isMobile = function() {
    return window.matchMedia("(max-width: 768px)").matches;
  };

  function closeSidebar() {
    if (isMobile()) {
      sidebar.classList.add("-translate-x-full");
      sidebar.classList.remove("translate-x-0");
      backdrop.classList.add("hidden");
      backdrop.classList.remove("block");
    }
  }

  function openSidebar() {
    if (isMobile()) {
      sidebar.classList.remove("-translate-x-full");
      sidebar.classList.add("translate-x-0");
      backdrop.classList.remove("hidden");
      backdrop.classList.add("block");
    }
  }

  // On mobile, start with sidebar hidden
  function handleResize() {
    if (isMobile()) {
      sidebar.classList.add("-translate-x-full");
      sidebar.classList.remove("translate-x-0");
      backdrop.classList.add("hidden");
    } else {
      sidebar.classList.remove("-translate-x-full");
      sidebar.classList.remove("translate-x-0");
      backdrop.classList.add("hidden");
    }
  }

  handleResize();
  window.addEventListener("resize", handleResize);

  if (toggle) {
    toggle.addEventListener("click", function() {
      if (isMobile()) {
        if (sidebar.classList.contains("-translate-x-full")) openSidebar();
        else closeSidebar();
      } else {
        // Desktop: collapse/expand sidebar
        document.body.classList.toggle("sidebar-collapsed");
      }
    });
  }
  if (backdrop) backdrop.addEventListener("click", closeSidebar);

  // Tab switching (leaf nav items only)
  var tabItems = document.querySelectorAll(".nav-item:not(.has-submenu)");
  tabItems.forEach(function(item) {
    item.addEventListener("click", function() {
      var tab = item.dataset.tab;
      if (!tab) return;

      // Remove active from all nav-items
      document.querySelectorAll(".nav-item").forEach(function(i) {
        i.classList.remove("bg-accent-bg", "text-accent", "font-semibold");
        i.classList.add("text-text-soft");
      });
      // Add active to clicked
      item.classList.add("bg-accent-bg", "text-accent", "font-semibold");
      item.classList.remove("text-text-soft");

      // Toggle tab panels
      document.querySelectorAll(".tab-panel").forEach(function(p) {
        p.classList.remove("active");
        p.style.display = "none";
      });
      var panel = document.getElementById("tab-" + tab);
      if (panel) {
        panel.classList.add("active");
        panel.style.display = "block";
      }

      var label = item.textContent.trim();
      if (navTitleEl) navTitleEl.textContent = label;

      if (isMobile()) closeSidebar();
    });
  });

  // Level-2 submenu toggle
  document.querySelectorAll(".nav-item.has-submenu").forEach(function(item) {
    item.addEventListener("click", function(e) {
      e.stopPropagation();
      var id = item.dataset.submenu;
      var submenu = document.getElementById("submenu-" + id);
      if (!submenu) return;

      var isOpen = !submenu.classList.contains("hidden");
      if (isOpen) {
        submenu.classList.add("hidden");
        submenu.classList.remove("flex");
        item.querySelector(".submenu-chev").style.transform = "";
      } else {
        submenu.classList.remove("hidden");
        submenu.classList.add("flex");
        submenu.style.animation = "submenu-in 0.2s var(--ease-smooth)";
        item.querySelector(".submenu-chev").style.transform = "rotate(180deg)";
      }
    });
  });

  // Level-3 subsubmenu toggle
  document.querySelectorAll(".submenu-item.has-subsubmenu").forEach(function(item) {
    item.addEventListener("click", function(e) {
      e.stopPropagation();
      var id = item.dataset.subsubmenu;
      var sub = document.getElementById("subsubmenu-" + id);
      if (!sub) return;

      var isOpen = !sub.classList.contains("hidden");
      if (isOpen) {
        sub.classList.add("hidden");
        sub.classList.remove("flex");
        var chev = item.querySelector(".submenu-chev");
        if (chev) chev.style.transform = "";
      } else {
        sub.classList.remove("hidden");
        sub.classList.add("flex");
        sub.style.animation = "submenu-in 0.2s var(--ease-smooth)";
        var chev = item.querySelector(".submenu-chev");
        if (chev) chev.style.transform = "rotate(180deg)";
      }
    });
  });

  // Set initial active state
  document.querySelectorAll(".tab-panel").forEach(function(p) {
    p.classList.remove("active");
    p.style.display = "none";
  });
  var firstNav = document.querySelector('.nav-item[data-tab="flow-meters"]');
  if (firstNav) {
    firstNav.classList.add("bg-accent-bg", "text-accent", "font-semibold");
    firstNav.classList.remove("text-text-soft");
  }
  var firstPanel = document.getElementById("tab-flow-meters");
  if (firstPanel) {
    firstPanel.classList.add("active");
    firstPanel.style.display = "block";
  }
  if (navTitleEl && firstNav) navTitleEl.textContent = firstNav.textContent.trim();
}
