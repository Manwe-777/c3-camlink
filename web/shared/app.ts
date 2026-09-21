/*
 * The settings page, transport-free.
 *
 * Nothing here knows whether it is talking to the module over Wi-Fi or over a
 * USB cable: every request goes through api(), which is a plain fetch to a
 * relative /api/ path. On the device that is a real HTTP request; in the
 * browser tool usb/transport.ts intercepts it and turns it into a line on the
 * serial link. See web/README.md.
 *
 * This file is a PLAIN SCRIPT, not a module -- no import, no export. It is
 * inlined into a bare <script> tag by tools/build_web.py.
 */

/* Every element this page addresses is in the markup it ships with, so a miss
 * is a bug in the page rather than a condition to handle. Throwing names the
 * selector; the alternative is a null dereference three lines later that
 * names nothing. */
function $<T extends HTMLElement = HTMLElement>(sel: string): T {
  const el = document.querySelector<T>(sel);
  if (!el) throw new Error('settings page is missing ' + sel);
  return el;
}

/* The one place an /api/ path is constructed. Endpoint is a closed union, so
 * a typo or an endpoint the USB transport cannot route is a compile error --
 * see the note on Endpoint in types/api.d.ts. */
function api(ep: Endpoint, init?: RequestInit): Promise<Response> {
  return fetch('/api/' + ep, init);
}

const form = (fields: Record<string, string | number>): URLSearchParams => {
  const p = new URLSearchParams();
  for (const k in fields) p.set(k, String(fields[k]));
  return p;
};

const AUX_MAX = 14;
let pollTimer: ReturnType<typeof setInterval> | null = null;
// Redraws are suppressed until this moment, so a poll cannot replace a row
// under a finger that is reaching for it.
//
// A DEADLINE, NOT A COUNTER. A counter incremented on pointerdown and
// decremented on pointerup leaks: a touch that turns into a scroll fires
// pointerdown and then pointercancel, never pointerup, so one scroll leaves it
// stuck above zero and polling stops for the rest of the session. The page
// then appears frozen -- selecting a camera does nothing, because nothing
// redraws it.
//
// A deadline cannot leak. The worst case is that redraws resume a couple of
// seconds after the last touch, which is self-correcting by construction
// rather than by remembering to pair every event.
let holdUntil = 0;
const holdRedraw = (ms: number): void => { holdUntil = Date.now() + ms; };
const redrawAllowed = (): boolean => Date.now() >= holdUntil;
let loaded = false;
let dirty = false;            // form edited since the last successful save

// Save is a separate button from Done & restart, so a form can be edited,
// admired, and then discarded by a restart without a single thing on screen
// having said so. Everything below exists to make that impossible: the button
// says there is something to save, and the restart says what it would cost.
function setDirty(v: boolean): void {
  dirty = v;
  $('#save').textContent = v ? 'Save changes' : 'Save';
  $('#save').classList.toggle('dirty', v);
}

for (let i = 1; i <= AUX_MAX; i++) {
  const o = document.createElement('option');
  o.value = String(i); o.textContent = 'AUX ' + i;
  $('#aux').appendChild(o);
}
{
  for (const sel of ['#cfgaux', '#hilight']) {
    const off = document.createElement('option');
    off.value = '0'; off.textContent = 'Off';
    $(sel).appendChild(off);
    for (let i = 1; i <= AUX_MAX; i++) {
      const o = document.createElement('option');
      o.value = String(i); o.textContent = 'AUX ' + i;
      $(sel).appendChild(o);
    }
  }
}

let toastTimer: ReturnType<typeof setTimeout> | undefined;
function toast(msg: string): void {
  const t = $('#toast');
  t.textContent = msg; t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), 1800);
}

// 900-2100, the same span Betaflight's Modes tab uses -- and the same span the
// receiver actually produces. A CRSF or SBUS channel at +100% lands on 2012 us,
// so a track that stopped at 2000 would pin the marker to the end and report
// the switch as outside a window it is visibly sitting at the top of.
const US_LO = 900, US_HI = 2100;
const pct = (us: number): number =>
  Math.max(0, Math.min(100, (us - US_LO) * 100 / (US_HI - US_LO)));

// One description at a time, under the switch, for the option actually
// chosen. Showing all of them at once makes the page a wall of text the user
// has to read before they can choose.
const MODE_DESC: Record<string, string> = {
  '0': '<b>Recording follows arming.</b> Arm the quad and the camera rolls; ' +
       'disarm and it stops. Nothing else to set up.',
  '1': '<b>A switch on your radio starts and stops recording.</b> Arming does ' +
       'nothing on its own — you decide when the camera rolls.',
  '2': '<b>Arming records, and the switch cuts a take.</b> Flick it mid-flight ' +
       'to end the current clip and immediately start a fresh one. It never ' +
       'stops recording — it only breaks the footage into separate files.',
  '3': '<b>Arming records, and the switch works on its own as well.</b> Arm and ' +
       'the camera rolls; flick the switch mid-flight to stop recording, flick ' +
       'it again to start. Arming or disarming takes control back, so you ' +
       'always land on a stopped camera.',
};

const KIND_DESC: Record<string, string> = {
  '0': '<b>A switch that holds its position.</b> Recording runs while the ' +
       'channel sits inside the window below. Survives a brownout: the switch ' +
       'still says where it is, so the module works out the rest.',
  '1': '<b>A button you press and release.</b> Each press flips recording on ' +
       'or off, and the window below is ignored — a button leaves the channel ' +
       'wherever the press put it, so there is no position to test. The ' +
       'trade-off is that after a power cut the module cannot know which way ' +
       'you left it.',
};

const CFG_KIND_DESC: Record<string, string> = {
  '0': '<b>A switch, in two parts, with the OSD counting you through it.</b> ' +
       'Flick it up and the OSD says <code>ENTER</code>; after the hold time ' +
       'it says <code>READY</code>; drop the switch and it says ' +
       '<code>CONFIG</code> and setup opens. Let go before <code>READY</code> ' +
       'and nothing happens — the OSD goes straight back to what it was ' +
       'saying. Committing on the way <i>down</i> also means the switch is ' +
       'already off when setup starts, so leaving it cannot walk straight ' +
       'back in.',
  '1': '<b>A button — this acts instantly.</b> Any movement of the channel ' +
       'says <code>CONFIG</code> and opens setup at once, with no hold and no ' +
       'release to wait for. Quicker, and correspondingly easier to trigger ' +
       'by accident: there is nothing between the press and the reboot but ' +
       'the check that you are disarmed.',
};

const TX_DESC: Record<string, string> = {
  '0': '<b>−24 dBm.</b> Correct once the camera is mounted on the quad, ' +
       'centimetres away. Quietest next to your receiver.',
  '1': '<b>−12 dBm.</b> Camera on the same desk.',
  '2': '<b>0 dBm.</b> Camera across the room.',
  '3': '<b>+3 dBm.</b> Maximum reach, for bench work only.',
};

const input = (sel: string): HTMLInputElement => $<HTMLInputElement>(sel);
const select = (sel: string): HTMLSelectElement => $<HTMLSelectElement>(sel);
const radio = (name: string): HTMLInputElement | null =>
  document.querySelector<HTMLInputElement>(`input[name=${name}]:checked`);

// Mirrors camlink_cfg_in_window() in camlink_cfg.h. A limit dragged to the end
// of its travel means "and beyond": a receiver at +100% is not obliged to stop
// on the number the slider stops on, and the marker must agree with the module
// about whether the switch counts.
function inWindow(us: number): boolean {
  const lo = +input('#min').value, hi = +input('#max').value;
  return (lo <= US_LO || us >= lo) && (hi >= US_HI || us <= hi);
}

function paint(): void {
  const lo = +input('#min').value, hi = +input('#max').value;
  $('#minV').textContent = String(lo);
  $('#maxV').textContent = String(hi);
  $('#window').style.left  = pct(lo) + '%';
  $('#window').style.width = (pct(hi) - pct(lo)) + '%';

  const mode = radio('mode')?.value;
  $('#modeDesc').innerHTML = (mode && MODE_DESC[mode]) || '';

  const kind = radio('kind')?.value;
  $('#kindDesc').innerHTML = (kind && KIND_DESC[kind]) || '';
  // A button ignores the window, so showing a window to tune is a lie.
  $('#windowCard').classList.toggle('hide', kind === '1');
  // Arm mode watches arm state and nothing else, so the channel settings are
  // not merely irrelevant — showing them would imply they still apply.
  $('#chanCard').classList.toggle('hide', mode === '0');

  const cfgAux  = +select('#cfgaux').value;
  const cfgKind = radio('cfgkind')?.value;
  $('#cfgCard').classList.toggle('hide', !cfgAux);
  $('#cfgKindDesc').innerHTML = (cfgKind && CFG_KIND_DESC[cfgKind]) || '';
  // A button has no position to hold, so a hold time would be a control that
  // does nothing.
  $('#cfgHoldCard').classList.toggle('hide', cfgKind === '1');
  const hd = +input('#cfghold').value;
  $('#cfgHoldV').textContent = hd === 0 ? 'instantly' : (hd / 10).toFixed(1) + ' s';
  const sd = +input('#stopdelay').value;
  $('#stopdelayV').textContent = sd === 0 ? 'off' : sd + ' s';

  const tx = radio('tx')?.value;
  $('#txDesc').innerHTML = (tx && TX_DESC[tx]) || '';
  $('#txWarn').textContent = (!input('#txauto').checked && (tx === '3' || tx === '2'))
    ? 'Set this back to Low before flying, or switch on the option below.' : '';
}

// The two ends of the window must not cross. A min above max is a range that
// can never match, which silently means "never record" -- a setting that looks
// saved and does nothing is worse than one that refuses to be set.
//
// Each handle is clamped so the OTHER one still has somewhere to go. Pushing
// the other handle past 2100 instead lets the range input clamp it back, which
// leaves min == max: a window that means "never record" and that the firmware
// rightly refuses as out of range.
const US_MIN = 900, US_MAX = 2100, US_GAP = 5;
input('#min').addEventListener('input', () => {
  let lo = +input('#min').value, hi = +input('#max').value;
  if (lo > US_MAX - US_GAP) { lo = US_MAX - US_GAP; input('#min').value = String(lo); }
  if (hi <= lo) { hi = lo + US_GAP; input('#max').value = String(hi); }
  paint();
});
input('#max').addEventListener('input', () => {
  let lo = +input('#min').value, hi = +input('#max').value;
  if (hi < US_MIN + US_GAP) { hi = US_MIN + US_GAP; input('#max').value = String(hi); }
  if (lo >= hi) { lo = hi - US_GAP; input('#min').value = String(lo); }
  paint();
});
document.addEventListener('input', paint);

// Only the controls that are actually persisted. The file picker and the
// camera list are not settings and must not light the Save button up.
document.addEventListener('change', e => {
  if (!loaded) return;
  const t = e.target as HTMLInputElement | null;
  if (!t) return;
  if (t.name === 'mode' || t.name === 'tx' || t.name === 'kind' ||
      t.id === 'aux' || t.id === 'min' || t.id === 'max' ||
      t.id === 'txauto' || t.id === 'cfgaux' || t.id === 'cfghold' ||
      t.id === 'hilight' || t.id === 'stopdelay' || t.name === 'cfgkind') setDirty(true);
});
['min', 'max', 'cfghold', 'stopdelay'].forEach(id =>
  $('#' + id).addEventListener('input', () => { if (loaded) setDirty(true); }));
document.addEventListener('pointerdown',   () => holdRedraw(2000));
document.addEventListener('pointerup',     () => holdRedraw(400));
document.addEventListener('pointercancel', () => holdRedraw(400));
document.addEventListener('pointermove',   e => {
  // Keep the hold alive during a drag, but only while a pointer is actually
  // down -- a mouse moving across the page must not stop the list updating.
  if (e.buttons || e.pointerType === 'touch') holdRedraw(2000);
});

function fill(s: State): void {
  s.fields.forEach(f => { CAT[f.id] = f; });
  CAT[0] = { id: 0, name: '', ex: '', w: 0 };
  OSDMAX = s.osdmax;
  layout = s.osd.map(r => r.filter(id => id !== 0));
  drawOsd();
  setOsdDirty(false);

  $('#meta').textContent = s.ssid + ' · ' + s.fw;
  // Optional transport hook. The USB tool puts the board's version in its
  // connection banner; the Wi-Fi page has nowhere to show it and leaves this
  // undefined.
  window.camlinkOnState?.(s);
  $('#fwVer').textContent = s.fw;
  $<HTMLInputElement>(`input[name=mode][value="${s.cfg.mode}"]`).checked = true;
  $<HTMLInputElement>(`input[name=tx][value="${s.cfg.tx}"]`).checked = true;
  $<HTMLInputElement>(`input[name=kind][value="${s.cfg.kind}"]`).checked = true;
  input('#txauto').checked = !!s.cfg.txauto;
  select('#cfgaux').value = String(s.cfg.cfgaux || 0);
  select('#hilight').value = String(s.cfg.hilight || 0);
  input('#stopdelay').value = String(s.cfg.stopdelay != null ? s.cfg.stopdelay : 5);
  $<HTMLInputElement>(`input[name=cfgkind][value="${s.cfg.cfgkind}"]`).checked = true;
  input('#cfghold').value = String(s.cfg.cfghold);
  select('#aux').value = String(Math.min(Math.max(s.cfg.aux, 1), AUX_MAX));
  input('#min').value = String(s.cfg.min);
  input('#max').value = String(s.cfg.max);
  paint();
}

function live(s: State): void {
  $('#camBound').textContent = s.cam.bound ? 'Selected: ' + s.cam.mac
                                           : 'No camera selected';
  $<HTMLButtonElement>('#forget').disabled = !s.cam.bound;

  // Saved-camera profiles: tap one to make it active (takes effect next boot).
  const pf: Profiles = s.profiles || { list: [], active: -1 };
  const pw = $('#profileWrap'), pl = $('#profileList');
  if (pf.list.length >= 2) {
    pw.classList.remove('hide');
    const key = pf.active + ':' + pf.list.map(p => p.mac).join(',');
    if (pl.dataset.key !== key) {
      pl.dataset.key = key;
      pl.innerHTML = pf.list.map((p, i) =>
        `<button class="ghost" data-idx="${i}" style="padding:6px 12px;border-radius:16px;` +
        (i === pf.active ? 'border:2px solid #0f8a4a;color:#0f8a4a;font-weight:600' : '') +
        `">${p.label}</button>`).join('');
      pl.querySelectorAll('button').forEach(b => {
        b.onclick = async () => {
          try {
            const r = await api('profile', { method: 'POST', body: form({ idx: b.dataset.idx ?? '' }) });
            toast(r.ok ? 'Active camera set — restart to use it' : 'Failed: ' + (await r.text()));
          } catch { toast('Failed'); }
        };
      });
    }
  } else {
    pw.classList.add('hide');
  }

  // Why the link is down, in the words of someone holding a quad. Shown only
  // when there is something to say: a module that has never had a failure
  // should not display a fault box explaining that nothing is wrong.
  const f = $('#linkFault');
  if (s.cam.bound && s.cam.fault && s.cam.fails > 0) {
    f.classList.remove('hide');
    $('#faultText').innerHTML =
      '<b>' + s.cam.fault + '</b><br>' + s.cam.fails +
      (s.cam.fails === 1 ? ' failed attempt' : ' failed attempts') +
      (s.cam.everup ? ', after connecting successfully earlier.'
                    : ', and it has not connected once since power-up.');
    // The camera list above is the other half of the answer, so say which half
    // to read: present-but-weak and absent-entirely need different fixes.
    const seen = (window._lastCams || []).find(c => c.bound);
    $('#faultHint').textContent = seen
      ? (seen.rssi > -80
          // Strong signal and it still refuses. Signal is not the problem, and
          // saying "move it closer" here sends people to rearrange their quad
          // for nothing. A camera that advertises but will not complete a
          // connection is almost always already talking to something else.
          ? 'Your camera is listed above at ' + seen.rssi + ' dBm, so it is on the ' +
            'air and close enough. A camera that is heard clearly but still refuses ' +
            'to connect is usually busy with something else: close DJI Mimo, turn ' +
            'off Bluetooth on your phone, then switch the camera off and on again. ' +
            'Note that a camera that is asleep still appears in this list.'
          : 'Your camera is listed above at ' + seen.rssi + ' dBm, which is weak. ' +
            'Move it closer or raise Radio power, and check it is actually switched ' +
            'on — a camera that is off or asleep still appears here.')
      : 'Your camera is not in the list above, so nothing is hearing it. Check it is ' +
        'switched on, charged, and not already connected to a phone.';
  } else {
    f.classList.add('hide');
  }

  const fp = $('#fcPill'), ap = $('#armPill');
  fp.textContent = s.fc.link ? 'connected' : 'no link';
  fp.className = 'pill ' + (s.fc.link ? 'on' : 'off');
  ap.textContent = s.fc.link ? (s.fc.armed ? 'ARMED' : 'disarmed') : '—';
  ap.className = 'pill ' + (s.fc.link && s.fc.armed ? 'off' : '');

  const idx = +select('#aux').value + 3;
  const raw = (s.fc.link && s.fc.rc) ? s.fc.rc[idx] : undefined;
  const us  = Number.isFinite(raw) ? (raw as number) : null;
  const m = $('#marker');
  if (us === null) {
    $('#live').textContent = s.fc.link ? 'channel not sent' : 'no link';
    m.classList.remove('in');
  } else {
    $('#live').textContent = us + ' us';
    m.style.left = pct(us) + '%';
    m.classList.toggle('in', inWindow(us));
  }
}

// Signal as four bars. RSSI is a log scale nobody reads at a glance, but
// "yours is the full one" needs no explanation.
const barCount = (rssi: number): number =>
  rssi > -55 ? 4 : rssi > -70 ? 3 : rssi > -82 ? 2 : 1;

function bars(rssi: number): string {
  const n = barCount(rssi);
  let h = '<span class="bars">';
  for (let i = 1; i <= 4; i++)
    h += `<i class="${i <= n ? 'on' : ''}" style="height:${4 + i * 3}px"></i>`;
  return h + '</span>';
}

let armedMac: string | null = null;
let armTimer: ReturnType<typeof setTimeout> | undefined;

function renderCams(cams: Cam[]): void {
  const box = $('#camList');
  if (!cams.length) {
    box.innerHTML = '<div class="note">Looking for cameras… switch yours on ' +
                    'and keep it close to the module.</div>';
    return;
  }
  // Rebuild only when something STRUCTURAL has changed -- which cameras are
  // listed, and which is selected or armed. RSSI is deliberately not in this
  // key: it moves by ten dB between adverts on a stationary camera, so keying
  // on it would rebuild the whole list roughly every poll, and a tap could land
  // on an element that a 400 ms refresh had already replaced. The bars are
  // refreshed in place below instead, which is the only part that changed.
  // c.model is in the key because a GoPro's model is read from its
  // manufacturer data, which does not arrive in every advert: the row can
  // legitimately go from 'GoPro' to 'GoPro HERO (2024)' a poll later. Keyed
  // on id and bound alone, that row would never be rebuilt to say so.
  const key = cams.map(c => c.id + c.bound + c.model).join('|') + '#' + armedMac;
  if (box.dataset.key === key) { paintBars(cams); return; }
  box.dataset.key = key;

  box.innerHTML = cams.map(c => {
    const cls = c.id === armedMac ? 'armed' : (c.bound ? 'sel' : '');
    const label = c.id === armedMac ? 'Tap again to select'
                : c.bound          ? 'Selected'
                : (c.model || c.name || 'Camera');
    // data-id is the separator-free form the firmware parses; c.mac with its
    // colons is for the human reading the row.
    // A note is advisory and never blocks the tap: this firmware binds
    // cameras GoPro does not list, including the ones it is tested against.
    const note = c.note ? `<div class="sub note">${c.note}</div>` : '';
    return `<div class="cam ${cls}" data-id="${c.id}">
      <div class="info"><div class="nm">${c.name || c.model || 'Camera'}</div>
      <div class="sub">${label} · ${c.mac}</div>${note}</div>${bars(c.rssi)}</div>`;
  }).join('');

  box.querySelectorAll<HTMLElement>('.cam').forEach(el => {
    el.onclick = () => { void selectCam(el.dataset.id ?? ''); };
  });
  paintBars(cams);
}

// Signal strength changes constantly; the rows must not. Swap the bars inside
// the existing elements so nothing the user is reaching for moves or is
// replaced mid-tap.
function paintBars(cams: Cam[]): void {
  cams.forEach(c => {
    const el = document.querySelector(`#camList .cam[data-id="${c.id}"] .bars`);
    if (!el) return;
    const n = barCount(c.rssi);
    el.querySelectorAll('i').forEach((bar, i) => bar.classList.toggle('on', i < n));
  });
}

// Two-tap, like every other destructive action on this page: rebinding points
// the module at a different camera, and a stray tap in a list should not do it.
async function selectCam(id: string): Promise<void> {
  if (armedMac !== id) {
    armedMac = id;
    clearTimeout(armTimer);
    armTimer = setTimeout(() => { armedMac = null; $('#camList').dataset.key = ''; }, 5000);
    $('#camList').dataset.key = '';
    return;
  }
  clearTimeout(armTimer);
  armedMac = null;
  try {
    const r = await api('bind', { method: 'POST', body: form({ mac: id }) });
    toast(r.ok ? 'Camera selected' : 'Could not select: ' + (await r.text()));
  } catch { toast('Could not select'); }
  $('#camList').dataset.key = '';
}

async function poll(): Promise<void> {
  try {
    const s = await (await api('state', { cache: 'no-store' })).json() as State;
    if (!loaded) { fill(s); loaded = true; }
    live(s);
    const sc = await (await api('scan', { cache: 'no-store' })).json() as ScanResult;
    window._lastCams = sc.cams || [];
    renderCams(sc.cams || []);
  } catch { /* the module reboots out from under us on purpose */ }
}


// ---- OSD layout editor ----------------------------------------------------
// The catalogue (names, examples and worst-case widths) comes from the
// firmware, not from here. Widths belong to the renderer, and a copy of them in
// this file would be a second source of truth that drifts the first time a
// field is added.
const CAT: Record<number, OsdField> = {};
let OSDMAX = 16;
let layout: number[][] = [];
let osdDirty = false;
const ROW_FIELDS = 4;

function setOsdDirty(v: boolean): void {
  osdDirty = v;
  $('#osdSave').textContent = v ? 'Save layout' : 'Saved';
  $('#osdSave').classList.toggle('dirty', v);
}

const rowWidth = (r: number[]): number =>
  r.reduce((w, id, i) => w + CAT[id].w + (i ? 1 : 0), 0);
const pad = (t: string, n: number): string => (t + ' '.repeat(n)).slice(0, n);

function drawOsd(): void {
  // Never rebuild the grid mid-drag: replacing the tiles destroys the element
  // the pointer is captured on, its pointerup never fires, and the floating
  // clone is left stuck over the page. The drag's own pointerup clears `drag`
  // before it calls drawOsd, so a legitimate redraw still gets through.
  if (drag) return;
  document.querySelectorAll('.otile.drag').forEach(g => g.remove());  // sweep orphans
  const box = $('#rows');
  box.innerHTML = layout.map((r, i) => {
    const w = rowWidth(r), over = w > OSDMAX;
    const pctW = Math.min(100, w * 100 / OSDMAX);
    return `<div class="orow${over ? ' bad' : ''}" data-row="${i}">
      <div class="oslots">${r.length ? r.map((id, j) =>
        `<span class="otile" data-row="${i}" data-idx="${j}" data-id="${id}"
         style="min-width:${CAT[id].w}ch">${CAT[id].name}</span>`).join('')
        : '<span class="ohint">Row ' + (i + 1) + ' — empty</span>'}</div>
      <div class="obar${over ? ' bad' : ''}"><i style="width:${pctW}%"></i></div>
      <div class="ometa"><span>Row ${i + 1}</span>
        <span class="${over ? 'over' : ''}">${w} / ${OSDMAX}${over ? ' — too long' : ''}</span>
      </div></div>`;
  }).join('');

  // The tray holds every field not currently placed. A field can only be in one
  // row: putting the same reading on screen twice is never what was meant, and
  // allowing it would double-charge the budget for no gain.
  const used = new Set(layout.flat());
  $('#tray').innerHTML = Object.keys(CAT).filter(id => +id && !used.has(+id))
    .map(id => `<span class="otile" data-row="-1" data-id="${id}"
      style="min-width:${CAT[+id].w}ch">${CAT[+id].name}</span>`).join('')
    || '<span class="ohint">All fields placed</span>';

  $('#osdPrev').textContent = layout.map(r =>
    r.length ? r.map(id => pad(CAT[id].ex, CAT[id].w)).join(' ') : ' ').join('\n');

  $<HTMLButtonElement>('#osdSave').disabled = layout.some(r => rowWidth(r) > OSDMAX);
  bindTiles();
}

interface Drag {
  el: HTMLElement;
  ghost: HTMLElement;
  id: number;
  from: number;
  idx: number;
  dx: number;
  dy: number;
}

// Pointer events rather than HTML5 drag-and-drop: the settings page is opened
// on a phone, and native dragstart/drop does not fire for touch at all. This
// works the same for a finger and a mouse.
let drag: Drag | null = null;
// Safety net: if the dragged tile is torn out from under us (a redraw, an error
// mid-drag), its own pointerup cannot fire. Catch the event at the window and
// clean up, so a stuck clone can never block the page until a restart.
window.addEventListener('pointerup', () => {
  if (drag && !document.body.contains(drag.el)) {
    drag.ghost.remove();
    drag = null;
    document.querySelectorAll('.otile.drag').forEach(g => g.remove());
    holdRedraw(400);
    drawOsd();
  }
});
window.addEventListener('pointercancel', () => {
  if (drag) {
    drag.ghost.remove();
    drag = null;
    document.querySelectorAll('.otile.drag').forEach(g => g.remove());
    drawOsd();
  }
});
function bindTiles(): void {
  document.querySelectorAll<HTMLElement>('.otile').forEach(el => {
    el.onpointerdown = e => {
      e.preventDefault();
      document.querySelectorAll('.otile.drag').forEach(g => g.remove());
      const ghost = el.cloneNode(true) as HTMLElement;
      ghost.classList.add('drag');
      const r = el.getBoundingClientRect();
      ghost.style.width = r.width + 'px';
      ghost.style.left = r.left + 'px';
      ghost.style.top = r.top + 'px';
      document.body.appendChild(ghost);
      el.classList.add('ghosted');
      drag = { el, ghost, id: +(el.dataset.id ?? 0),
               from: +(el.dataset.row ?? -1), idx: +(el.dataset.idx ?? -1),
               dx: e.clientX - r.left, dy: e.clientY - r.top };
      el.setPointerCapture(e.pointerId);
      holdRedraw(2000);                // keep the poll from redrawing mid-drag
    };
    el.onpointermove = e => {
      if (!drag) return;
      drag.ghost.style.left = (e.clientX - drag.dx) + 'px';
      drag.ghost.style.top  = (e.clientY - drag.dy) + 'px';
      const t = document.elementFromPoint(e.clientX, e.clientY);
      document.querySelectorAll('.orow').forEach(x => x.classList.remove('over'));
      const row = t?.closest('.orow');
      if (row) row.classList.add('over');
    };
    el.onpointerup = e => {
      if (!drag) return;
      const t = document.elementFromPoint(e.clientX, e.clientY);
      const row = t?.closest<HTMLElement>('.orow');
      const tray = t?.closest('#tray');
      drag.ghost.remove();
      document.querySelectorAll('.orow').forEach(x => x.classList.remove('over'));

      if (drag.from >= 0) layout[drag.from].splice(drag.idx, 1);
      if (row) {
        const dest = +(row.dataset.row ?? 0);
        if (layout[dest].length < ROW_FIELDS) {
          // Drop where the finger is, not always at the end: order within a row
          // is what decides which reading sits where on screen.
          const dragged = drag.el;
          const tiles = [...row.querySelectorAll('.otile')].filter(x => x !== dragged);
          const at = tiles.findIndex(x => {
            const b = x.getBoundingClientRect();
            return e.clientX < b.left + b.width / 2;
          });
          layout[dest].splice(at < 0 ? layout[dest].length : at, 0, drag.id);
        } else if (drag.from >= 0) {
          layout[drag.from].splice(drag.idx, 0, drag.id);   // full: put it back
          toast('That row already holds four fields');
        }
      } else if (!tray && drag.from >= 0) {
        layout[drag.from].splice(drag.idx, 0, drag.id);     // dropped nowhere
      }
      drag = null;
      holdRedraw(400);
      setOsdDirty(true);
      drawOsd();
    };
    el.onpointercancel = el.onpointerup;
  });
}

$('#osdReset').onclick = () => {
  layout = [[1, 7], [2], [3], [5]];    // state+dot, clip, battery, card
  setOsdDirty(true);
  drawOsd();
};

$('#osdSave').onclick = async () => {
  const flat = layout.map(r => [...r, ...Array<number>(ROW_FIELDS - r.length).fill(0)]).flat();
  try {
    const r = await api('osd', { method: 'POST', body: form({ osd: flat.join(',') }) });
    if (r.ok) setOsdDirty(false);
    toast(r.ok ? 'Layout saved' : 'Rejected: ' + (await r.text()));
  } catch { toast('Save failed'); }
};

$('#save').onclick = async () => {
  const body = form({
    mode: radio('mode')?.value ?? '',
    tx:   radio('tx')?.value ?? '',
    kind: radio('kind')?.value ?? '',
    txauto: input('#txauto').checked ? 1 : 0,
    cfgaux:  select('#cfgaux').value,
    hilight: select('#hilight').value,
    stopdelay: input('#stopdelay').value,
    cfgkind: radio('cfgkind')?.value ?? '',
    cfghold: input('#cfghold').value,
    aux:  select('#aux').value,
    min:  input('#min').value,
    max:  input('#max').value,
  });
  try {
    const r = await api('config', { method: 'POST', body });
    if (r.ok) setDirty(false);
    toast(r.ok ? 'Saved' : 'Rejected: ' + (await r.text()));
  } catch { toast('Save failed'); }
};

// Native confirm()/alert() are unreliable inside a captive-portal sheet --
// the stripped-down web view a phone opens on joining a network can return
// false without ever showing anything, which reads as "the button does
// nothing". Both destructive actions below are therefore two-step in-page,
// using only DOM, which works the same everywhere.
function arm(btn: HTMLElement, prompt: string | (() => string), action: () => void): void {
  let armed = false;
  let timer: ReturnType<typeof setTimeout> | undefined;
  const original = btn.textContent;
  btn.onclick = () => {
    if (!armed) {
      armed = true;
      // Resolved at press time, not at wire time: the Done button has to be
      // able to say whether this restart is about to throw an edit away.
      btn.textContent = (typeof prompt === 'function') ? prompt() : prompt;
      btn.classList.add('armed');
      timer = setTimeout(() => {
        armed = false; btn.textContent = original; btn.classList.remove('armed');
      }, 5000);
      return;
    }
    clearTimeout(timer);
    armed = false; btn.textContent = original; btn.classList.remove('armed');
    action();
  };
}

const startPolling = (): void => {
  pollTimer = setInterval(() => { if (redrawAllowed()) void poll(); }, 400);
};
startPolling();

// ---- firmware update ------------------------------------------------------
let fwBlob: File | null = null;
input('#fwFile').onchange = () => {
  fwBlob = input('#fwFile').files?.[0] ?? null;
  $('#fwName').textContent = fwBlob
    ? `${fwBlob.name} (${(fwBlob.size / 1048576).toFixed(2)} MB)` : 'No file chosen';
  $<HTMLButtonElement>('#fwGo').disabled = !fwBlob;
  if (fwBlob && fwBlob.size < 100000) {
    $('#fwNote').innerHTML = '<b style="color:var(--warn)">That file is only ' +
      Math.round(fwBlob.size / 1024) + ' KB — an update is normally over 1 MB. ' +
      'Check you picked the update file.</b>';
  }
};

$('#fwGo').onclick = () => {
  if (!fwBlob) return;
  // Polling competes with a 1.4 MB upload for the module's few sockets, and
  // nothing on screen matters more than the transfer right now.
  if (pollTimer) clearInterval(pollTimer);

  $<HTMLButtonElement>('#fwGo').disabled = true;
  $('#fwGo').textContent = 'Uploading…';
  $('#fwTrack').style.display = 'block';

  // XMLHttpRequest rather than fetch: fetch has no upload progress events, and
  // a minute-long transfer with no feedback is indistinguishable from a hang.
  // This is also why /api/update is not in the Endpoint union -- it never goes
  // through api(), and the USB tool hides the card that reaches it.
  const x = new XMLHttpRequest();
  x.open('POST', '/api/update');
  x.upload.onprogress = ev => {
    if (!ev.lengthComputable) return;
    const p = 100 * ev.loaded / ev.total;
    $('#fwBar').style.width = p + '%';
    $('#fwGo').textContent = `Uploading ${p.toFixed(0)}%`;
  };
  x.onload = () => {
    if (x.status === 200) {
      document.body.innerHTML =
        '<div class="wrap" style="padding:48px 16px"><h1>Installing</h1>' +
        '<p class="meta">The module is restarting on the new version. ' +
        '<b>Leave it powered for 30 seconds</b> — it has to prove it runs ' +
        'before it is kept. If it cannot, the previous version comes back ' +
        'by itself.</p></div>';
    } else {
      $('#fwGo').textContent = 'Install';
      $<HTMLButtonElement>('#fwGo').disabled = false;
      $('#fwNote').innerHTML = '<b style="color:var(--bad)">Rejected: ' +
        (x.responseText || x.status) + '</b>';
      startPolling();
    }
  };
  x.onerror = () => {
    $('#fwGo').textContent = 'Install';
    $<HTMLButtonElement>('#fwGo').disabled = false;
    $('#fwNote').innerHTML =
      '<b style="color:var(--bad)">Upload failed — the module still has its ' +
      'old firmware.</b>';
    startPolling();
  };
  x.send(fwBlob);
};

arm($('#done'), () => (dirty || osdDirty)
      ? 'Unsaved — tap again to discard' : 'Tap again to restart', () => {
  if (pollTimer) clearInterval(pollTimer);   // stop polling a module that is going away
  api('exit', { method: 'POST' }).catch(() => { /* it is leaving; nobody to tell */ });

  document.body.innerHTML =
    '<div class="wrap" style="padding:48px 16px"><h1>Restarting</h1>' +
    '<p class="meta">The c3-camlink network is closing. The module is back to ' +
    'normal operation &mdash; you can close this page.</p></div>';

  // Best effort only. A browser lets a page close a window that a SCRIPT
  // opened; a captive-portal sheet usually honours it, a tab the user opened
  // themselves never will. So the screen above is the real answer and this is
  // the bonus -- never the other way round.
  setTimeout(() => { try { window.close(); } catch { /* not ours to close */ } }, 400);
});

arm($('#repair'), 'Tap again to forget keys', () => {
  void (async () => {
    try {
      const r = await api('repair', { method: 'POST' });
      toast(r.ok ? 'Pairing keys forgotten — the camera will ask to pair again'
                 : 'Could not clear the keys');
    } catch { toast('Could not clear the keys'); }
  })();
});

arm($('#forget'), 'Tap again to release', () => {
  void (async () => {
    try {
      const r = await api('forget', { method: 'POST' });
      toast(r.ok ? 'Camera released' : 'Failed');
      $('#camList').dataset.key = '';
    } catch { toast('Failed'); }
  })();
});

void poll();
