// ============================================================
//  SERVICE WORKER — PWA caching
//  Strategy: Network-first for code (HTML/JS/CSS) so deploys
//  propagate instantly; cache-first for images/fonts.
// ============================================================

var CACHE_NAME = 'mresource-' + Date.now();  // New cache on every deploy
var OFFLINE_URL = '/404.html';

var PRECACHE_URLS = [
  '/',
  '/index.html',
  '/login.html',
  '/404.html',
  '/manifest.json',
  '/img/industrial-iot-icon.png'
];

self.addEventListener('install', function(event) {
  event.waitUntil(
    caches.open(CACHE_NAME).then(function(cache) {
      return cache.addAll(PRECACHE_URLS);
    }).then(function() {
      return self.skipWaiting();  // Activate new SW immediately
    })
  );
});

self.addEventListener('activate', function(event) {
  event.waitUntil(
    caches.keys().then(function(cacheNames) {
      return Promise.all(
        cacheNames.filter(function(name) { return name !== CACHE_NAME; })
                  .map(function(name) { return caches.delete(name); })
      );
    }).then(function() {
      return self.clients.claim();
    }).then(function() {
      // Хуучин cache-тай хэрэглэгчдийг автоматаар refresh хийнэ.
      // update-manager.js байхгүй хуучин хувилбарууд ч энэ замаар шинэчлэгдэнэ.
      return self.clients.matchAll({ type: 'window' }).then(function(clients) {
        return Promise.all(clients.map(function(client) {
          return client.navigate(client.url);
        }));
      });
    })
  );
});

self.addEventListener('fetch', function(event) {
  var request = event.request;
  if (request.method !== 'GET') return;

  // Skip 3rd-party APIs
  if (request.url.includes('firebasedatabase.app') ||
      request.url.includes('googleapis.com') ||
      request.url.includes('gstatic.com') ||
      request.url.includes('api.ipify.org') ||
      request.url.includes('cdnjs.cloudflare.com')) {
    return;
  }

  var url = new URL(request.url);

  // version.json — үргэлж network, хэзээ ч cache хийхгүй (update detection)
  if (url.pathname === '/version.json') {
    event.respondWith(fetch(request, { cache: 'no-store' }));
    return;
  }

  var isCode = /\.(js|css|html)$/.test(url.pathname) || request.mode === 'navigate';

  if (isCode) {
    // Network-first: code өөрчлөгдвөл шууд харагдана
    event.respondWith(
      fetch(request).then(function(response) {
        if (response.status === 200) {
          var clone = response.clone();
          caches.open(CACHE_NAME).then(function(cache) { cache.put(request, clone); });
        }
        return response;
      }).catch(function() {
        return caches.match(request).then(function(cached) {
          return cached || caches.match(OFFLINE_URL);
        });
      })
    );
    return;
  }

  // Cache-first for images/fonts/other static assets
  event.respondWith(
    caches.match(request).then(function(cached) {
      if (cached) return cached;
      return fetch(request).then(function(response) {
        if (response.status === 200) {
          var clone = response.clone();
          caches.open(CACHE_NAME).then(function(cache) { cache.put(request, clone); });
        }
        return response;
      });
    })
  );
});
