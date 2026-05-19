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
    // Teerem (Тээрэм tab)
    teeremWeight:     document.getElementById("teeremWeight"),
    teeremWeightTons: document.getElementById("teeremWeightTons"),
    teeremWeightLed:  document.getElementById("teeremWeightLed"),
    // Teerem хуулбар (Activity tab)
    teeremWeightAct:     document.getElementById("teeremWeightAct"),
    teeremWeightTonsAct: document.getElementById("teeremWeightTonsAct"),
    teeremWeightLedAct:  document.getElementById("teeremWeightLedAct"),
    // Butluur
    butluurWeight:     document.getElementById("butluurWeight"),
    butluurWeightTons: document.getElementById("butluurWeightTons"),
    butluurWeightLed:  document.getElementById("butluurWeightLed"),
    // Цахилгаан тоолуурууд (Slave ID → card)
    //   em01 ← Slave 2 (Боловсруулах үйлдвэр ХС)
    //   em02 ← Slave 3 (Нунтаглах хэсэг ХС)
    //   em04 ← Slave 4 (Бөмбөлөгт тээрэм 1)
    //   em05 ← Slave 5 (Бөмбөлөгт тээрэм 2)
    em01Power:  document.getElementById("emPower01"),
    em01Energy: document.getElementById("emEnergy01"),
    em01Led:    document.getElementById("emLed01"),
    em02Power:  document.getElementById("emPower02"),
    em02Energy: document.getElementById("emEnergy02"),
    em02Led:    document.getElementById("emLed02"),
    em04Power:   document.getElementById("emPower04"),
    em04Energy:  document.getElementById("emEnergy04"),
    em04Led:     document.getElementById("emLed04"),
    em04CurA:    document.getElementById("emCurrentA04"),
    em04CurB:    document.getElementById("emCurrentB04"),
    em04CurC:    document.getElementById("emCurrentC04"),
    em05Power:   document.getElementById("emPower05"),
    em05Energy:  document.getElementById("emEnergy05"),
    em05Led:     document.getElementById("emLed05"),
    em05CurA:    document.getElementById("emCurrentA05"),
    em05CurB:    document.getElementById("emCurrentB05"),
    em05CurC:    document.getElementById("emCurrentC05"),
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

  // ── Teerem ────────────────────────────────────────────
  db.ref("/teerem/weight_rate").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val()).toFixed(2);
    if (_el.teeremWeight)    _el.teeremWeight.textContent    = v;
    if (_el.teeremWeightAct) _el.teeremWeightAct.textContent = v;
    _blinkLed(_el.teeremWeightLed);
    _blinkLed(_el.teeremWeightLedAct);
  });
  db.ref("/teerem/cumulative_kg").on("value", s => {
    if (s.val() === null) return;
    const t = (parseInt(s.val(), 10) / 1000).toFixed(3);
    if (_el.teeremWeightTons)    _el.teeremWeightTons.textContent    = t;
    if (_el.teeremWeightTonsAct) _el.teeremWeightTonsAct.textContent = t;
  });

  // ── Butluur ───────────────────────────────────────────
  db.ref("/butluur/weight_rate").on("value", s => {
    if (s.val() === null) return;
    if (_el.butluurWeight) _el.butluurWeight.textContent = parseFloat(s.val()).toFixed(2);
    _blinkLed(_el.butluurWeightLed);
  });
  db.ref("/butluur/cumulative_kg").on("value", s => {
    if (s.val() === null) return;
    if (_el.butluurWeightTons) _el.butluurWeightTons.textContent = (parseInt(s.val(), 10) / 1000).toFixed(3);
  });

  // ── Цахилгаан тоолуурууд ──────────────────────────────
  // Power + energy дөрвөн card-д адил, гүйдэл 04/05-д л байдаг.
  function bindMeter(key, hasCurrents) {
    const power  = _el[key + "Power"];
    const energy = _el[key + "Energy"];
    const led    = _el[key + "Led"];
    const curA   = _el[key + "CurA"];
    const curB   = _el[key + "CurB"];
    const curC   = _el[key + "CurC"];

    db.ref("/energy_meters/" + key + "/power_kw").on("value", s => {
      if (s.val() === null) return;
      if (power) power.textContent = parseFloat(s.val()).toFixed(2);
      _blinkLed(led);
    });
    db.ref("/energy_meters/" + key + "/total_energy_kwh").on("value", s => {
      if (s.val() === null) return;
      if (energy) energy.textContent = parseFloat(s.val()).toFixed(3);
    });
    if (hasCurrents) {
      db.ref("/energy_meters/" + key + "/current_a").on("value", s => {
        if (s.val() === null) return;
        if (curA) curA.textContent = parseFloat(s.val()).toFixed(2);
      });
      db.ref("/energy_meters/" + key + "/current_b").on("value", s => {
        if (s.val() === null) return;
        if (curB) curB.textContent = parseFloat(s.val()).toFixed(2);
      });
      db.ref("/energy_meters/" + key + "/current_c").on("value", s => {
        if (s.val() === null) return;
        if (curC) curC.textContent = parseFloat(s.val()).toFixed(2);
      });
    }
  }

  bindMeter("em01", false);
  bindMeter("em02", false);
  bindMeter("em04", true);
  bindMeter("em05", true);
}
