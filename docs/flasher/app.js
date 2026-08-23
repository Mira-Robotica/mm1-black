/**
 * MM1-BLACK — installer (Wi-Fi /update or USB UART + esptool-js). MIRA.
 * Release metadata from GitHub API; .bin served from ./bins/ (same origin).
 */

const REPO = "verlab/mm1-black";
const DEFAULT_FLASH_BAUD = 115200;
const CONNECT_TIMEOUT_MS = 22000;

const BOARDS = {
  denky32: {
    id: "denky32",
    prefix: "MM1-BLACK-denky32-",
    flashAddr: 0x10000,
    flashSize: "4MB",
    flashFreq: "40m",
    versionBaud: 9600,
    filters: [{ usbVendorId: 0x1a86 }],
  },
  mm1_p4: {
    id: "mm1_p4",
    prefix: "MM1-BLACK-mm1_p4-",
    flashAddr: 0x10000,
    flashSize: "32MB",
    flashFreq: "80m",
    versionBaud: 115200,
    filters: [
      { usbVendorId: 0x1a86, usbProductId: 0x55d3 },
      { usbVendorId: 0x1a86 },
    ],
  },
};

const AP_HOST = "192.168.4.1";

let selectedBoard = "denky32";
let installMode = "wifi";
let selectedPort = null;
let releases = [];
let deviceVersion = null;
let esptoolModule = null;
let foundDevices = [];

const $ = (id) => document.getElementById(id);
const logEl = $("log");
const progressWrap = $("progressWrap");
const progressBar = $("progressBar");

function board() {
  return BOARDS[selectedBoard] || BOARDS.denky32;
}

function log(msg) {
  const t = new Date().toLocaleTimeString();
  logEl.textContent += `[${t}] ${msg}\n`;
  logEl.scrollTop = logEl.scrollHeight;
}

function setStatus(msg, kind = "") {
  const el = $("statusLine");
  if (!el) return;
  el.textContent = msg || "";
  el.className = kind ? `status-line ${kind}` : "status-line";
}

function setProgress(pct) {
  progressWrap.classList.add("visible");
  progressBar.style.width = `${Math.min(100, Math.max(0, pct))}%`;
}

function hideProgress() {
  progressWrap.classList.remove("visible");
  progressBar.style.width = "0%";
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

function flashBaud() {
  const sel = $("flashBaud");
  return sel ? parseInt(sel.value, 10) || DEFAULT_FLASH_BAUD : DEFAULT_FLASH_BAUD;
}

function usbAdapterName(_port) {
  return "USB UART";
}

function otaHost() {
  const el = $("otaHost");
  return (el && el.value ? el.value : AP_HOST).trim().replace(/^https?:\/\//, "").replace(/\/.*$/, "");
}

function validHost(host) {
  if (!host) return false;
  if (/^(\d{1,3}\.){3}\d{1,3}$/.test(host)) {
    return host.split(".").every((n) => +n >= 0 && +n <= 255);
  }
  return /^[a-zA-Z0-9.-]+$/.test(host) && host.length < 80;
}

function configureLoaderBaud(loader, baud) {
  loader.baudrate = baud;
  loader.romBaudrate = baud;
}

async function ensurePortClosed(port) {
  try {
    await port.close();
  } catch (_) {}
}

/** Set DTR/RTS one at a time — combined setSignals breaks on some Chromium builds */
async function setLineSignals(port, dtr, rts) {
  if (dtr !== undefined) {
    await port.setSignals({ dataTerminalReady: dtr });
  }
  if (rts !== undefined) {
    await port.setSignals({ requestToSend: rts });
  }
}

async function probeSerialControl(port) {
  await ensurePortClosed(port);
  try {
    await port.open({ baudRate: 115200, bufferSize: 8192 });
    await sleep(80);
    await setLineSignals(port, false, false);
    await sleep(40);
    await setLineSignals(port, false, true);
    await sleep(40);
    await setLineSignals(port, false, false);
    await ensurePortClosed(port);
    await sleep(150);
    return { ok: true };
  } catch (e) {
    await ensurePortClosed(port);
    const msg = e.message || String(e);
    return {
      ok: false,
      error:
        `USB control signals failed (${msg}). Enter download mode manually (BOOT+RST).`,
    };
  }
}

/** esptool.py classic reset — D0|R1|W100|D1|R0|W400|D0 */
async function classicBootloaderReset(port, baud = 115200) {
  await ensurePortClosed(port);
  await port.open({ baudRate: baud, bufferSize: 8192 });
  await sleep(80);
  await setLineSignals(port, false, true);
  await sleep(100);
  await setLineSignals(port, true, false);
  await sleep(400);
  await setLineSignals(port, false, undefined);
  await sleep(200);
  await ensurePortClosed(port);
  await sleep(250);
}

/** Pulse EN (RTS) so the ESP32 runs firmware — Web Serial often fails flashDeflFinish(reboot). */
async function hardResetEsp32(port, baud = 115200) {
  await ensurePortClosed(port);
  await port.open({ baudRate: baud, bufferSize: 8192 });
  await sleep(80);
  await setLineSignals(port, false, false);
  await sleep(100);
  await setLineSignals(port, undefined, true);
  await sleep(80);
  await setLineSignals(port, undefined, false);
  await sleep(300);
  await ensurePortClosed(port);
}

async function rebootAfterFlash(port, loader, transport, baud) {
  log("Resetting MCU…");
  try {
    if (loader.IS_STUB) await loader.flashDeflFinish(true);
  } catch (e) {
    log(`Stub reboot: ${e.message || e} — trying hardware reset.`);
  }
  await sleep(300);
  try {
    await loader.after("hard_reset");
  } catch (e) {
    log(`hard_reset: ${e.message || e}`);
  }
  try {
    await transport.disconnect();
  } catch (_) {}
  await ensurePortClosed(port);
  await sleep(200);
  try {
    await hardResetEsp32(port, baud);
    log("Hardware reset sent.");
  } catch (e) {
    log(`Hardware reset: ${e.message || e}`);
  }
}

function withTimeout(promise, ms, label) {
  return Promise.race([
    promise,
    sleep(ms).then(() => {
      throw new Error(`${label} (timeout ${ms / 1000}s)`);
    }),
  ]);
}

function parseTagVer(tag) {
  const m = /^v?(\d+)\.(\d+)\.(\d+)/i.exec(tag || "");
  if (!m) return null;
  return { major: +m[1], minor: +m[2], patch: +m[3], raw: tag };
}

function compareVer(a, b) {
  if (!a || !b) return 0;
  if (a.major !== b.major) return a.major - b.major;
  if (a.minor !== b.minor) return a.minor - b.minor;
  return a.patch - b.patch;
}

function normalizeDeviceVer(s) {
  if (!s) return null;
  const m = /v?(\d+\.\d+\.\d+)/i.exec(s);
  return m ? parseTagVer(m[1]) : parseTagVer(s);
}

function updateUI() {
  const rel = releases.find((r) => r.tag === $("releaseSelect").value);
  const cmp = $("versionCompare");
  const dv = normalizeDeviceVer(deviceVersion);

  $("deviceVersion").textContent = deviceVersion || "—";

  if (rel && dv) {
    const rv = parseTagVer(rel.tag);
    if (rv) {
      const c = compareVer(rv, dv);
      if (c > 0)
        cmp.innerHTML =
          '<span class="compare-newer">Newer release available.</span>';
      else if (c < 0)
        cmp.innerHTML =
          '<span class="compare-older">Device is newer than selected build.</span>';
      else cmp.textContent = "Device matches selected release.";
      $("btnInstall").disabled = !canInstall();
      return;
    }
  }
  cmp.textContent = rel ? "" : "";
  $("btnInstall").disabled = !canInstall();
}

function canInstall() {
  const ready =
    releases.length > 0 && $("releaseSelect").value && $("ackFlash").checked;
  if (!ready) return false;
  if (installMode === "wifi") return validHost(otaHost());
  return "serial" in navigator;
}

function localBinUrl(fileName) {
  return new URL(`bins/${fileName}`, window.location.href).href;
}

function applyBoardChrome() {
  const p4 = selectedBoard === "mm1_p4";
  $("btnBoardCyd").classList.toggle("active", !p4);
  $("btnBoardP4").classList.toggle("active", p4);
  try {
    localStorage.setItem("mm1-board", selectedBoard);
  } catch (_) {}
}

function applyModeChrome() {
  const wifi = installMode === "wifi";
  $("btnModeWifi").classList.toggle("active", wifi);
  $("btnModeUsb").classList.toggle("active", !wifi);
  $("stepListWifi").classList.toggle("hidden", !wifi);
  $("stepListUsb").classList.toggle("hidden", wifi);
  $("wifiPanel").classList.toggle("hidden", !wifi);
  $("usbPanel").classList.toggle("hidden", wifi);
  $("ackLabel").textContent = wifi
    ? "Do not power off during flash."
    : "Do not unplug USB during flash.";
  try {
    localStorage.setItem("mm1-mode", installMode);
  } catch (_) {}
  updateUI();
}

function setMode(mode) {
  installMode = mode === "usb" ? "usb" : "wifi";
  applyModeChrome();
}

function setBoard(id, reload) {
  if (!BOARDS[id]) id = "denky32";
  selectedBoard = id;
  applyBoardChrome();
  if (reload) {
    deviceVersion = null;
    $("deviceVersion").textContent = "—";
    fetchReleases().catch((e) => {
      log(`Releases: ${e.message}`);
      setStatus(e.message, "err");
    });
  }
}

function pickAsset(rel, prefix) {
  return (rel.assets || []).find(
    (a) => a.name.startsWith(prefix) && a.name.endsWith(".bin") && !a.name.includes("sha256")
  );
}

async function fetchReleasesFromApi() {
  const prefix = board().prefix;
  const res = await fetch(`https://api.github.com/repos/${REPO}/releases`, {
    headers: { Accept: "application/vnd.github+json" },
  });
  if (res.status === 404) {
    throw new Error("Repository not accessible");
  }
  if (!res.ok) {
    throw new Error(`GitHub API HTTP ${res.status}`);
  }
  const data = await res.json();
  const out = [];
  for (const rel of data) {
    if (rel.draft || rel.prerelease) continue;
    const tag = rel.tag_name || rel.name;
    const asset = pickAsset(rel, prefix);
    if (!asset) continue;
    out.push({
      tag,
      name: rel.name || tag,
      fileName: asset.name,
      url: localBinUrl(asset.name),
      size: asset.size,
    });
  }
  return out;
}

async function fetchReleasesFromManifest() {
  const res = await fetch(new URL("latest.json", window.location.href).href, {
    cache: "no-store",
  });
  if (!res.ok) return [];
  const data = await res.json();
  const rec = data[selectedBoard];
  if (!rec || !rec.file) return [];
  const fileName = String(rec.file).split("/").pop();
  return [
    {
      tag: rec.tag,
      name: rec.tag,
      fileName,
      url: new URL(rec.file, window.location.href).href,
      size: rec.size || 0,
    },
  ];
}

async function fetchReleases() {
  const sel = $("releaseSelect");
  sel.innerHTML = '<option value="">Loading…</option>';
  sel.disabled = true;
  setStatus("Loading releases…");
  releases = [];

  try {
    releases = await fetchReleasesFromApi();
  } catch (e) {
    log(`GitHub API: ${e.message}`);
    releases = [];
  }

  if (!releases.length) {
    try {
      releases = await fetchReleasesFromManifest();
      if (releases.length) log("Using latest.json from this site.");
    } catch (e) {
      log(`latest.json: ${e.message}`);
    }
  }

  sel.innerHTML = "";
  if (!releases.length) {
    sel.innerHTML = `<option value="">No ${board().prefix}*.bin yet</option>`;
    setStatus(
      `No ${selectedBoard} firmware on Releases yet. Tag a v* build or use SETUP → WiFi → Join after the first flash.`,
      "err"
    );
    updateUI();
    return;
  }

  for (const r of releases) {
    const opt = document.createElement("option");
    opt.value = r.tag;
    const kb = r.size ? ` (${(r.size / 1024).toFixed(0)} KB)` : "";
    opt.textContent = `${r.tag}${kb}`;
    sel.appendChild(opt);
  }
  sel.disabled = false;
  setStatus(`${releases.length} release(s) for ${selectedBoard}.`, "ok");
  log(`Loaded ${releases.length} release(s) (${selectedBoard}).`);
  updateUI();
}

async function readVersionFromPort(port) {
  let reader;
  let writer;
  try {
    await port.open({ baudRate: board().versionBaud });
    await new Promise((r) => setTimeout(r, 300));
    writer = port.writable.getWriter();
    reader = port.readable.getReader();
    await writer.write(new TextEncoder().encode("VERSION\n"));
    await writer.releaseLock();
    writer = null;

    const dec = new TextDecoder();
    let buf = "";
    const deadline = Date.now() + 3000;
    while (Date.now() < deadline) {
      const { value, done } = await reader.read();
      if (done) break;
      buf += dec.decode(value, { stream: true });
      const m = /MM1_FW_VERSION=([^\r\n]+)/.exec(buf);
      if (m) return m[1].trim();
    }
    return null;
  } finally {
    try {
      if (writer) await writer.releaseLock();
    } catch (_) {}
    try {
      if (reader) await reader.releaseLock();
    } catch (_) {}
    try {
      await port.close();
    } catch (_) {}
  }
}

async function requestPort() {
  setStatus("Choose the USB serial port…");
  try {
    selectedPort = await navigator.serial.requestPort({
      filters: board().filters,
    });
  } catch (e) {
    if (e.name === "NotFoundError") throw e;
    selectedPort = await navigator.serial.requestPort();
  }
  return selectedPort;
}

async function readInstalledVersion() {
  if (!("serial" in navigator)) return;

  try {
    const port = await requestPort();
    setStatus("Reading installed version…");
    deviceVersion = await readVersionFromPort(port);
    selectedPort = null;
    if (deviceVersion) {
      log(`Installed: ${deviceVersion}`);
      setStatus(`Installed firmware: ${deviceVersion}`, "ok");
    } else {
      log("No VERSION response (wrong port, baud, or device busy).");
      setStatus("Version not read — you can still install.", "ok");
    }
    updateUI();
  } catch (e) {
    selectedPort = null;
    if (e.name !== "NotFoundError") {
      log(`Read version: ${e.message || e}`);
      setStatus(e.message || String(e), "err");
    }
    updateUI();
  }
}

async function loadEsptool() {
  if (!esptoolModule) {
    log("Loading esptool-js…");
    esptoolModule = await import(
      "https://cdn.jsdelivr.net/npm/esptool-js@0.6.1/+esm"
    );
  }
  return esptoolModule;
}

async function downloadFirmware(rel) {
  log(`Downloading ${rel.fileName}…`);
  let res = await fetch(rel.url);
  if (!res.ok) {
    throw new Error(
      `Firmware file not on this site (HTTP ${res.status}). ` +
        "Re-deploy Pages after a new Release, or wait a few minutes."
    );
  }
  return new Uint8Array(await res.arrayBuffer());
}

async function connectLoader(port, baud, terminal) {
  const { ESPLoader, Transport, ClassicReset } = await loadEsptool();

  const probe = await probeSerialControl(port);
  if (!probe.ok) {
    log(probe.error);
  } else {
    log("USB control signals OK.");
  }

  const attempts = [
    {
      label: "HW auto-reset",
      mode: "no_reset",
      async prep() {
        log("Toggle DTR/RTS for bootloader entry…");
        await classicBootloaderReset(port, baud);
      },
    },
    {
      label: "manual BOOT+RST",
      mode: "no_reset",
      async prep() {
        log("Hold BOOT → tap RST → release RST → release BOOT (4 s)…");
        setStatus("Download mode: BOOT + RST now…", "ok");
        await sleep(4000);
      },
    },
    {
      label: "esptool auto-reset",
      mode: "default_reset",
      async prep() {
        await sleep(300);
      },
    },
  ];

  let lastErr = null;
  for (let i = 0; i < attempts.length; i++) {
    const step = attempts[i];
    let transport;
    try {
      await step.prep();
      await ensurePortClosed(port);
      await sleep(200);

      transport = new Transport(port, false);
      const loader = new ESPLoader({
        transport,
        baudrate: baud,
        terminal,
        debugLogging: false,
        resetConstructors: {
          classicReset: (t, delay) => new ClassicReset(t, Math.max(delay, 400)),
        },
      });
      configureLoaderBaud(loader, baud);

      log(`Connecting ${i + 1}/${attempts.length}: ${step.label}…`);
      setStatus(`Connecting (${step.label})…`);
      const chip = await withTimeout(
        loader.main(step.mode),
        CONNECT_TIMEOUT_MS,
        `Connect timed out (${step.label})`
      );
      return { loader, transport, chip };
    } catch (e) {
      lastErr = e;
      log(`Connect failed (${step.label}): ${e.message || e}`);
      try {
        if (transport) await transport.disconnect();
      } catch (_) {}
      await ensurePortClosed(port);
      await sleep(500);
    }
  }

  throw new Error(
    (lastErr?.message || "Failed to connect with the device") +
      ". Close other serial tools, then try BOOT+RST."
  );
}

async function probeMm1(host, ms = 1800) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), ms);
  try {
    const res = await fetch(`http://${host}/api/status`, {
      signal: ctrl.signal,
      cache: "no-store",
    });
    if (!res.ok) return null;
    const st = await res.json();
    const mm1 =
      res.headers.get("X-MM1-BLACK") === "1" ||
      st.mm1 === true ||
      st.ssid === "MM1-MIRA" ||
      (typeof st.dev === "string" && st.dev.startsWith("SAP6"));
    if (!mm1) return null;
    return { ...st, _ip: host };
  } catch (_) {
    return null;
  } finally {
    clearTimeout(timer);
  }
}

function renderFound() {
  const box = $("foundDevices");
  if (!box) return;
  box.innerHTML = "";
  for (const st of foundDevices) {
    const btn = document.createElement("button");
    btn.type = "button";
    const board = st.board ? String(st.board).toUpperCase() : "MM1";
    const fw = st.fw || "—";
    btn.innerHTML = `${board} · ${fw}<small>${st._ip}</small>`;
    if (st._ip === otaHost()) btn.classList.add("active");
    btn.addEventListener("click", () => {
      $("otaHost").value = st._ip;
      try {
        localStorage.setItem("mm1-ota-host", st._ip);
      } catch (_) {}
      renderFound();
      updateUI();
    });
    box.appendChild(btn);
  }
}

async function findDevices() {
  const btn = $("btnFind");
  if (btn) btn.disabled = true;
  foundDevices = [];
  renderFound();
  setStatus("Looking for MM1-BLACK on the network…");

  const hosts = new Set([AP_HOST]);
  const typed = otaHost();
  if (validHost(typed)) hosts.add(typed);

  const results = [];
  for (const host of hosts) {
    const st = await probeMm1(host);
    if (st) results.push(st);
  }

  foundDevices = results;
  renderFound();

  if (results.length) {
    if (!validHost(typed) || typed === AP_HOST) {
      $("otaHost").value = results[0]._ip;
    }
    setStatus(`Found ${results.length} device(s).`, "ok");
    log(`Found: ${results.map((d) => `${d._ip} ${d.fw || ""}`).join(", ")}`);
  } else {
    setStatus(
      "No device answered. Enter the address from SETUP → WiFi, or connect this computer to the MM1 access point and use 192.168.4.1.",
      "err"
    );
    log("Find: no HTTP /api/status (HTTPS pages cannot scan the LAN).");
  }
  if (btn) btn.disabled = false;
  updateUI();
}

function postFirmwareForm(host, firmware, fileName) {
  const form = $("wifiOtaForm");
  form.innerHTML = "";
  form.action = `http://${host}/update`;
  const input = document.createElement("input");
  input.type = "file";
  input.name = "firmware";
  const dt = new DataTransfer();
  dt.items.add(
    new File([firmware], fileName, { type: "application/octet-stream" })
  );
  input.files = dt.files;
  form.appendChild(input);
  window.open("about:blank", "mm1ota");
  form.submit();
}

async function installFirmwareWifi() {
  const tag = $("releaseSelect").value;
  const rel = releases.find((r) => r.tag === tag);
  const host = otaHost();
  if (!rel) {
    setStatus("Select a firmware release.", "err");
    return;
  }
  if (!$("ackFlash").checked) {
    setStatus("Confirm the checkbox first.", "err");
    return;
  }
  if (!validHost(host)) {
    setStatus("Enter a device address.", "err");
    return;
  }

  $("btnInstall").disabled = true;
  $("btnFind").disabled = true;
  setProgress(0);
  setStatus("Downloading firmware…");

  try {
    const firmware = await downloadFirmware(rel);
    log(`Downloaded ${(firmware.byteLength / 1024).toFixed(0)} KB.`);
    setProgress(30);

    setStatus(`Sending to ${host}…`);
    let posted = false;
    try {
      const fd = new FormData();
      fd.append(
        "firmware",
        new Blob([firmware], { type: "application/octet-stream" }),
        rel.fileName
      );
      const res = await fetch(`http://${host}/update`, {
        method: "POST",
        body: fd,
      });
      posted = true;
      if (!res.ok) {
        const txt = await res.text().catch(() => "");
        throw new Error(txt || `HTTP ${res.status}`);
      }
      log(`Wi-Fi update: ${await res.text().catch(() => "ok")}`);
    } catch (e) {
      if (posted) throw e;
      log(`Direct send blocked (${e.message || e}). Using the update page…`);
      postFirmwareForm(host, firmware, rel.fileName);
    }

    setProgress(100);
    setStatus(
      posted
        ? `Installed ${rel.tag}. The tape should reboot.`
        : `Sending ${rel.tag} to http://${host}/update. Keep that tab open until it reboots.`,
      "ok"
    );
    log(`Wi-Fi install started → ${host}`);
    try {
      localStorage.setItem("mm1-ota-host", host);
    } catch (_) {}
  } catch (e) {
    log(`Wi-Fi install failed: ${e.message || e}`);
    setStatus(`Install failed: ${e.message || e}`, "err");
  } finally {
    hideProgress();
    $("btnFind").disabled = false;
    updateUI();
  }
}

async function installFirmware() {
  if (installMode === "wifi") {
    await installFirmwareWifi();
    return;
  }

  const tag = $("releaseSelect").value;
  const rel = releases.find((r) => r.tag === tag);
  if (!rel) {
    setStatus("Select a firmware release.", "err");
    return;
  }
  if (!$("ackFlash").checked) {
    setStatus("Confirm the checkbox first.", "err");
    return;
  }

  const baud = flashBaud();
  const cfg = board();
  $("btnInstall").disabled = true;
  $("btnReadVersion").disabled = true;
  setProgress(0);
  setStatus("Downloading firmware…");

  const terminal = {
    clean: () => {},
    writeLine: (d) => log(d),
    write: (d) => log(d),
  };

  try {
    const firmware = await downloadFirmware(rel);
    log(`Downloaded ${(firmware.byteLength / 1024).toFixed(0)} KB.`);

    setStatus("Select USB port and flash…");
    selectedPort = null;
    const port = await requestPort();
    log(`Port: ${usbAdapterName(port)}`);
    log(`Flashing ${rel.tag} @ ${baud} baud (${cfg.flashSize})…`);

    setStatus("Connecting to bootloader…");
    const { loader, transport, chip } = await connectLoader(
      port,
      baud,
      terminal
    );
    log(`Chip: ${chip}`);

    setStatus("Writing flash… do not unplug USB.");
    await loader.writeFlash({
      fileArray: [{ data: firmware, address: cfg.flashAddr }],
      flashMode: "dio",
      flashFreq: cfg.flashFreq,
      flashSize: cfg.flashSize,
      eraseAll: false,
      compress: true,
      reportProgress: (_idx, written, total) => {
        setProgress((written / total) * 100);
      },
    });
    log("Flash written.");

    await rebootAfterFlash(port, loader, transport, baud);
    selectedPort = null;

    deviceVersion = rel.tag.replace(/^v/, "");
    log("Install complete. Press RST if the screen stays blank.");
    setStatus(`Installed ${rel.tag} successfully.`, "ok");
    updateUI();
  } catch (e) {
    selectedPort = null;
    if (e.name === "NotFoundError") setStatus("No port selected.", "err");
    else {
      log(`Install failed: ${e.message || e}`);
      setStatus(`Install failed: ${e.message || e}`, "err");
    }
  } finally {
    hideProgress();
    $("btnReadVersion").disabled = false;
    updateUI();
  }
}

function initBoardFromUrl() {
  const q = new URLSearchParams(window.location.search).get("board");
  if (q === "p4" || q === "mm1_p4") return "mm1_p4";
  if (q === "cyd" || q === "denky32") return "denky32";
  try {
    const saved = localStorage.getItem("mm1-board");
    if (saved && BOARDS[saved]) return saved;
  } catch (_) {}
  return "denky32";
}

function initModeFromUrl() {
  const q = new URLSearchParams(window.location.search).get("mode");
  if (q === "usb") return "usb";
  if (q === "wifi") return "wifi";
  try {
    const saved = localStorage.getItem("mm1-mode");
    if (saved === "usb" || saved === "wifi") return saved;
  } catch (_) {}
  return "wifi";
}

function init() {
  selectedBoard = initBoardFromUrl();
  installMode = initModeFromUrl();
  applyBoardChrome();
  applyModeChrome();

  try {
    const savedHost = localStorage.getItem("mm1-ota-host");
    if (savedHost && validHost(savedHost)) $("otaHost").value = savedHost;
  } catch (_) {}

  $("btnBoardCyd").addEventListener("click", () => setBoard("denky32", true));
  $("btnBoardP4").addEventListener("click", () => setBoard("mm1_p4", true));
  $("btnModeWifi").addEventListener("click", () => setMode("wifi"));
  $("btnModeUsb").addEventListener("click", () => setMode("usb"));
  $("btnFind").addEventListener("click", () => findDevices().catch((e) => {
    log(`Find: ${e.message}`);
    setStatus(e.message, "err");
  }));
  $("otaHost").addEventListener("input", () => {
    try {
      localStorage.setItem("mm1-ota-host", otaHost());
    } catch (_) {}
    updateUI();
  });

  if (!("serial" in navigator)) {
    $("noSerial").classList.remove("hidden");
    $("btnReadVersion").disabled = true;
  } else {
    $("btnReadVersion").addEventListener("click", readInstalledVersion);
  }

  $("btnInstall").addEventListener("click", installFirmware);
  $("releaseSelect").addEventListener("change", updateUI);
  $("ackFlash").addEventListener("change", updateUI);
  $("flashBaud").addEventListener("change", updateUI);
  fetchReleases().catch((e) => {
    log(`Releases: ${e.message}`);
    setStatus(e.message, "err");
  });
}

init();
