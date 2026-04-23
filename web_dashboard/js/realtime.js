// ============================================================
//  REALTIME — Firebase RTDB listeners for live flowmeter data
//
//  Sensor mapping:
//    Slave ID 2 = Суларсан уусмал 1  → flowmeter1 → #fm1Flow / #fm1Total / #dataLed1
//    Slave ID 3 = Баян уусмал        → flowmeter2 → #fm2Flow / #fm2Total / #dataLed2
//    Slave ID 4 = Суларсан уусмал 2  → flowmeter3 → #fm3Flow / #fm3Total / #dataLed3
//
//  Public API:
//    initRealtime() — элемент кэш + Firebase listeners-ийг эхлүүлнэ
// ============================================================

let _readingCount = 0;
let _el = {};

function _blinkLed(ledEl) {
  if (!ledEl) return;
  ledEl.classList.add("data-led-active");
  setTimeout(function () { ledEl.classList.remove("data-led-active"); }, 400);
}

function _onFlowRate(key, val) {
  _readingCount++;
  const flow = parseFloat(val).toFixed(2);
  if (key === "fm1") {
    if (_el.fm1Flow) _el.fm1Flow.textContent = flow;
    _blinkLed(_el.dataLed1);
  } else if (key === "fm2") {
    if (_el.fm2Flow) _el.fm2Flow.textContent = flow;
    _blinkLed(_el.dataLed2);
  } else if (key === "fm3") {
    if (_el.fm3Flow) _el.fm3Flow.textContent = flow;
    _blinkLed(_el.dataLed3);
  }
  if (_el.readingCount) _el.readingCount.textContent = _readingCount;
  if (_el.lastUpdate)   _el.lastUpdate.textContent   = new Date().toLocaleTimeString();
}

function _onTotalizer(key, val) {
  const total = parseFloat(val).toFixed(2);
  if (key === "fm1") {
    if (_el.fm1Total) _el.fm1Total.textContent = total;
  } else if (key === "fm2") {
    if (_el.fm2Total) _el.fm2Total.textContent = total;
  } else if (key === "fm3") {
    if (_el.fm3Total) _el.fm3Total.textContent = total;
  }
}

function initRealtime() {
  _el = {
    fm1Flow:      document.getElementById("fm1Flow"),
    fm1Total:     document.getElementById("fm1Total"),
    fm2Flow:      document.getElementById("fm2Flow"),
    fm2Total:     document.getElementById("fm2Total"),
    fm3Flow:      document.getElementById("fm3Flow"),
    fm3Total:     document.getElementById("fm3Total"),
    dataLed1:     document.getElementById("dataLed1"),
    dataLed2:     document.getElementById("dataLed2"),
    dataLed3:     document.getElementById("dataLed3"),
    statusLed:    document.getElementById("statusLed"),
    statusText:   document.getElementById("statusText"),
    readingCount: document.getElementById("readingCount"),
    lastUpdate:   document.getElementById("lastUpdate"),
  };

  if (typeof firebase === "undefined" || !firebase.apps || !firebase.apps.length) {
    console.info("[realtime] Firebase skipped (SDK or app not initialized)");
    return;
  }

  const db = firebase.database();

  // Connection status → статус LED
  db.ref(".info/connected").on("value", (snap) => {
    if (!_el.statusLed || !_el.statusText) return;
    if (snap.val() === true) {
      _el.statusLed.classList.add("led-online");
      _el.statusText.textContent = "ONLINE";
    } else {
      _el.statusLed.classList.remove("led-online");
      _el.statusText.textContent = "OFFLINE";
    }
  });

  // Flowmeter 1 — Суларсан уусмал 1 (Slave ID 2)
  db.ref("/flow_system/flowmeter1/flow_rate").on("value", s => {
    if (s.val() !== null) _onFlowRate("fm1", s.val());
  });
  db.ref("/flow_system/flowmeter1/totalizer").on("value", s => {
    if (s.val() !== null) _onTotalizer("fm1", s.val());
  });

  // Flowmeter 2 — Баян уусмал (Slave ID 3)
  db.ref("/flow_system/flowmeter2/flow_rate").on("value", s => {
    if (s.val() !== null) _onFlowRate("fm2", s.val());
  });
  db.ref("/flow_system/flowmeter2/totalizer").on("value", s => {
    if (s.val() !== null) _onTotalizer("fm2", s.val());
  });

  // Flowmeter 3 — Суларсан уусмал 2 (Slave ID 4)
  db.ref("/flow_system/flowmeter3/flow_rate").on("value", s => {
    if (s.val() !== null) _onFlowRate("fm3", s.val());
  });
  db.ref("/flow_system/flowmeter3/totalizer").on("value", s => {
    if (s.val() !== null) _onTotalizer("fm3", s.val());
  });
}
