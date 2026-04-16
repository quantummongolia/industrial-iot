// ============================================================
//  TOAST — Lightweight notification system
//  showToast(message, type?, duration?)
//  type: 'info' | 'success' | 'warning' | 'error'
// ============================================================

function showToast(message, type, duration) {
  type = type || 'info';
  duration = duration || 3000;

  var container = document.getElementById('toastContainer');
  if (!container) return;

  var colors = {
    info:    'border-accent/30 text-accent',
    success: 'border-success/30 text-success',
    warning: 'border-warning/30 text-warning',
    error:   'border-danger/30 text-danger'
  };

  var toast = document.createElement('div');
  toast.className = 'flex items-center gap-2 px-4 py-3 bg-bg-elevated border rounded-DEFAULT text-sm font-medium shadow-lg backdrop-blur-xl transition-all duration-300 translate-x-0 opacity-100 ' + (colors[type] || colors.info);
  toast.textContent = message;
  container.appendChild(toast);

  setTimeout(function() {
    toast.style.opacity = '0';
    toast.style.transform = 'translateX(100%)';
    setTimeout(function() { toast.remove(); }, 300);
  }, duration);
}
