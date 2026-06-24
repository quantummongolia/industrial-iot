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

// Нэг логик утга нэгээс олон таб дээр давхар card-аар харагдаж болно (ж:
// Activity ба Үйлдвэр таб). Тийм элементүүдийг массиваар хадгалж, доорх туслах
// функцууд нэг ба массив хоёуланд ажиллана.
function _asArr(x) {
  return x == null ? [] : (Array.isArray(x) ? x : [x]);
}

// Үндсэн id + "Uv" (Үйлдвэр таб) хуулбарыг цуглуулна. Байгаа элементүүдийг л
// буцаана (хоосон бол []).
function _pickAll(base) {
  return ["", "Uv"].map(s => document.getElementById(base + s)).filter(Boolean);
}

function _setText(els, v) {
  _asArr(els).forEach(el => { el.textContent = v; });
}

function _blinkLed(led) {
  _asArr(led).forEach(el => {
    el.classList.add("data-led-active");
    setTimeout(function () { el.classList.remove("data-led-active"); }, 400);
  });
}

// Усны савны card — түвшинг (метр) .wl-card[data-max]-тай харьцуулж усыг дүүргэнэ.
// Бодит савны өндрийг мэдэхгүй бол data-max default нь 4 м.
function _setTankLevel(waterEls, v) {
  _asArr(waterEls).forEach(waterEl => {
    const card = waterEl.closest(".wl-card");
    const max = (card && parseFloat(card.dataset.max)) || 4;
    const pct = Math.max(0, Math.min(100, (v / max) * 100));
    waterEl.style.setProperty("--level", pct.toFixed(2) + "%");
  });
}

// Савны өндрийг (data-max) зөвхөн index.html дээр ГАРААР тохируулна — ESP-ийн
// mount_height-аар автоматаар дарж бичих логикийг (2026-06-17) хассан.

function _onFlowRate(key, val) {
  _readingCount++;
  const flow = parseFloat(val).toFixed(2);
  if (key === "fm1") {
    _setText(_el.fm1Flow, flow);
    _blinkLed(_el.dataLed1);
  } else if (key === "fm2") {
    _setText(_el.fm2Flow, flow);
    _blinkLed(_el.dataLed2);
  } else if (key === "fm3") {
    _setText(_el.fm3Flow, flow);
    _blinkLed(_el.dataLed3);
  }
  if (_el.readingCount) _el.readingCount.textContent = _readingCount;
  if (_el.lastUpdate)   _el.lastUpdate.textContent   = new Date().toLocaleTimeString();
}

function _onTotalizer(key, val) {
  const total = parseFloat(val).toFixed(2);
  if (key === "fm1") {
    _setText(_el.fm1Total, total);
  } else if (key === "fm2") {
    _setText(_el.fm2Total, total);
  } else if (key === "fm3") {
    _setText(_el.fm3Total, total);
  }
}

// ── Компрессор (ushg Slave 4, Atlas Copco Q1CMCSTD) ──────────────────────
// Карт нь error / температур / даралт / нийт цаг гэсэн 4 нүднээс бүрдэнэ.
// (Status-ийн хөдөлгөөнт дүрс/текстийг хассан — зөвхөн тоон утга + дата LED.)

// CODED-ERROR хүснэгт (Modbus баримтаас) — тоон код → дэлгэцийн код + тайлбар.
const _CP_ERRORS = {
  0: 'Алдаагүй',
  81: '(E0010) Emergency stop',
  241: '(E0030) Door Open', 242: '(A0030) Door Open', 250: '(A0031) CAB filter DP',
  321: '(E0040) Oil LVL IMM stop', 322: '(A0040) Oil level alarm',
  401: '(E0050) RD alarm', 402: '(A0050) RD alarm',
  481: '(E0060) Belt drive SERV',
  561: '(E0070) Fan MTR IMM stop', 562: '(A0070) Fan motor alarm', 569: '(E0071) Fan MTR IMM stop',
  641: '(E0080) Main motor short', 649: '(E0081) Main motor lock', 657: '(E0082) Main MTR overLD',
  665: '(E0083) Motor phase IMB', 666: '(A0083) Motor phase IMB',
  673: '(E0084) Main MTR CT SENS', 681: '(E0085) Fan MTR CT SENS', 682: '(A0085) MTR STR HR INH',
  689: '(E0086) Fan MTR overload',
  721: '(E0090) Phase sequence', 729: '(E0091) Phase L1 fault', 737: '(E0092) Phase L2 fault', 745: '(E0093) Phase L3 fault',
  921: '(E0115) PD PRESS high', 953: '(E0119) PD PRESS high', 954: '(A0119) Del press high',
  1001: '(E0125) PD TEMP SENS', 1033: '(E0129) PD TEMP high', 1034: '(A0129) Del temp high',
  1049: '(E0131) INT PRESS low', 1081: '(E0135) INT PRESS sensor', 1113: '(E0139) INT PRESS high', 1114: '(A0139) INT PRESS high',
  1601: '(E0200) COOL WTR IMMstop', 1602: '(A0200) COOL water alarm', 1610: '(A0201) CNDS drain alarm',
  1833: '(E0229) PD TEMP high',
  6473: '(E0809) DIFF PRESS high', 6474: '(A0809) Diff press high', 6513: '(E0814) Venting error', 6569: '(E0821) Short circuit',
  6769: '(E0846) DEL PRESS range', 6849: '(E0856) INT PRESS range',
  7209: '(E0901) User trip 1', 7210: '(A0901) CONF alarm 1', 7217: '(E0902) User trip 2', 7218: '(A0902) CONF alarm 2',
  7225: '(E0903) User trip 3', 7226: '(A0903) CONF alarm 3',
  16242: '(A2030) Air filter DP', 16257: '(E2032) line FTR DP stop', 16258: '(A2032) Line FTR DP ALM', 16282: '(A2035) SEP filter DP',
  16322: '(A2040) Oil filter DP', 17610: '(A2201) FTR drain ALM', 17922: '(A2240) Oil/WTR SEP ALM',
  22530: '(A2816) Power failure', 22690: '(A2836) RTC error',
  23321: '(E2915) ISC PRESS SENS', 23601: '(E2950) ISC sensor range', 23681: '(E2960) ISC XPM COMMS',
  23762: '(A2970) ISC XPM Di alarm', 23841: '(E2980) ISC XPM DI',
  24990: '(R3123) PD TEMP low', 25102: '(R3137) INT PRESS high', 25844: '(E3230) Door Open', 28003: '(S3500) Start inhibit',
  38437: '(A4804) Service hours', 38445: '(A4805) Cabinet filters', 38453: '(A4806) Air filter SERV', 38461: '(A4807) Oilfilter SERV',
  38466: '(E4808) OilAir SEP DP HI', 38469: '(A4808) Separator SERV', 38477: '(A4809) Grease service', 38485: '(A4810) Valves service',
  38493: '(A4811) Belt drive SERV', 38501: '(A4812) ELEC SYS SERV', 38509: '(A4813) MTR bearing SERV', 38517: '(A4814) COMP BRG SERV',
  38525: '(A4815) Weekly service', 38533: '(A4816) Annual service', 38541: '(A4817) Bi-annual SERV',
  40001: '(E5000) System error', 40002: '(A5000) System error', 40017: '(E5002) System error',
};

function _cpIcons() { if (window.lucide) lucide.createIcons(); }

function _setCompressorError(code) {
  const val = document.getElementById('cpErrVal');
  if (!val) return;
  const cell = document.getElementById('cpErrCell');
  const ico = document.getElementById('cpErrIco');
  const label = document.getElementById('cpErrLabel');
  const fault = code !== 0;
  // Тоо гардаг хэсэгт → кодын дугаар. "Алдаа" бичигтэй хэсэгт → тайлбар текст.
  val.textContent = String(code);
  if (label) label.textContent = _CP_ERRORS[code] || (fault ? ('Тодорхойгүй код ' + code) : 'Алдаагүй');
  cell.classList.toggle('fault', fault);
  ico.innerHTML = '<i data-lucide="' + (fault ? 'shield-alert' : 'shield-check') + '"></i>';
  _cpIcons();
}

// /ushg/compressor → карт. error / температур / даралт / нийт цаг (тоон утга).
// Дата ирэх бүрд LED анивчина; 10 сек дата ирэхгүй бол анхааруулах гурвалжин гарна.
function _setupCompressor(db) {
  const led  = document.getElementById('cpLed');
  const warn = document.getElementById('cpWarn');
  let lastData = 0;

  // Дата ирэх бүрд: LED анивчуулж, анхааруулгыг арилгаж, цагийг шинэчилнэ.
  function _ping() {
    lastData = Date.now();
    if (led) _blinkLed(led);
    if (warn) warn.classList.remove('show');
  }

  db.ref('/ushg/compressor/error_code').on('value', s => {
    if (s.val() === null) return;
    _setCompressorError(parseInt(s.val(), 10) || 0);
    _ping();
  });
  db.ref('/ushg/compressor/temp_c').on('value', s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val());
    const el = document.getElementById('cpTempVal');
    if (el) el.textContent = Math.round(v);
    const cell = document.getElementById('cpTempCell');
    if (cell) cell.classList.toggle('warn', v > 88); // халуун бол улбар шар
    _ping();
  });
  db.ref('/ushg/compressor/pressure_bar').on('value', s => {
    if (s.val() === null) return;
    const el = document.getElementById('cpPressVal');
    if (el) el.textContent = parseFloat(s.val()).toFixed(1);
    _ping();
  });
  db.ref('/ushg/compressor/total_hours').on('value', s => {
    if (s.val() === null) return;
    const el = document.getElementById('cpHrsVal');
    if (el) el.textContent = parseInt(s.val(), 10).toLocaleString('en-US');
    _ping();
  });

  // 10 секунд ямар ч дата ирэхгүй бол анхааруулах гурвалжныг гаргана.
  setInterval(function () {
    if (warn && lastData && Date.now() - lastData > 10000) warn.classList.add('show');
  }, 1000);
}

function initRealtime() {
  // Доорх олонлог элементүүд Activity ба Үйлдвэр таб дээр давхар card-аар
  // харагдана. _pickAll нь үндсэн id + "Uv" (Үйлдвэр) хуулбар хоёуланг цуглуулж,
  // _setText / _blinkLed / _setTankLevel массиваар зэрэг шинэчилнэ.
  _el = {
    fm1Flow:      _pickAll("fm1Flow"),
    fm1Total:     _pickAll("fm1Total"),
    fm2Flow:      _pickAll("fm2Flow"),
    fm2Total:     _pickAll("fm2Total"),
    fm3Flow:      _pickAll("fm3Flow"),
    fm3Total:     _pickAll("fm3Total"),
    dataLed1:     _pickAll("dataLed1"),
    dataLed2:     _pickAll("dataLed2"),
    dataLed3:     _pickAll("dataLed3"),
    // Ultrasonic level transmitter (Slave 1) — Суларсан уусмал савны түвшин
    ulsLevel:     _pickAll("ulsLevel"),
    ulsLevelBar:  _pickAll("ulsLevelBar"),
    ulsLevelLed:  _pickAll("ulsLevelLed"),
    // Ultrasonic level (Teerem Slave 7) — Эргэлтийн усан сан
    waterTankLevel:    _pickAll("waterTankLevel"),
    waterTankLevelBar: _pickAll("waterTankLevelBar"),
    waterTankLevelLed: _pickAll("waterTankLevelLed"),
    statusLed:    document.getElementById("statusLed"),
    statusText:   document.getElementById("statusText"),
    readingCount: document.getElementById("readingCount"),
    lastUpdate:   document.getElementById("lastUpdate"),
    // Тээрмийн жин — Activity + Үйлдвэр таб дублицат
    teeremWeightAct:     _pickAll("teeremWeightAct"),
    teeremWeightTonsAct: _pickAll("teeremWeightTonsAct"),
    teeremWeightLedAct:  _pickAll("teeremWeightLedAct"),
    // Тээрмийн тэжээлийн ус 1 (Slave 8) — Activity + Үйлдвэр таб дублицат
    feedWater1FlowAct:   _pickAll("feedWater1FlowAct"),
    feedWater1TotalAct:  _pickAll("feedWater1TotalAct"),
    feedWater1LedAct:    _pickAll("feedWater1LedAct"),
    // Тээрмийн тэжээлийн ус 2 (Slave 6) — Activity + Үйлдвэр таб дублицат
    feedWater2FlowAct:   _pickAll("feedWater2FlowAct"),
    feedWater2TotalAct:  _pickAll("feedWater2TotalAct"),
    feedWater2LedAct:    _pickAll("feedWater2LedAct"),
    // 01-WT-01 Бутлуурын жин — Activity + Үйлдвэр таб дублицат
    wt01Weight:     _pickAll("wt01Weight"),
    wt01WeightTons: _pickAll("wt01WeightTons"),
    wt01Led:        _pickAll("wt01Led"),
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
    bayanLevel:    _pickAll("bayanLevel"),
    bayanLevelBar: _pickAll("bayanLevelBar"),
    bayanLevelLed: _pickAll("bayanLevelLed"),
    // Цэвэр усан сан (Supmea ultrasonic — pressfilter Bus C Slave 6)
    cleanWaterLevel:    _pickAll("cleanWaterLevel"),
    cleanWaterLevelBar: _pickAll("cleanWaterLevelBar"),
    cleanWaterLevelLed: _pickAll("cleanWaterLevelLed"),
  };

  // Усны савны card бүрийн харагдах дээд хэмжээг (метр) data-max-аас тааруулна —
  // ингэснээр зөвхөн index.html дээр data-max-г өөрчлөхөд хангалттай.
  document.querySelectorAll(".wl-card").forEach(card => {
    const max = parseFloat(card.dataset.max);
    const maxEl = card.querySelector(".wl-max");
    if (maxEl && !isNaN(max)) maxEl.textContent = max.toFixed(2);
  });

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
  // Усны дүүргэлтийн дээд хязгаарыг index.html дахь .wl-card[data-max]-аас уншина.
  db.ref("/flow_system/level_sensor/level").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val());
    _setText(_el.ulsLevel, v.toFixed(2));
    _setTankLevel(_el.ulsLevelBar, v);
    _blinkLed(_el.ulsLevelLed);
  });

  // Баян уусмалын сан (Supmea ultrasonic — pressfilter Bus C Slave 5)
  // Суларсан уусмалын сантай яг адил төхөөрөмж, ижил bar хязгаар (0..2.20m).
  db.ref("/pressfilter/bayan_tank/level").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val());
    _setText(_el.bayanLevel, v.toFixed(2));
    _setTankLevel(_el.bayanLevelBar, v);
    _blinkLed(_el.bayanLevelLed);
  });

  // Цэвэр усан сан (Supmea ultrasonic — pressfilter Bus C Slave 6)
  // Bar-ийн дээд хязгаарыг index.html дахь #cleanWaterLevelBar data-max-аас уншина.
  db.ref("/pressfilter/clean_water_tank/level").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val());
    _setText(_el.cleanWaterLevel, v.toFixed(2));
    _setTankLevel(_el.cleanWaterLevelBar, v);
    _blinkLed(_el.cleanWaterLevelLed);
  });

  // ── Teerem ────────────────────────────────────────────
  db.ref("/teerem/weight_rate").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val()).toFixed(2);
    _setText(_el.teeremWeightAct, v);
    _blinkLed(_el.teeremWeightLedAct);
  });
  db.ref("/teerem/cumulative_kg").on("value", s => {
    if (s.val() === null) return;
    const t = (parseInt(s.val(), 10) / 1000).toFixed(3);
    _setText(_el.teeremWeightTonsAct, t);
  });

  // ── Тээрмийн тэжээлийн ус 1 (Slave 8 magnetic flowmeter) ────────
  db.ref("/teerem/feed_water1/flow_rate").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val()).toFixed(2);
    _setText(_el.feedWater1FlowAct, v);
    _blinkLed(_el.feedWater1LedAct);
  });
  db.ref("/teerem/feed_water1/totalizer").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val()).toFixed(2);
    _setText(_el.feedWater1TotalAct, v);
  });

  // ── Тээрмийн тэжээлийн ус 2 (Slave 6 flowmeter) ────────
  db.ref("/teerem/feed_water2/flow_rate").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val()).toFixed(2);
    _setText(_el.feedWater2FlowAct, v);
    _blinkLed(_el.feedWater2LedAct);
  });
  db.ref("/teerem/feed_water2/totalizer").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val()).toFixed(2);
    _setText(_el.feedWater2TotalAct, v);
  });

  // ── Эргэлтийн усан сан — Ultrasonic Level (Teerem Slave 7) ─────────
  db.ref("/teerem/water_tank/level").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val());
    _setText(_el.waterTankLevel, v.toFixed(2));
    _setTankLevel(_el.waterTankLevelBar, v);
    _blinkLed(_el.waterTankLevelLed);
  });

  // ── Компрессор (ushg Slave 4) ─────────────────────────────────────
  _setupCompressor(db);

  // ── Butluur (01-WT-01 Бутлуурын жин) ───────────────────
  // weight_rate = t/h, cumulative_t = нийт тонн (t).
  db.ref("/butluur/weight_rate").on("value", s => {
    if (s.val() === null) return;
    const v = parseFloat(s.val()).toFixed(2);
    _setText(_el.wt01Weight, v);
    _blinkLed(_el.wt01Led);
  });
  db.ref("/butluur/cumulative_t").on("value", s => {
    if (s.val() === null) return;
    const t = parseFloat(s.val()).toFixed(3);
    _setText(_el.wt01WeightTons, t);
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
    const n = key.slice(2); // "04"
    // Анхдагч + "...T" дагавартай толин тусгал (өөр таб дээрх ижил card).
    // Нэг тоолуурыг хэд хэдэн таб дээр харуулахын тулд хуулбарын id-д "T"
    // дагавар өг (ж: emPower04T) — энд автоматаар хамт шинэчлэгдэнэ.
    const pick = (base) =>
      ["", "T"].map(sfx => document.getElementById(base + n + sfx)).filter(Boolean);
    const powerEls  = pick("emPower");
    const energyEls = pick("emEnergy");
    const ledEls    = pick("emLed");
    const curAEls   = pick("emCurrentA");
    const curBEls   = pick("emCurrentB");
    const curCEls   = pick("emCurrentC");
    const sumRow = summaryRows[key];

    db.ref("/energy_meters/" + key + "/power_kw").on("value", s => {
      if (s.val() === null) return;
      const v = parseFloat(s.val());
      powerEls.forEach(el => el.textContent = v.toFixed(2));
      ledEls.forEach(_blinkLed);
      currentPower[key] = v;
      _recomputeTotalPower();
      _bumpMeter(key);
    });
    db.ref("/energy_meters/" + key + "/total_energy_kwh").on("value", s => {
      if (s.val() === null) return;
      const v = parseFloat(s.val()).toFixed(3);
      energyEls.forEach(el => el.textContent = v);
      if (sumRow && sumRow.energy) sumRow.energy.textContent = v;
      _bumpMeter(key);
    });
    if (meta.hasCurrents) {
      db.ref("/energy_meters/" + key + "/current_a").on("value", s => {
        if (s.val() === null) return;
        const v = parseFloat(s.val()).toFixed(2);
        curAEls.forEach(el => el.textContent = v);
      });
      db.ref("/energy_meters/" + key + "/current_b").on("value", s => {
        if (s.val() === null) return;
        const v = parseFloat(s.val()).toFixed(2);
        curBEls.forEach(el => el.textContent = v);
      });
      db.ref("/energy_meters/" + key + "/current_c").on("value", s => {
        if (s.val() === null) return;
        const v = parseFloat(s.val()).toFixed(2);
        curCEls.forEach(el => el.textContent = v);
      });
    }
  }

  METERS.forEach(bindMeter);
}
