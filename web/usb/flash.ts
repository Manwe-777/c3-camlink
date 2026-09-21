/* The flash panel: esptool-js driven from the page, for putting firmware on a
   board over the same cable the settings travel on.

   This one IS an ES module -- it is inlined into <script type="module">,
   because esptool-js is loaded by URL from a CDN. See types/esptool.d.ts. */
import { ESPLoader, Transport } from 'https://cdn.jsdelivr.net/npm/esptool-js@0.5.4/bundle.js';

const $ = <T extends HTMLElement = HTMLElement>(s: string): T | null =>
  document.querySelector<T>(s);

const flog = (t: string): void => {
  const e = $('#flashLog');
  if (e) {
    e.textContent = ((e.textContent ?? '') + t + '\n').split('\n').slice(-60).join('\n');
    e.scrollTop = 9e9;
  }
};

const setBar = (f: number): void => {
  const w = $('#flashBarWrap'), b = $('#flashBar');
  if (w && b) { w.style.display = ''; b.style.width = Math.round(f * 100) + '%'; }
};

/* esptool-js wants a binary string -- one character per byte -- rather than a
   Uint8Array. Chunked because String.fromCharCode.apply blows the argument
   limit on a 1.6 MB image. */
function binStr(ab: ArrayBuffer): string {
  const u = new Uint8Array(ab);
  let s = '';
  const C = 0x8000;
  for (let i = 0; i < u.length; i += C) {
    s += String.fromCharCode.apply(null, Array.from(u.subarray(i, i + C)));
  }
  return s;
}

async function fetchBin(name: string): Promise<string> {
  flog('Fetching ' + name + '…');
  const r = await fetch('firmware/' + name, { cache: 'no-store' });
  if (!r.ok) throw new Error(name + ' not found');
  return binStr(await r.arrayBuffer());
}

async function fromFile(f: File): Promise<string> { return binStr(await f.arrayBuffer()); }

async function flash(getData: () => Promise<string>, address: number, label: string): Promise<void> {
  const btns = [$<HTMLButtonElement>('#flashUpdate'), $<HTMLButtonElement>('#flashFull')]
    .filter((b): b is HTMLButtonElement => b !== null);
  btns.forEach(b => { b.disabled = true; });
  setBar(0);
  let transport: Transport | undefined;
  try {
    flog('Select the c3-camlink serial port in the chooser…');
    const port = await navigator.serial.requestPort();
    transport = new Transport(port, false);
    const loader = new ESPLoader({
      transport, baudrate: 115200, romBaudrate: 115200,
      terminal: { clean() { /* the log is append-only */ }, writeLine(d) { flog(d); }, write() { /* partial lines are noise */ } },
    });
    const chip = await loader.main();
    flog('Connected: ' + chip);
    const data = await getData();
    flog(label + ': writing ' + data.length + ' bytes at 0x' + address.toString(16) + ' — do not unplug…');
    await loader.writeFlash({
      fileArray: [{ data, address }], flashSize: 'keep', eraseAll: false, compress: true,
      reportProgress: (_i, w, t) => setBar(w / t),
    });
    setBar(1);
    flog('Written. Restarting the board…');
    await loader.after();
    flog('Done. The board is booting into setup — click “Connect over USB”.');
  } catch (e) {
    flog('Flash failed: ' + ((e as Error)?.message || String(e)));
  } finally {
    try { await transport?.disconnect(); } catch { /* it may already be gone */ }
    btns.forEach(b => { b.disabled = false; });
  }
}

const toggle = $('#flashToggle');
if (toggle) {
  toggle.onclick = () => {
    const p = $('#flashPanel');
    if (p) p.style.display = p.style.display === 'none' ? '' : 'none';
  };
}

// Update: write only the app slot (0x20000), leaving NVS -- and your settings -- untouched.
const btnUpdate = $('#flashUpdate');
if (btnUpdate) {
  btnUpdate.onclick = () => void flash(() => fetchBin('c3-camlink-app.bin'), 0x20000, 'Update (keeps settings)');
}

// Full install: the merged image at 0x0, which resets settings. For a blank board or a clean slate.
const btnFull = $('#flashFull');
if (btnFull) {
  btnFull.onclick = () => {
    if (confirm('A full install resets saved settings. Use this for a blank board or a clean slate. Continue?')) {
      void flash(() => fetchBin('c3-camlink-full.bin'), 0, 'Full install');
    }
  };
}

const picker = $<HTMLInputElement>('#flashFile');
if (picker) {
  picker.onchange = e => {
    const f = (e.target as HTMLInputElement).files?.[0];
    if (f) void flash(() => fromFile(f), 0, 'Custom .bin');
  };
}
