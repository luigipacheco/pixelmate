// PixelMate web installer. Flow after CrossPoint Reader's installer: choose the board, choose
// the firmware, then connect and install with a checklist. Flashing is esptool-js over Web Serial.
// Board data (offsets, save area, tips) comes from boards.json; staged firmware from
// firmware/<board>/firmware.json (see tools/stage_installer_firmware.py).
import { ESPLoader, Transport } from "./vendor/esptool-js-0.7.1/bundle.js";

const $ = (id) => document.getElementById(id);
const hex = (n) => "0x" + n.toString(16);
const num = (s) => parseInt(s, 16);

const state = { boards: [], board: null, firmware: {}, custom: null, busy: false };

// Line drawings of each device with Bitsy's eyes on its screen.
const EYES = (cx, cy, s) => `
  <rect x="${cx - 13 * s}" y="${cy - 8 * s}" width="${10 * s}" height="${16 * s}" fill="#dcff28"/>
  <rect x="${cx + 3 * s}" y="${cy - 8 * s}" width="${10 * s}" height="${16 * s}" fill="#dcff28"/>`;
const ART = {
  square: `<svg viewBox="0 0 120 120" aria-hidden="true">
    <rect x="10" y="10" width="100" height="100" rx="6" fill="#23271d" stroke="#eef0e6" stroke-width="4"/>
    <rect x="20" y="20" width="80" height="80" fill="#0a0c08"/>${EYES(60, 60, 1.6)}</svg>`,
  stackchan: `<svg viewBox="0 0 120 130" aria-hidden="true">
    <rect x="14" y="8" width="92" height="76" rx="6" fill="#23271d" stroke="#eef0e6" stroke-width="4"/>
    <rect x="24" y="18" width="72" height="56" fill="#0a0c08"/>${EYES(60, 46, 1.3)}
    <rect x="22" y="88" width="76" height="34" rx="4" fill="#ff5a1e" stroke="#eef0e6" stroke-width="4"/></svg>`,
  round: `<svg viewBox="0 0 120 130" aria-hidden="true">
    <rect x="38" y="0" width="44" height="130" fill="#23271d" stroke="#eef0e6" stroke-width="4"/>
    <circle cx="60" cy="65" r="48" fill="#23271d" stroke="#eef0e6" stroke-width="4"/>
    <circle cx="60" cy="65" r="38" fill="#0a0c08"/>${EYES(60, 65, 1.3)}
    <rect x="108" y="56" width="8" height="18" fill="#dcff28" stroke="#eef0e6" stroke-width="3"/></svg>`,
};

// ---------- Step 1: board ----------

async function init() {
  if (!("serial" in navigator)) $("unsupported").hidden = false;
  state.boards = (await (await fetch("boards.json")).json()).boards;
  await Promise.all(state.boards.map(async (b) => {
    try {
      const r = await fetch(`firmware/${b.id}/firmware.json`, { cache: "no-cache" });
      if (r.ok) state.firmware[b.id] = await r.json();
    } catch { /* not staged */ }
  }));
  renderBoards();
  const fromHash = location.hash.slice(1);
  if (state.boards.some((b) => b.id === fromHash)) selectBoard(fromHash);

  $("fresh").addEventListener("change", updateButtons);
  $("custom").addEventListener("change", (e) => {
    const f = e.target.files[0];
    state.custom = f || null;
    $("custom-name").textContent = f ? `${f.name} (${f.size.toLocaleString()} bytes)` : "No file chosen";
    renderFirmware();
  });
  $("install").addEventListener("click", install);
  $("backup").addEventListener("click", backup);
}

function renderBoards() {
  const grid = $("boards");
  grid.innerHTML = "";
  for (const b of state.boards) {
    const fw = state.firmware[b.id];
    const el = document.createElement("button");
    el.type = "button";
    el.className = "board";
    el.setAttribute("role", "radio");
    el.setAttribute("aria-checked", "false");
    el.dataset.id = b.id;
    el.innerHTML = `
      <span class="board-art">${ART[b.screen] || ART.square}</span>
      <span class="board-body">
        <span class="board-name">${b.name}</span>
        <span class="chip">${b.chip}</span>
        <span class="board-blurb">${b.blurb}</span>
        <span class="board-status">${fw ? `● Firmware ${fw.version}` : "○ No firmware staged"}</span>
      </span>`;
    el.addEventListener("click", () => selectBoard(b.id));
    el.addEventListener("keydown", (e) => {
      const i = state.boards.indexOf(b);
      const step = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 }[e.key];
      if (!step) return;
      e.preventDefault();
      const next = state.boards[(i + step + state.boards.length) % state.boards.length];
      selectBoard(next.id);
      grid.querySelector(`[data-id="${next.id}"]`).focus();
    });
    grid.append(el);
  }
}

function selectBoard(id) {
  if (state.busy) return;
  state.board = state.boards.find((b) => b.id === id);
  history.replaceState(null, "", `#${id}`);
  for (const el of $("boards").children) {
    const on = el.dataset.id === id;
    el.setAttribute("aria-checked", String(on));
    el.tabIndex = on ? 0 : -1;
  }
  $("step-firmware").removeAttribute("aria-disabled");
  $("step-flash").removeAttribute("aria-disabled");
  $("connect-tips").innerHTML = state.board.connect.map((t) => `<li>${t}</li>`).join("");
  $("board-note").textContent = state.board.note;
  $("progress").hidden = true;
  renderFirmware();
}

// ---------- Step 2: firmware ----------

function renderFirmware() {
  const b = state.board;
  if (!b) return;
  const fw = state.firmware[b.id];
  const box = $("firmware");
  if (state.custom) {
    const app = b.files.find((f) => f.role === "app");
    box.innerHTML = `<dl class="fw">
      <dd class="fw-version">Your build</dd>
      <dt>File</dt><dd>${escapeHtml(state.custom.name)}</dd>
      <dt>Size</dt><dd>${state.custom.size.toLocaleString()} bytes</dd>
      <dt>Writes</dt><dd>app slot at ${app.offset} only</dd></dl>`;
  } else if (fw) {
    const total = fw.files.reduce((n, f) => n + f.size, 0);
    box.innerHTML = `<dl class="fw">
      <dd class="fw-version">PixelMate ${escapeHtml(fw.version)}</dd>
      <dt>Board</dt><dd>${b.name} (${b.chip})</dd>
      <dt>Built</dt><dd>${new Date(fw.built).toLocaleString()}${fw.commit ? ` · commit ${escapeHtml(fw.commit)}` : ""}</dd>
      <dt>Writes</dt><dd>${fw.files.map((f) => `${f.role} @ ${f.offset}`).join(", ")}</dd>
      <dt>Size</dt><dd>${(total / 1048576).toFixed(2)} MB</dd></dl>`;
  } else {
    box.innerHTML = `<div class="fw-missing">
      <h3>No firmware staged for the ${b.name} yet.</h3>
      <p class="muted">Build it with ESP-IDF, then copy it into this site from the repo root:</p>
      <pre>python tools/stage_installer_firmware.py ${b.id} ${b.build === "repo root" ? "build" : b.build + "/build"}</pre>
      <p class="muted">Or install a build of your own under Advanced.</p></div>`;
  }
  updateButtons();
}

function updateButtons() {
  const ok = "serial" in navigator && state.board && !state.busy;
  $("install").disabled = !(ok && (state.custom || state.firmware[state.board.id]));
  $("backup").disabled = !ok;
}

async function sha256(data) {
  const d = await crypto.subtle.digest("SHA-256", data);
  return [...new Uint8Array(d)].map((x) => x.toString(16).padStart(2, "0")).join("");
}

// The images to write, checked against firmware.json before anything touches the device.
async function loadImages() {
  const b = state.board;
  if (state.custom) {
    const data = new Uint8Array(await state.custom.arrayBuffer());
    if (data[0] !== 0xe9) throw new Error("That file is not an ESP32 app image (it should start with byte 0xE9).");
    return [{ role: "app", address: num(b.files.find((f) => f.role === "app").offset), data }];
  }
  const fw = state.firmware[b.id];
  const images = [];
  for (const f of fw.files) {
    const r = await fetch(`firmware/${b.id}/${f.file}`, { cache: "no-cache" });
    if (!r.ok) throw new Error(`Could not download ${f.file} (${r.status}).`);
    const data = new Uint8Array(await r.arrayBuffer());
    if (data.length !== f.size || (await sha256(data)) !== f.sha256)
      throw new Error(`${f.file} does not match firmware.json; it may be damaged. Reload and try again.`);
    images.push({ role: f.role, address: num(f.offset), data });
  }
  return images;
}

// ---------- Step 3: connect, install, back up ----------

function startProgress(title, steps) {
  $("progress").hidden = false;
  $("progress-title").textContent = title;
  $("checklist").innerHTML = steps.map((s) => `<li data-state="pending">${s}</li>`).join("");
  $("log").textContent = "";
  setBar(0);
  setStatus("", "");
  $("progress").scrollIntoView({ behavior: "smooth", block: "nearest" });
  let i = -1;
  return {
    next() {
      const items = $("checklist").children;
      if (i >= 0) items[i].dataset.state = "done";
      i += 1;
      if (i < items.length) items[i].dataset.state = "running";
    },
    fail() { if (i >= 0 && i < $("checklist").children.length) $("checklist").children[i].dataset.state = "error"; },
  };
}
function setBar(fraction) {
  const pct = Math.round(fraction * 100);
  $("bar").querySelector("span").style.width = `${pct}%`;
  $("bar").setAttribute("aria-valuenow", String(pct));
  $("bar").toggleAttribute("data-done", pct >= 100);
}
function setStatus(text, kind) {
  $("status").textContent = text;
  $("status").dataset.kind = kind;
}
const terminal = {
  clean() {},
  writeLine(s) { log(s + "\n"); },
  write(s) { log(s); },
};
function log(s) {
  const el = $("log");
  el.textContent += s;
  el.scrollTop = el.scrollHeight;
}

// Ask for the port, start the ROM loader and make sure the chip is the board's chip.
async function connect() {
  const port = await navigator.serial.requestPort();
  const transport = new Transport(port, false);
  const loader = new ESPLoader({ transport, baudrate: 921600, romBaudrate: 115200, terminal });
  try {
    const description = await loader.main();
    log(`Connected: ${description}\n`);
    const chip = loader.chip.CHIP_NAME;
    if (chip !== state.board.chip) {
      const other = state.boards.filter((b) => b.chip === chip).map((b) => b.name);
      throw new Error(`This device is an ${chip}, but the ${state.board.name} is an ${state.board.chip}.`
        + (other.length ? ` Did you mean the ${other.join(" or ")}?` : ""));
    }
  } catch (e) {
    await transport.disconnect().catch(() => {});
    // An Espressif port that is not the chip's own USB-Serial-JTAG (PID 0x1001) belongs
    // to the running firmware (e.g. the Mosaico's "Iris" ports, PID 0x4002), which
    // cannot reset the chip into the ROM loader. Say how to get there instead.
    const { usbVendorId, usbProductId } = port.getInfo();
    if (usbVendorId === 0x303a && usbProductId !== 0x1001 && !/is an /.test(e?.message || "")) {
      throw new Error("This port belongs to the firmware that is running, not the chip's download mode. "
        + (state.board.download || "Put the board in download mode and pick the new port."));
    }
    throw e;
  }
  return { loader, transport };
}

async function run(title, steps, work) {
  state.busy = true;
  updateButtons();
  const p = startProgress(title, steps);
  let session = null;
  try {
    await work(p, async () => (session = await connect()));
  } catch (e) {
    p.fail();
    const msg = e?.name === "NotFoundError" ? "No port was chosen." : (e?.message || String(e));
    setStatus(msg, "error");
    log(`\nError: ${msg}\n`);
  } finally {
    if (session) await session.transport.disconnect().catch(() => {});
    state.busy = false;
    updateButtons();
  }
}

async function install() {
  const b = state.board;
  const fresh = $("fresh").checked;
  const steps = ["Check the firmware", "Connect to the device", fresh ? "Write PixelMate and clear the save" : "Write PixelMate", "Restart"];
  await run(`Installing PixelMate on the ${b.name}`, steps, async (p, open) => {
    p.next();
    const images = await loadImages();
    if (fresh) images.push({ role: "save", address: num(b.save.offset), data: new Uint8Array(num(b.save.size)).fill(0xff) });
    images.sort((x, y) => x.address - y.address);
    images.forEach((im) => log(`${im.role.padEnd(16)} ${hex(im.address).padStart(9)}  ${im.data.length} bytes\n`));

    p.next();
    const { loader } = await open();

    p.next();
    const sizes = images.map((im) => im.data.length);
    const total = sizes.reduce((a, n) => a + n, 0);
    await loader.writeFlash({
      fileArray: images.map((im) => ({ data: im.data, address: im.address })),
      flashMode: "keep", flashFreq: "keep", flashSize: "keep",
      eraseAll: false, compress: true,
      reportProgress: (i, written, size) => {
        const before = sizes.slice(0, i).reduce((a, n) => a + n, 0);
        setBar((before + (size ? (written / size) * sizes[i] : 0)) / total);
      },
    });
    setBar(1);

    p.next();
    await loader.after("hard_reset");
    p.next();
    setStatus(`Done. ${b.after}`, "ok");
  });
}

async function backup() {
  const b = state.board;
  await run(`Backing up the ${b.name}`, ["Connect to the device", "Read the whole flash (takes a few minutes)", "Save the file"], async (p, open) => {
    p.next();
    const { loader } = await open();
    p.next();
    const sizeName = (await loader.detectFlashSize()) || "16MB";
    const size = parseInt(sizeName, 10) * (sizeName.includes("KB") ? 1024 : 1048576);
    log(`Flash size: ${sizeName}\n`);
    const data = await loader.readFlash(0, size, (_packet, done, of) => setBar(done / of));
    setBar(1);
    p.next();
    const name = `${b.id}-backup-${new Date().toISOString().slice(0, 10)}.bin`;
    const a = Object.assign(document.createElement("a"), { href: URL.createObjectURL(new Blob([data])), download: name });
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
    await loader.after("hard_reset");
    p.next();
    setStatus(`Saved ${name}. To restore it later: esptool --port <port> write-flash 0 ${name}`, "ok");
  });
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

init().catch((e) => {
  $("boards").innerHTML = `<p class="banner banner-warn">Could not load the board list: ${escapeHtml(e.message)}</p>`;
});
