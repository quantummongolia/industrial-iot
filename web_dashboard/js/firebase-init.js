// ============================================================
//  FIREBASE INIT
//  Requires firebase compat SDKs and config.js loaded BEFORE this file
// ============================================================
firebase.initializeApp(firebaseConfig);
const db = firebase.database();
window.db = db;
