// ============================================================
//  REALTIME DATA — RTDB listeners + DOM updates
//  Requires firebase-init.js (window.db) loaded BEFORE this file
//
//  Sensor mapping:
//    flowmeter1  →  Суларсан уусмал 2  (Нуруулдан уусгалт tab)
//    flowmeter2  →  Баян уусмал        (Баян уусмалын сан tab)
// ============================================================
let readingCount = 0;
let lastDataTime = 0;
let el = {};

function tickClock() {
  const now = new Date();
  const h = String(now.getHours()).padStart(2, "0");
  const m = String(now.getMinutes()).padStart(2, "0");
  const s = String(now.getSeconds()).padStart(2, "0");
  if (el.headerTime) el.headerTime.textContent = `${h}:${m}:${s}`;
}

function blinkLed(ledEl) {
  if (!ledEl) return;
  ledEl.classList.add("blink");
  setTimeout(() => ledEl.classList.remove("blink"), 400);
}

function updateFlowmeter(key, flow) {
  lastDataTime = Date.now();
  readingCount++;
  if (key === "fm1" && el.fm1Flow) {
    el.fm1Flow.textContent = flow.toFixed(2);
    blinkLed(el.dataLed1);
  }
  if (key === "fm2" && el.fm2Flow) {
    el.fm2Flow.textContent = flow.toFixed(2);
    blinkLed(el.dataLed2);
  }
}

function initRealtime() {
  el = {
    fm1Flow: document.getElementById("fm1Flow"),
    fm2Flow: document.getElementById("fm2Flow"),
    dataLed1: document.getElementById("dataLed1"),
    dataLed2: document.getElementById("dataLed2"),
    statusLed: document.getElementById("statusLed"),
    statusText: document.getElementById("statusText"),
    headerTime: document.getElementById("headerTime")
  };

  tickClock();
  setInterval(tickClock, 1000);

  const dbRef = window.db;
  if (!dbRef) {
    console.error("[realtime] window.db is not initialised");
    return;
  }

  dbRef.ref(".info/connected").on("value", (snap) => {
    if (!el.statusLed) return;
    if (snap.val() === true) {
      el.statusLed.classList.add("online");
      el.statusText.textContent = "ONLINE";
    } else {
      el.statusLed.classList.remove("online");
      el.statusText.textContent = "OFFLINE";
    }
  });

  dbRef.ref("/flow_system/last_updated").on("value", () => { lastDataTime = Date.now(); });
  dbRef.ref("/flow_system/flowmeter1/current_flow").on("value", s => {
    const v = s.val(); if (v !== null) updateFlowmeter("fm1", parseFloat(v));
  });
  dbRef.ref("/flow_system/flowmeter2/current_flow").on("value", s => {
    const v = s.val(); if (v !== null) updateFlowmeter("fm2", parseFloat(v));
  });
}
