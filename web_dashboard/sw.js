// ============================================================
//  SERVICE WORKER — Offline-first caching for PWA
//  Strategy: Network-first for HTML/API, Cache-first for assets
// ============================================================

var CACHE_NAME = 'mresource-v1';
var OFFLINE_URL = '/404.html';

// Assets to pre-cache on install
var PRECACHE_URLS = [
  '/',
  '/index.html',
  '/login.html',
  '/404.html',
  '/css/app.css',
  '/js/theme.js',
  '/js/auth.js',
  '/js/login-form.js',
  '/js/sidebar.js',
  '/js/clock.js',
  '/js/ip-detect.js',
  '/js/realtime.js',
  '/js/toast.js',
  '/js/app.js',
  '/img/industrial-iot-icon.png',
  '/manifest.json'
];

// Install — pre-cache core assets
self.addEventListener('install', function(event) {
  event.waitUntil(
    caches.open(CACHE_NAME).then(function(cache) {
      return cache.addAll(PRECACHE_URLS);
    }).then(function() {
      return self.skipWaiting();
    })
  );
});

// Activate — clean up old caches
self.addEventListener('activate', function(event) {
  event.waitUntil(
    caches.keys().then(function(cacheNames) {
      return Promise.all(
        cacheNames.filter(function(name) {
          return name !== CACHE_NAME;
        }).map(function(name) {
          return caches.delete(name);
        })
      );
    }).then(function() {
      return self.clients.claim();
    })
  );
});

// Fetch — Network-first for navigation, cache-first for assets
self.addEventListener('fetch', function(event) {
  var request = event.request;

  // Skip non-GET requests
  if (request.method !== 'GET') return;

  // Skip Firebase and external API requests
  if (request.url.includes('firebasedatabase.app') ||
      request.url.includes('googleapis.com') ||
      request.url.includes('gstatic.com') ||
      request.url.includes('api.ipify.org') ||
      request.url.includes('cdnjs.cloudflare.com')) {
    return;
  }

  // Navigation requests — network first, fallback to cache
  if (request.mode === 'navigate') {
    event.respondWith(
      fetch(request).catch(function() {
        return caches.match(request).then(function(cached) {
          return cached || caches.match(OFFLINE_URL);
        });
      })
    );
    return;
  }

  // Static assets — cache first, fallback to network
  event.respondWith(
    caches.match(request).then(function(cached) {
      if (cached) return cached;

      return fetch(request).then(function(response) {
        // Cache successful responses
        if (response.status === 200) {
          var responseClone = response.clone();
          caches.open(CACHE_NAME).then(function(cache) {
            cache.put(request, responseClone);
          });
        }
        return response;
      }).catch(function() {
        // Offline fallback for HTML
        if (request.headers.get('accept').includes('text/html')) {
          return caches.match(OFFLINE_URL);
        }
      });
    })
  );
});
