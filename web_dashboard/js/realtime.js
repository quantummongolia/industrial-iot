// ============================================================
//  REALTIME — Firebase RTDB listeners for live flowmeter data
//  ------------------------------------------------------------
//  Энэ модуль Firebase SDK болон config.js (firebaseConfig) байгаа эсэхээс
//  хамаарна. Хоёрын аль нь ч байхгүй тохиолдолд аюулгүйгээр skip хийнэ —
//  ингэснээр pure UI/UX ажил Firebase эвдэхгүйгээр үргэлжилж чадна.
//
//  Sensor mapping:
//    flowmeter1  →  #fm1Flow  /  #dataLed1
//    flowmeter2  →  #fm2Flow  /  #dataLed2
//    total       →  #fmTotal  (fm1 + fm2)
//
//  Public API:
//    initRealtime() — элемент кэш + Firebase listeners-ийг эхлүүлнэ
// ============================================================

let _readingCount = 0;
let _lastDataTime = 0;
let _fm1 = 0;
let _fm2 = 0;
let _el  = {};

// ---------- Helpers ----------
function _blinkLed(ledEl) {
  if (!ledEl) return;
  ledEl.classList.add("blink");
  setTimeout(() => ledEl.classList.remove("blink"), 400);
}

function _updateFlowmeter(key, flow) {
  _lastDataTime = Date.now();
  _readingCount++;

  if (key === "fm1") {
    _fm1 = flow;
    if (_el.fm1Flow) _el.fm1Flow.textContent = flow.toFixed(2);
    _blinkLed(_el.dataLed1);
  }
  if (key === "fm2") {
    _fm2 = flow;
    if (_el.fm2Flow) _el.fm2Flow.textContent = flow.toFixed(2);
    _blinkLed(_el.dataLed2);
  }

  if (_el.fmTotal)      _el.fmTotal.textContent      = (_fm1 + _fm2).toFixed(2);
  if (_el.readingCount) _el.readingCount.textContent = _readingCount;
  if (_el.lastUpdate)   _el.lastUpdate.textContent   = new Date().toLocaleTimeString();
}

// ---------- Boot ----------
function initRealtime() {
  // DOM элементүүдийг нэг удаа кэшлэнэ
  _el = {
    fm1Flow:      document.getElementById("fm1Flow"),
    fm2Flow:      document.getElementById("fm2Flow"),
    fmTotal:      document.getElementById("fmTotal"),
    dataLed1:     document.getElementById("dataLed1"),
    dataLed2:     document.getElementById("dataLed2"),
    statusLed:    document.getElementById("statusLed"),
    statusText:   document.getElementById("statusText"),
    readingCount: document.getElementById("readingCount"),
    lastUpdate:   document.getElementById("lastUpdate"),
  };

  // Firebase optional — SDK эсвэл config байхгүй бол чимээгүй гарна
  if (typeof firebase === "undefined" || typeof firebaseConfig === "undefined") {
    console.info("[realtime] Firebase skipped (SDK or config missing)");
    return;
  }

  try {
    firebase.initializeApp(firebaseConfig);
  } catch (e) {
    console.warn("[realtime] firebase.initializeApp failed:", e);
    return;
  }

  const db = firebase.database();

  // Connection status → статус LED
  db.ref(".info/connected").on("value", (snap) => {
    if (!_el.statusLed || !_el.statusText) return;
    if (snap.val() === true) {
      _el.statusLed.classList.add("online");
      _el.statusText.textContent = "ONLINE";
    } else {
      _el.statusLed.classList.remove("online");
      _el.statusText.textContent = "OFFLINE";
    }
  });

  // Flow updates
  db.ref("/flow_system/last_updated").on("value", () => {
    _lastDataTime = Date.now();
  });
  db.ref("/flow_system/flowmeter1/current_flow").on("value", s => {
    const v = s.val();
    if (v !== null) _updateFlowmeter("fm1", parseFloat(v));
  });
  db.ref("/flow_system/flowmeter2/current_flow").on("value", s => {
    const v = s.val();
    if (v !== null) _updateFlowmeter("fm2", parseFloat(v));
  });
}
