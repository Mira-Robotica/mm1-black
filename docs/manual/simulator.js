/* MM1-BLACK UI simulator — faithful to firmware screens (main.cpp) */
(() => {
  const TABS = ["POINTS", "VIEW", "SENSOR", "FILE", "SETUP"];
  const SETUP_SUB = ["Bright", "Cal", "BT", "WiFi", "About"];
  const CAL_PAGES = ["menu", "IMU", "Laser", "Trim", "Measure"];

  const state = {
    tab: "POINTS",
    setupSub: "Bright",
    calPage: "menu",
    theme: "light",
    measureMode: "2-tap",
    holdNav: true,
    aimArmed: false,
    capturing: false,
    status: "Ready",
    hdrBlink: null,
    selectedRow: 1,
    page: 0,
    pts: [
      { id: 12, d: 4.812, e: false, az: 212.4, inc: -3.1, nav: false },
      { id: 11, d: 3.105, e: false, az: 210.8, inc: -2.8, nav: true },
      { id: 10, d: 3.105, e: false, az: 210.8, inc: -2.8, nav: true },
      { id: 9,  d: 3.105, e: false, az: 210.8, inc: -2.8, nav: true },
      { id: 8,  d: 2.447, e: false, az: 98.2,  inc: 1.4,  nav: false },
      { id: 7,  d: 5.920, e: false, az: 45.0,  inc: -12.6, nav: false },
      { id: 6,  d: 1.338, e: true,  az: 0.0,   inc: 0.0,   nav: false },
      { id: 5,  d: 6.771, e: false, az: 301.5, inc: 4.2,  nav: false },
      { id: 4,  d: 0.854, e: false, az: 178.9, inc: -0.5, nav: false },
      { id: 3,  d: 4.210, e: false, az: 15.3,  inc: 8.8,  nav: false },
      { id: 2,  d: 2.001, e: false, az: 90.0,  inc: 0.2,  nav: false },
      { id: 1,  d: 3.560, e: false, az: 270.1, inc: -6.4, nav: false },
    ],
    nextId: 13,
    laser: 3.245,
    bat: 78,
    clock: "14:32",
    bright: 80,
    volume: 40,
    btLink: true,
    sdOk: true,
    wifi: false,
  };

  const $ = (sel, root = document) => root.querySelector(sel);
  const screen = () => $("#mm1-screen");

  function fmt3(n) { return n.toFixed(3); }
  function fmt1(n) { return n.toFixed(1); }

  function setStatus(msg) {
    state.status = msg;
    const el = $("#sim-status");
    if (el) el.textContent = msg;
  }

  function renderHeader() {
    const batCol = state.bat <= 15 ? "#C62828" : state.bat <= 30 ? "#F9A825" : "#2E7D32";
    return `
      <div class="dev-header">
        <span class="dev-title">MM1-BLACK</span>
        <div class="dev-icons">
          <span class="ico bat" style="color:${batCol}" title="Battery">${state.bat}%</span>
          <span class="ico sd ${state.sdOk ? "on" : "off"}">SD</span>
          <span class="ico wifi ${state.wifi ? "on" : ""}">WiFi</span>
          <span class="ico bt ${state.btLink ? "link" : ""}">${state.btLink ? "LINK" : "BT"}</span>
          <span class="ico clock">${state.clock}</span>
        </div>
      </div>`;
  }

  function renderTabs() {
    return `<div class="dev-tabs">${TABS.map(t =>
      `<button type="button" class="dev-tab${state.tab === t ? " active" : ""}" data-tab="${t}">${t}</button>`
    ).join("")}</div>`;
  }

  function renderPoints() {
    const pageSize = 8;
    const start = state.page * pageSize;
    const slice = state.pts.slice(start, start + pageSize);
    const pages = Math.max(1, Math.ceil(state.pts.length / pageSize));
    const hdrClass = state.hdrBlink || "";

    const rows = slice.map((p, i) => {
      const abs = start + i;
      const sel = abs === state.selectedRow ? " sel" : "";
      const nav = p.nav ? " nav" : "";
      return `<tr class="pt-row${sel}${nav}" data-idx="${abs}">
        <td class="c-ref">${p.id}</td>
        <td class="c-d">${p.e ? "—" : fmt3(p.d)}</td>
        <td class="c-e">${p.e ? "E" : ""}</td>
        <td>${p.e ? "-" : fmt1(p.az)}</td>
        <td>${p.e ? "-" : fmt1(p.inc)}</td>
      </tr>`;
    }).join("");

    return `
      <div class="pane points">
        <table class="pts-table">
          <thead class="${hdrClass}"><tr>
            <th>Ref#</th><th>D(m)</th><th>E</th><th>Azm</th><th>Inc</th>
          </tr></thead>
          <tbody>${rows}</tbody>
        </table>
        <div class="pager">
          <button type="button" data-act="prev">‹</button>
          <span>${state.pts.length} pts  ${state.page + 1}/${pages}</span>
          <button type="button" data-act="next">›</button>
        </div>
        <div class="action-bar">
          <button type="button" class="ab del" data-act="del">DEL</button>
          <button type="button" class="ab clr" data-act="clr">CLR</button>
          <button type="button" class="ab save" data-act="save">SAVE</button>
          <button type="button" class="ab tx" data-act="tx">TX</button>
        </div>
        <div class="status-line" id="sim-status">${state.status}</div>
      </div>`;
  }

  function renderView() {
    return `
      <div class="pane view">
        <div class="plot-card">
          <div class="plot-title">PLAN  azi   2 leg</div>
          <svg viewBox="0 0 220 120" class="plot">
            <line x1="18" y1="100" x2="205" y2="100" stroke="currentColor" stroke-width=".6" opacity=".35"/>
            <line x1="18" y1="100" x2="18" y2="12" stroke="currentColor" stroke-width=".6" opacity=".35"/>
            <circle cx="40" cy="85" r="4" fill="#2E7D32"/>
            <text x="46" y="82" font-size="7" fill="currentColor">A</text>
            <line x1="40" y1="85" x2="70" y2="55" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="40" y1="85" x2="28" y2="50" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="40" y1="85" x2="85" y2="90" stroke="#90A4AE" stroke-width="1.2"/>
            <circle cx="70" cy="55" r="2" fill="#90A4AE"/>
            <circle cx="28" cy="50" r="2" fill="#90A4AE"/>
            <circle cx="85" cy="90" r="2" fill="#90A4AE"/>
            <line x1="40" y1="85" x2="130" y2="48" stroke="#0B5CAB" stroke-width="2.4"/>
            <circle cx="130" cy="48" r="4" fill="#2E7D32"/>
            <text x="136" y="45" font-size="7" fill="currentColor">B</text>
            <line x1="130" y1="48" x2="165" y2="30" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="130" y1="48" x2="175" y2="60" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="130" y1="48" x2="155" y2="75" stroke="#90A4AE" stroke-width="1.2"/>
            <circle cx="165" cy="30" r="2" fill="#90A4AE"/>
            <circle cx="175" cy="60" r="2" fill="#90A4AE"/>
            <circle cx="155" cy="75" r="2" fill="#90A4AE"/>
            <line x1="130" y1="48" x2="110" y2="25" stroke="#C62828" stroke-width="1.2" stroke-dasharray="3 2"/>
            <circle cx="110" cy="25" r="2" fill="#C62828"/>
            <text x="160" y="112" font-size="8" fill="currentColor">5 m</text>
            <text x="20" y="14" font-size="7" fill="#4A6270">N ↑</text>
          </svg>
        </div>
        <div class="plot-card">
          <div class="plot-title">PROFILE  inc   6 splay</div>
          <svg viewBox="0 0 220 120" class="plot">
            <line x1="18" y1="60" x2="205" y2="60" stroke="currentColor" stroke-width=".6" opacity=".35"/>
            <line x1="18" y1="105" x2="18" y2="15" stroke="currentColor" stroke-width=".6" opacity=".35"/>
            <circle cx="35" cy="62" r="4" fill="#2E7D32"/>
            <text x="30" y="78" font-size="7" fill="currentColor">A</text>
            <line x1="35" y1="62" x2="55" y2="48" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="35" y1="62" x2="60" y2="78" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="35" y1="62" x2="70" y2="58" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="35" y1="62" x2="120" y2="42" stroke="#00796B" stroke-width="2.4"/>
            <circle cx="120" cy="42" r="4" fill="#2E7D32"/>
            <text x="115" y="58" font-size="7" fill="currentColor">B</text>
            <line x1="120" y1="42" x2="150" y2="28" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="120" y1="42" x2="165" y2="50" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="120" y1="42" x2="155" y2="65" stroke="#90A4AE" stroke-width="1.2"/>
            <circle cx="55" cy="48" r="2" fill="#90A4AE"/>
            <circle cx="60" cy="78" r="2" fill="#90A4AE"/>
            <circle cx="150" cy="28" r="2" fill="#90A4AE"/>
            <text x="160" y="112" font-size="8" fill="currentColor">5 m</text>
            <text x="20" y="20" font-size="7" fill="#4A6270">Up</text>
          </svg>
        </div>
      </div>`;
  }

  function renderSensor() {
    const btnLabel = state.aimArmed ? "AIM — tap again"
      : state.measureMode === "Cont" ? "cont idle"
      : state.measureMode === "1-tap" ? "1-tap ready" : "2-tap ready";
    return `
      <div class="pane sensor">
        <div class="scard c-lzr"><div class="sc-h">LASER</div><div class="sc-v">${fmt3(state.laser)} m</div></div>
        <div class="scard c-imu"><div class="sc-h">IMU</div><div class="sc-v">Az 212.4°<br>Inc -3.1°<br>Roll 1.2°</div></div>
        <div class="scard c-lnk"><div class="sc-h">LINK</div><div class="sc-v">laser OK<br>imu OK<br>rx 1842</div></div>
        <div class="scard c-btn"><div class="sc-h">BUTTON</div><div class="sc-v">${btnLabel}</div></div>
        <div class="scard c-tmp"><div class="sc-h">TEMP / BAT</div><div class="sc-v">38.2 °C<br>3.91 V · ${state.bat}%</div></div>
      </div>`;
  }

  function renderFile() {
    return `
      <div class="pane file">
        <div class="file-active">Active: mm1_black_003.csv  (${state.pts.length} pts)</div>
        <table class="file-table">
          <thead><tr><th>File</th><th>Size</th></tr></thead>
          <tbody>
            <tr class="act"><td>mm1_black_003.csv</td><td>4.2 KB</td></tr>
            <tr><td>mm1_black_002.csv</td><td>18.7 KB</td></tr>
            <tr><td>mm1_black_001.csv</td><td>9.1 KB</td></tr>
            <tr><td>mm1_black_000.csv</td><td>2.4 KB</td></tr>
          </tbody>
        </table>
        <div class="action-bar">
          <button type="button" class="ab new">NEW</button>
          <button type="button" class="ab use">USE</button>
          <button type="button" class="ab del">DEL</button>
        </div>
        <div class="status-line">Ready</div>
      </div>`;
  }

  function renderSetupBright() {
    return `
      <div class="setup-body">
        <label class="slider-lab">Display brightness</label>
        <input type="range" min="10" max="100" value="${state.bright}" data-bright />
        <div class="slider-val">${state.bright}%</div>
        <label class="slider-lab">Speaker volume</label>
        <input type="range" min="0" max="100" value="${state.volume}" data-vol />
        <div class="slider-val">${state.volume === 0 ? "0 = mute" : state.volume + "%"}</div>
        <label class="slider-lab">Theme</label>
        <div class="seg">
          <button type="button" class="seg-btn${state.theme === "light" ? " on" : ""}" data-theme="light">Light</button>
          <button type="button" class="seg-btn${state.theme === "dark" ? " on" : ""}" data-theme="dark">Dark</button>
        </div>
      </div>`;
  }

  function renderCal() {
    if (state.calPage === "menu") {
      return `
        <div class="cal-menu">
          <button type="button" class="cal-tile" data-cal="IMU"><strong>IMU</strong><span>Health, quality, heading trim</span></button>
          <button type="button" class="cal-tile" data-cal="Laser"><strong>Laser</strong><span>Distance, test, health</span></button>
          <button type="button" class="cal-tile" data-cal="Trim"><strong>Trim</strong><span>Laser offset (mm)</span></button>
          <button type="button" class="cal-tile" data-cal="Measure"><strong>Measure</strong><span>Capture button mode</span></button>
        </div>`;
    }
    if (state.calPage === "Measure") {
      return `
        <div class="setup-body">
          <button type="button" class="back-cal" data-cal="menu">‹ Cal</button>
          <div class="hint">2-tap: aim then capture. 1-tap: one press. Cont: tap start/stop. Hold 5s (1/2-tap) writes nav x3.</div>
          <div class="seg triple">
            ${["2-tap","1-tap","Cont"].map(m =>
              `<button type="button" class="seg-btn${state.measureMode===m?" on":""}" data-mode="${m}">${m}</button>`
            ).join("")}
          </div>
          <button type="button" class="hold-btn${state.holdNav?" on":""}" data-hold>
            Hold 5s nav ${state.holdNav ? "ON" : "OFF"}
          </button>
        </div>`;
    }
    if (state.calPage === "Laser") {
      return `
        <div class="setup-body">
          <button type="button" class="back-cal" data-cal="menu">‹ Cal</button>
          <div class="big-metric">${fmt3(state.laser)} m</div>
          <div class="hint">Health OK · Aim at a bright target (&gt;10 cm). Use Test to check distance.</div>
          <button type="button" class="ab save" style="width:100%;margin-top:8px">Test</button>
        </div>`;
    }
    if (state.calPage === "Trim") {
      return `
        <div class="setup-body">
          <button type="button" class="back-cal" data-cal="menu">‹ Cal</button>
          <div class="hint">Offset (mm), then OK. Applies to all shots. D = D_laser + trim</div>
          <div class="big-metric">+0 mm</div>
          <div class="numpad-fake">7 8 9<br>4 5 6<br>1 2 3<br>Rst 0 OK</div>
        </div>`;
    }
    return `
      <div class="setup-body">
        <button type="button" class="back-cal" data-cal="menu">‹ Cal</button>
        <div class="hint">Health OK · Quality good · If heading drifts, figure-8 for ~30 s away from metal.</div>
        <div class="row-kv"><span>Heading</span><span>212.4°</span></div>
        <div class="row-kv"><span>Az offset</span><span>0.0°</span></div>
        <div class="seg" style="margin-top:10px">
          <button type="button" class="seg-btn">Head=0</button>
          <button type="button" class="seg-btn">Default</button>
        </div>
      </div>`;
  }

  function renderSetupBT() {
    return `
      <div class="setup-body">
        <div class="bt-status">${state.btLink ? "Bluetooth: SAP6 connected" : "Bluetooth: advertising SAP6_0001 (BLE)"}</div>
        <div class="row-kv"><span>Name</span><span>SAP6_0001</span></div>
        <div class="row-kv"><span>Bond</span><span>saved</span></div>
        <div class="row-kv"><span>Peer</span><span>${state.btLink ? "TopoDroid" : "-"}</span></div>
        <div class="action-bar wrap">
          <button type="button" class="ab tx">Measure</button>
          <button type="button" class="ab save">TX</button>
          <button type="button" class="ab use">Restart BLE</button>
          <button type="button" class="ab del">Unpair</button>
        </div>
      </div>`;
  }

  function renderSetupWiFi() {
    return `
      <div class="setup-body">
        <div class="row-kv"><span>Network</span><span>Off</span></div>
        <input class="field" placeholder="SSID" readonly value=""/>
        <input class="field" placeholder="password" readonly value=""/>
        <div class="action-bar wrap">
          <button type="button" class="ab del">Off</button>
          <button type="button" class="ab use">Join</button>
          <button type="button" class="ab tx">Scan</button>
        </div>
        <div class="action-bar wrap">
          <button type="button" class="ab save">Check</button>
          <button type="button" class="ab new">Install</button>
          <button type="button" class="ab use">AP</button>
        </div>
        <div class="hint">SoftAP: MM1-MIRA / mira-mm1 — portal for file export</div>
      </div>`;
  }

  function renderSetupAbout() {
    return `
      <div class="setup-body about">
        <div class="about-brand">MIRA Robotica</div>
        <div class="about-url">https://www.mirarobotica.com/</div>
        <div class="about-prod">MM1-BLACK</div>
        <div class="about-ver">v1.0.7</div>
        <div class="hint">Check for updates in SETUP → WiFi.</div>
        <div class="qr-fake" title="Installer QR">QR</div>
      </div>`;
  }

  function renderSetup() {
    let body = "";
    switch (state.setupSub) {
      case "Bright": body = renderSetupBright(); break;
      case "Cal": body = renderCal(); break;
      case "BT": body = renderSetupBT(); break;
      case "WiFi": body = renderSetupWiFi(); break;
      default: body = renderSetupAbout();
    }
    return `
      <div class="pane setup">
        <div class="setup-subs">${SETUP_SUB.map(s =>
          `<button type="button" class="sub${state.setupSub===s?" on":""}" data-sub="${s}">${s}</button>`
        ).join("")}</div>
        ${body}
      </div>`;
  }

  function renderContent() {
    switch (state.tab) {
      case "VIEW": return renderView();
      case "SENSOR": return renderSensor();
      case "FILE": return renderFile();
      case "SETUP": return renderSetup();
      default: return renderPoints();
    }
  }

  function render() {
    const root = screen();
    if (!root) return;
    root.dataset.theme = state.theme;
    root.innerHTML = renderHeader() + renderTabs() + renderContent();
    bind();
  }

  function blinkHeader(kind, ms = 550) {
    state.hdrBlink = kind;
    render();
    clearTimeout(blinkHeader._t);
    blinkHeader._t = setTimeout(() => {
      state.hdrBlink = null;
      render();
    }, ms);
  }

  function doCapture() {
    const d = +(state.laser + (Math.random() * 0.4 - 0.1)).toFixed(3);
    state.laser = d;
    const az = +(180 + Math.random() * 40).toFixed(1);
    const inc = +((Math.random() * 10) - 5).toFixed(1);
    state.pts.unshift({ id: state.nextId++, d, e: false, az, inc, nav: false });
    state.selectedRow = 0;
    state.page = 0;
    blinkHeader("ok");
    setStatus(`Shot #${state.nextId - 1}  ${fmt3(d)} m`);
    render();
  }

  function doNav() {
    const d = state.laser;
    const az = 210.8, inc = -2.8;
    for (let i = 0; i < 3; i++) {
      state.pts.unshift({ id: state.nextId++, d, e: false, az, inc, nav: true });
    }
    state.selectedRow = 0;
    blinkHeader("ok");
    setStatus("Nav x3 (same shot)");
    render();
  }

  function onCapturePress() {
    if (state.tab !== "POINTS" && state.tab !== "SENSOR" && state.tab !== "SETUP") {
      state.tab = "POINTS";
    }
    if (state.measureMode === "2-tap") {
      if (!state.aimArmed) {
        state.aimArmed = true;
        blinkHeader("aim", 90000);
        setStatus("AIM - press again to CAPTURE");
        render();
        return;
      }
      state.aimArmed = false;
      blinkHeader("meas", 200);
      setTimeout(doCapture, 180);
      return;
    }
    if (state.measureMode === "1-tap") {
      blinkHeader("meas", 200);
      setTimeout(doCapture, 180);
      return;
    }
    // Cont
    setStatus("Continuous — tap to stop");
    doCapture();
  }

  function bind() {
    const root = screen();
    root.querySelectorAll("[data-tab]").forEach(btn => {
      btn.addEventListener("click", () => {
        state.tab = btn.dataset.tab;
        state.aimArmed = false;
        state.hdrBlink = null;
        setStatus("Ready");
        render();
      });
    });
    root.querySelectorAll("[data-sub]").forEach(btn => {
      btn.addEventListener("click", () => {
        state.setupSub = btn.dataset.sub;
        state.calPage = "menu";
        render();
      });
    });
    root.querySelectorAll("[data-cal]").forEach(btn => {
      btn.addEventListener("click", () => {
        state.calPage = btn.dataset.cal;
        render();
      });
    });
    root.querySelectorAll("[data-theme]").forEach(btn => {
      btn.addEventListener("click", () => {
        state.theme = btn.dataset.theme;
        render();
      });
    });
    root.querySelectorAll("[data-mode]").forEach(btn => {
      btn.addEventListener("click", () => {
        state.measureMode = btn.dataset.mode;
        render();
      });
    });
    const hold = root.querySelector("[data-hold]");
    if (hold) hold.addEventListener("click", () => {
      state.holdNav = !state.holdNav;
      render();
    });
    const bright = root.querySelector("[data-bright]");
    if (bright) bright.addEventListener("input", () => {
      state.bright = +bright.value;
      render();
    });
    const vol = root.querySelector("[data-vol]");
    if (vol) vol.addEventListener("input", () => {
      state.volume = +vol.value;
      render();
    });
    root.querySelectorAll(".pt-row").forEach(row => {
      row.addEventListener("click", () => {
        state.selectedRow = +row.dataset.idx;
        render();
      });
    });
    root.querySelectorAll("[data-act]").forEach(btn => {
      btn.addEventListener("click", () => {
        const a = btn.dataset.act;
        if (a === "prev") { state.page = Math.max(0, state.page - 1); render(); }
        if (a === "next") {
          const pages = Math.max(1, Math.ceil(state.pts.length / 8));
          state.page = Math.min(pages - 1, state.page + 1);
          render();
        }
        if (a === "del" && state.selectedRow != null) {
          state.pts.splice(state.selectedRow, 1);
          state.selectedRow = Math.min(state.selectedRow, state.pts.length - 1);
          setStatus("Deleted");
          render();
        }
        if (a === "clr") {
          state.pts = [];
          setStatus("RAM cleared (SD file kept)");
          render();
        }
        if (a === "save") { setStatus("Saved mm1_black_003.csv"); render(); }
        if (a === "tx") { setStatus("STREAM BLE — Preparing..."); render(); }
      });
    });
  }

  // Physical button + long-press for nav
  let pressTimer = null;
  function wireCaptureButton() {
    const btn = $("#capture-btn");
    if (!btn) return;
    const down = () => {
      btn.classList.add("pressed");
      if (state.holdNav && (state.measureMode === "2-tap" || state.measureMode === "1-tap") && !state.aimArmed) {
        pressTimer = setTimeout(() => {
          pressTimer = "fired";
          doNav();
        }, 5000);
      }
    };
    const up = () => {
      btn.classList.remove("pressed");
      if (pressTimer === "fired") { pressTimer = null; return; }
      if (pressTimer) { clearTimeout(pressTimer); pressTimer = null; }
      onCapturePress();
    };
    btn.addEventListener("pointerdown", down);
    btn.addEventListener("pointerup", up);
    btn.addEventListener("pointerleave", () => {
      btn.classList.remove("pressed");
      if (pressTimer && pressTimer !== "fired") { clearTimeout(pressTimer); pressTimer = null; }
    });
  }

  document.addEventListener("DOMContentLoaded", () => {
    render();
    wireCaptureButton();
    // demo clock tick
    setInterval(() => {
      const d = new Date();
      state.clock = `${String(d.getHours()).padStart(2,"0")}:${String(d.getMinutes()).padStart(2,"0")}`;
      const clock = screen()?.querySelector(".clock");
      if (clock) clock.textContent = state.clock;
    }, 30000);
  });
})();
