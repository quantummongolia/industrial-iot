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
    // Ultrasonic level transmitter (Slave 1) — Суларсан уусмал савны түвшин
    ulsLevel:     document.getElementById("ulsLevel"),
    ulsLevelBar:  document.getElementById("ulsLevelBar"),
    ulsLevelLed:  document.getElementById("ulsLevelLed"),
    // Ultrasonic level (Teerem Slave 7) — Эргэлтийн усан сан
    waterTankLevel:    document.getElementById("waterTankLevel"),
    waterTankLevelBar: document.getElementById("waterTankLevelBar"),
    waterTankLevelLed: document.getElementById("waterTankLevelLed"),
    statusLed:    document.getElementById("statusLed"),
    statusText:   document.getElementById("statusText"),
    readingCount: document.getElementById("readingCount"),
    lastUpdate:   document.getElementById("lastUpdate"),
    // Teerem (Тээрэм tab)
    teeremWeight:     document.getElementById("teeremWeight"),
    teeremWeightTons: document.getElementById("teeremWeightTons"),
    teeremWeightLed:  document.getElementById("teeremWeightLed"),
    // Тээрмийн тэжээлийн ус — Teerem tab + Activity tab дублицат
    feedWaterFlow:       document.getElementById("feedWaterFlow"),
    feedWaterTotal:      document.getElementById("feedWaterTotal"),
    feedWaterLed:        document.getElementById("feedWaterLed"),
    feedWaterFlowAct:    document.getElementById("feedWaterFlowAct"),
    feedWaterTotalAct:   document.getElementById("feedWaterTotalAct"),
    feedWaterLedAct:     document.getElementById("feedWaterLedAct"),
    // Teerem хуулбар (Activity tab)
    teeremWeightAct:     document.getElementById("teeremWeightAct"),
    teeremWeightTonsAct: document.getElementById("teeremWeightTonsAct"),
    teeremWeightLedAct:  document.getElementById("teeremWeightLedAct"),
    // Butluur
    butluurWeight:     document.getElementById("butluurWeight"),
    butluurWeightTons: document.getElementById("butluurWeightTons"),
    butluurWeightLed:  document.getElementById("butluurWeightLed"),
    // 01-WT-01 Бутлуурын жин (Activity tab card)
    wt01Weight:     document.getElementById("wt01Weight"),
    wt01WeightTons: document.getElementById("wt01WeightTons"),
    wt01Led:        document.getElementById("wt01Led"),
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
    em03Power:   document.getElementById("emPower03"),
    em03Energy:  document.getElementById("emEnergy03"),
    em03Led:     document.getElementById("emLed03"),
    em03CurA:    document.getElementById("emCurrentA03"),
    em03CurB:    document.getElementById("emCurrentB03"),
    em03CurC:    document.getElementById("emCurrentC03"),
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
    // ushg ESP32 — 2× SPM33
    //   em06 ← Slave 1 (Өтгөрүүлэгч УС)
    //   em07 ← Slave 2 (Уусгалтын ган УС)
    em06Power:  document.getElementById("emPower06"),
    em06Energy: document.getElementById("emEnergy06"),
    em06Led:    document.getElementById("emLed06"),
    em07Power:  document.getElementById("emPower07"),
    em07Energy: document.getElementById("emEnergy07"),
    em07Led:    document.getElementById("emLed07"),
    // pressfilter ESP32 — 4× SPM33 (3 transceiver)
    //   em08 ← Bus A Slave 2 (Шүүн шахах УС)
    //   em09 ← Bus A Slave 1 (Десорбци ХС)
    //   em10 ← Bus B Slave 3 (Нуруулдан уусгалт ХС)
    //   em11 ← Bus B Slave 4 (Компрессор ХС)
    em08Power:  document.getElementById("emPower08"),
    em08Energy: document.getElementById("emEnergy08"),
    em08Led:    document.getElementById("emLed08"),
    em09Power:  document.getElementById("emPower09"),
    em09Energy: document.getElementById("emEnergy09"),
    em09Led:    document.getElementById("emLed09"),
    em10Power:  document.getElementById("emPower10"),
    em10Energy: document.getElementById("emEnergy10"),
    em10Led:    document.getElementById("emLed10"),
    em11Power:  document.getElementById("emPower11"),
    em11Energy: document.getElementById("emEnergy11"),
    em11Led:    document.getElementById("emLed11"),
    // em12..em17 — gen / hothon / mech / lab цахилгаан тоолуурууд
    em12Power:  document.getElementById("emPower12"),
    em12Energy: document.getElementById("emEnergy12"),
    em12Led:    document.getElementById("emLed12"),
    em13Power:  document.getElementById("emPower13"),
    em13Energy: document.getElementById("emEnergy13"),
    em13Led:    document.getElementById("emLed13"),
    em14Power:  document.getElementById("emPower14"),
    em14Energy: document.getElementById("emEnergy14"),
    em14Led:    document.getElementById("emLed14"),
    em15Power:  document.getElementById("emPower15"),
    em15Energy: document.getElementById("emEnergy15"),
    em15Led:    document.getElementById("emLed15"),
    em16Power:  document.getElementById("emPower16"),
    em16Energy: document.getElementById("emEnergy16"),
    em16Led:    document.getElementById("emLed16"),
    em17Power:  document.getElementById("emPower17"),
    em17Energy: document.getElementById("emEnergy17"),
    em17Led:    document.getElementById("emLed17"),
    // Баян уусмалын сан (Supmea ultrasonic — pressfilter Bus C Slave 5)
    bayanLevel:    document.getElementById("bayanLevel"),
    bayanLevelBar: document.getElementById("bayanLevelBar"),
    bayanLevelLed: document.getElementById("bayanLevelLed"),
    // Цэвэр усан сан (Supmea ultrasonic — pressfilter Bus C Slave 6)
    cleanWaterLevel:    document.getElementById("cleanWaterLevel"),
    cleanWaterLevelBar: document.getElementById("cleanWaterLevelBar"),
    cleanWaterLevelLed: document.getElementById("cleanWaterLevelLed"),
    cleanWaterMax:      document.getElementById("cleanWaterMax"),
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

  // Ultrasonic level transmitter — Суларсан уусмал савны түвшин (Slave ID 1)
  // Bar дүүргэлт 0..2.20m хооронд, max-аас давсан бол 100%-аар clamp хийнэ.
  const ULS_MAX_M = 2.20;
  db.ref("/flow_system/level_sensor/level").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val());
    if (_el.ulsLevel) _el.ulsLevel.textContent = v.toFixed(2);
    if (_el.ulsLevelBar) {
      const pct = Math.max(0, Math.min(100, (v / ULS_MAX_M) * 100));
      _el.ulsLevelBar.style.width = pct + "%";
    }
    _blinkLed(_el.ulsLevelLed);
  });

  // Баян уусмалын сан (Supmea ultrasonic — pressfilter Bus C Slave 5)
  // Суларсан уусмалын сантай яг адил төхөөрөмж, ижил bar хязгаар (0..2.20m).
  db.ref("/pressfilter/bayan_tank/level").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val());
    if (_el.bayanLevel) _el.bayanLevel.textContent = v.toFixed(2);
    if (_el.bayanLevelBar) {
      const pct = Math.max(0, Math.min(100, (v / ULS_MAX_M) * 100));
      _el.bayanLevelBar.style.width = pct + "%";
    }
    _blinkLed(_el.bayanLevelLed);
  });

  // Цэвэр усан сан (Supmea ultrasonic — pressfilter Bus C Slave 6)
  // Bar-ийн дээд хязгаарыг index.html дахь #cleanWaterLevelBar data-max-аас уншина.
  db.ref("/pressfilter/clean_water_tank/level").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val());
    if (_el.cleanWaterLevel) _el.cleanWaterLevel.textContent = v.toFixed(2);
    if (_el.cleanWaterLevelBar) {
      const max = parseFloat(_el.cleanWaterLevelBar.dataset.max) || ULS_MAX_M;
      if (_el.cleanWaterMax) _el.cleanWaterMax.textContent = max.toFixed(2) + " m";
      const pct = Math.max(0, Math.min(100, (v / max) * 100));
      _el.cleanWaterLevelBar.style.width = pct + "%";
    }
    _blinkLed(_el.cleanWaterLevelLed);
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

  // ── Тээрмийн тэжээлийн ус (Slave 6 flowmeter) ────────
  db.ref("/teerem/feed_water/flow_rate").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val()).toFixed(2);
    if (_el.feedWaterFlow)    _el.feedWaterFlow.textContent    = v;
    if (_el.feedWaterFlowAct) _el.feedWaterFlowAct.textContent = v;
    _blinkLed(_el.feedWaterLed);
    _blinkLed(_el.feedWaterLedAct);
  });
  db.ref("/teerem/feed_water/totalizer").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val()).toFixed(2);
    if (_el.feedWaterTotal)    _el.feedWaterTotal.textContent    = v;
    if (_el.feedWaterTotalAct) _el.feedWaterTotalAct.textContent = v;
  });

  // ── Эргэлтийн усан сан — Ultrasonic Level (Teerem Slave 7) ─────────
  const WATER_TANK_MAX_M = 3.0;
  db.ref("/teerem/water_tank/level").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val());
    if (_el.waterTankLevel) _el.waterTankLevel.textContent = v.toFixed(2);
    if (_el.waterTankLevelBar) {
      const pct = Math.max(0, Math.min(100, (v / WATER_TANK_MAX_M) * 100));
      _el.waterTankLevelBar.style.width = pct + "%";
    }
    _blinkLed(_el.waterTankLevelLed);
  });

  // ── Butluur (01-WT-01 Бутлуурын жин) ───────────────────
  // weight_rate = t/h, cumulative_t = нийт тонн (t).
  db.ref("/butluur/weight_rate").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val()).toFixed(2);
    if (_el.butluurWeight) _el.butluurWeight.textContent = v;
    if (_el.wt01Weight)    _el.wt01Weight.textContent    = v;
    _blinkLed(_el.butluurWeightLed);
    _blinkLed(_el.wt01Led);
  });
  db.ref("/butluur/cumulative_t").on("value", s => {
    if (s.val() === null) return;
    const t = parseFloat(s.val()).toFixed(3);
    if (_el.butluurWeightTons) _el.butluurWeightTons.textContent = t;
    if (_el.wt01WeightTons)    _el.wt01WeightTons.textContent    = t;
  });

  // ── Цахилгаан тоолуурууд + Нийт эрчим хүч хураангуй ───────────────
  // ALL meters here — ажиллаагүй самбарууд ч жагсаагдсан (Firebase утга
  // байхгүй бол UI placeholder 0.00 хэвээр, 10 сек дараа гурвалжин гарна).
  const METERS = [
    { key: "em01", name: "Боловсруулах үйлдвэр ХС", hasCurrents: false },
    { key: "em02", name: "Нунтаглах хэсэг ХС",       hasCurrents: false },
    { key: "em03", name: "Бутлуур ЕС",               hasCurrents: true  },
    { key: "em04", name: "Бөмбөлөгт тээрэм 1",       hasCurrents: true  },
    { key: "em05", name: "Бөмбөлөгт тээрэм 2",       hasCurrents: true  },
    { key: "em06", name: "Өтгөрүүлэгч УС",            hasCurrents: false },
    { key: "em07", name: "Уусгалтын ган УС",          hasCurrents: false },
    { key: "em08", name: "Шүүн шахах УС",             hasCurrents: false },
    { key: "em09", name: "Десорбци ХС",               hasCurrents: false },
    { key: "em10", name: "Нуруулдан уусгалт ХС",      hasCurrents: false },
    { key: "em11", name: "Компрессор ХС",             hasCurrents: false },
    { key: "em12", name: "Шатахуун түгээх ХС",        hasCurrents: false },
    { key: "em13", name: "Уурын зуух ХС",             hasCurrents: false },
    { key: "em14", name: "Хотхон ХС",                 hasCurrents: false },
    { key: "em15", name: "Механик ХС",                hasCurrents: false },
    { key: "em16", name: "Лаборатор ХС1",             hasCurrents: false },
    { key: "em17", name: "Лаборатор ХС2",             hasCurrents: false },
  ];

  // Нийт эрчим хүч card-д самбарын мөрүүдийг render хийнэ.
  const summaryList = document.getElementById("energySummaryList");
  const summaryRows = {}; // key → { led, name, stale, energy }
  if (summaryList) {
    METERS.forEach(m => {
      const row = document.createElement("div");
      // CSS-аар `display:flex` тавьдаг (input.css → #energySummaryList > *).
      // Эндээс align/justify/gap/font зэргээ Tailwind-аар хийнэ.
      row.className = "items-center justify-between gap-2 text-[13px] py-0.5";
      // Inline width/height/style ашиглав — Tailwind-ийн arbitrary class JS-аар
      // inject хийсэн HTML дотор compile хийгдэхгүй тул SVG-ийг шууд size-лнэ.
      row.innerHTML =
        '<div class="flex items-center gap-2 text-text-soft min-w-0">' +
          '<div style="width:7px;height:7px" class="rounded-full bg-text-faint transition-[background,box-shadow] duration-300 shrink-0" data-led></div>' +
          '<span class="truncate">' + m.name + '</span>' +
          '<svg data-stale width="14" height="14" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true" ' +
               'style="display:none;flex-shrink:0;color:var(--color-warning,#f59e0b)">' +
            '<path d="M12 2L1 21h22L12 2zm0 4.5L19.5 19h-15L12 6.5z"/>' +
            '<rect x="11" y="10" width="2" height="5" rx="1" fill="currentColor"/>' +
            '<circle cx="12" cy="17" r="1" fill="currentColor"/>' +
          '</svg>' +
        '</div>' +
        '<span class="font-semibold text-text-soft tabular-nums shrink-0">' +
          '<span data-energy>0.000</span> kWh' +
        '</span>';
      summaryList.appendChild(row);
      summaryRows[m.key] = {
        led:    row.querySelector("[data-led]"),
        stale:  row.querySelector("[data-stale]"),
        energy: row.querySelector("[data-energy]"),
      };
    });
  }

  // Сүүлд утга ирсэн агшин ба одоо урсаж буй чадал — нийлбэр + stale-д хэрэглэнэ.
  const STALE_MS = 10 * 1000;
  const lastUpdateAt = {};
  const currentPower = {};
  const summaryPowerEl = document.getElementById("energySummaryPower");

  function _bumpMeter(key) {
    lastUpdateAt[key] = Date.now();
    const row = summaryRows[key];
    if (row && row.stale) row.stale.style.display = "none";
    if (row && row.led) _blinkLed(row.led);
  }

  function _recomputeTotalPower() {
    if (!summaryPowerEl) return;
    let sum = 0;
    for (const k in currentPower) sum += currentPower[k];
    summaryPowerEl.textContent = sum.toFixed(2);
  }

  function _checkStale() {
    const now = Date.now();
    METERS.forEach(m => {
      const ts = lastUpdateAt[m.key];
      const row = summaryRows[m.key];
      if (!row || !row.stale) return;
      const isStale = ts == null || (now - ts) > STALE_MS;
      row.stale.style.display = isStale ? "inline-block" : "none";
    });
  }
  setInterval(_checkStale, 1000);

  // Сенсоруудтай холбоотой card-ийн хувийн span-уудыг шинэчилнэ. Хураангуй
  // мөрийг (energy + led + stale + sum) бас энд хариуцна.
  function bindMeter(meta) {
    const key = meta.key;
    const power  = _el[key + "Power"];
    const energy = _el[key + "Energy"];
    const led    = _el[key + "Led"];
    const curA   = _el[key + "CurA"];
    const curB   = _el[key + "CurB"];
    const curC   = _el[key + "CurC"];
    const sumRow = summaryRows[key];

    db.ref("/energy_meters/" + key + "/power_kw").on("value", s => {
      if (s.val() === null) return;
      const v = parseFloat(s.val());
      if (power) power.textContent = v.toFixed(2);
      _blinkLed(led);
      currentPower[key] = v;
      _recomputeTotalPower();
      _bumpMeter(key);
    });
    db.ref("/energy_meters/" + key + "/total_energy_kwh").on("value", s => {
      if (s.val() === null) return;
      const v = parseFloat(s.val()).toFixed(3);
      if (energy) energy.textContent = v;
      if (sumRow && sumRow.energy) sumRow.energy.textContent = v;
      _bumpMeter(key);
    });
    if (meta.hasCurrents) {
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

  METERS.forEach(bindMeter);
}
