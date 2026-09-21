/* Web Serial transport. The page is the unmodified Wi-Fi settings page; the
   only change is that fetch('/api/...') is routed over the USB serial link
   instead of HTTP, so the exact same UI drives the module over the cable.

   This file is a PLAIN SCRIPT, not a module -- it is inlined into a bare
   <script> tag, and the IIFE is what keeps its names off the global object. */
(function () {
  interface Pending {
    res: (text: string) => void;
    rej: (err: Error) => void;
  }

  class Board {
    private n = 0;
    private pending: Record<string, Pending> = {};
    private buf = '';
    private enc = new TextEncoder();
    private closing = false;
    private port: SerialPort | null = null;
    private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
    private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
    lastError: string | null = null;
    onlost: ((why: string) => void) | null = null;

    get connected(): boolean { return !!this.port; }

    private async _open(): Promise<void> {
      const port = this.port;
      if (!port) throw new Error('no port');
      await port.open({ baudRate: 115200 });
      /* Both streams exist only between open() and close(). A port that opened
         and still has no writable is a driver that did not attach -- say so
         here rather than failing on the first api() with 'not connected'. */
      if (!port.writable) throw new Error('serial port opened without a writable stream');
      this.writer = port.writable.getWriter();
      this.buf = ''; void this._read();
    }

    async connect(): Promise<void> {
      this.port = await navigator.serial.requestPort();
      await this._open();
    }

    async reopen(): Promise<boolean> {
      for (let i = 0; i < 30; i++) {
        await new Promise(r => setTimeout(r, 500));
        try {
          const ps = await navigator.serial.getPorts();
          if (ps.length) { this.port = ps[0]; await this._open(); return true; }
        } catch { /* the board is still rebooting; try again */ }
      }
      return false;
    }

    async disconnect(): Promise<void> {
      this.closing = true;
      try { if (this.reader) await this.reader.cancel(); } catch { /* already gone */ }
      try { if (this.writer) this.writer.releaseLock(); } catch { /* already gone */ }
      try { if (this.port) await this.port.close(); } catch { /* already gone */ }
      this.port = null; this.closing = false;
      this._failPending('disconnected');
    }

    private _failPending(why: string): void {
      const p = this.pending; this.pending = {};
      for (const id in p) p[id].rej(new Error(why));
    }

    private async _read(): Promise<void> {
      const port = this.port;
      if (!port?.readable) return;
      const dec = new TextDecoder();
      this.reader = port.readable.getReader();
      let err: Error | null = null;
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
          this.buf += dec.decode(value, { stream: true });
          let i: number;
          while ((i = this.buf.indexOf('\n')) >= 0) {
            const line = this.buf.slice(0, i).replace(/\r$/, '');
            this.buf = this.buf.slice(i + 1);
            this._line(line);
          }
        }
      } catch (e) { err = e as Error; }
      /* A deliberate disconnect() cancels the reader and lands here too; that
         is not a fault. Anything else is the link dying under us and must be
         said out loud -- a silent null here is how "connected, no data" and a
         vanished Disconnect button happened on Linux. */
      const lost = !this.closing;
      this.port = null;
      if (lost) {
        const why = 'USB link lost' + (err?.message ? ': ' + err.message : ' (stream ended)');
        this.lastError = why; console.error('[c3-camlink] ' + why, err);
        this._failPending(why);
        if (this.onlost) this.onlost(why);
      }
    }

    private _line(line: string): void {
      if (line.startsWith('@api ')) {
        const m = /^@api (\S+) (ok|err) ?([\s\S]*)$/.exec(line);
        if (m) {
          const p = this.pending[m[1]];
          if (p) {
            delete this.pending[m[1]];
            if (m[2] === 'ok') p.res(m[3] || '{}'); else p.rej(new Error(m[3] || 'error'));
          }
        }
      }
    }

    api(verb: string, rest?: string): Promise<string> {
      const id = String(++this.n);
      const line = 'api ' + id + ' ' + verb + (rest ? ' ' + rest : '') + '\n';
      const writer = this.writer;
      if (!this.port || !writer) return Promise.reject(new Error(this.lastError || 'not connected'));
      return new Promise<string>((res, rej) => {
        this.pending[id] = { res, rej };
        writer.write(this.enc.encode(line)).catch((e: Error) => {
          if (this.pending[id]) {
            delete this.pending[id];
            rej(new Error('write failed: ' + (e.message || String(e))));
          }
        });
        setTimeout(() => {
          if (this.pending[id]) {
            delete this.pending[id];
            rej(new Error('timeout: no reply from the module in 4s'));
          }
        }, 4000);
      });
    }
  }

  const board = new Board();
  window._board = board;

  const real = window.fetch.bind(window);
  function resp(ok: boolean, text: string): Response {
    return {
      ok: ok,
      status: ok ? 200 : 400,
      json: () => Promise.resolve(JSON.parse(text || '{}') as unknown),
      text: () => Promise.resolve(text),
    } as Response;
  }

  /* Every endpoint the shared page can reach must be routable here, or it
     fails over USB while working over Wi-Fi -- which is how profile switching
     was silently dead on the cable for several versions.

     Record<Endpoint, string> is what enforces that: add an endpoint to the
     union in types/api.d.ts and this table stops compiling until it is
     handled. WITH_BODY names the ones that carry a form body; the rest are
     bare verbs, and sending a body to those would put junk on the wire. */
  const MAP: Record<Endpoint, string> = {
    state: 'state', scan: 'scan', config: 'config', osd: 'osd', bind: 'bind',
    profile: 'profile', forget: 'forget', repair: 'repair', exit: 'exit',
  };
  const WITH_BODY: ReadonlySet<Endpoint> = new Set<Endpoint>(['config', 'osd', 'bind', 'profile']);

  const isEndpoint = (s: string): s is Endpoint => Object.prototype.hasOwnProperty.call(MAP, s);

  window.fetch = async (input: RequestInfo | URL, opts?: RequestInit): Promise<Response> => {
    opts = opts || {};
    if (typeof input === 'string' && input.indexOf('/api/') === 0) {
      if (!board.connected) throw new Error('not connected');
      const ep = input.split('?')[0].slice(5);
      /* Only the two body forms the shared page actually sends. Anything else
         -- a Blob, a stream -- would stringify to '[object Object]' and put
         junk on the serial link rather than failing visibly. */
      const raw = opts.body;
      const body = typeof raw === 'string' ? raw
                 : raw instanceof URLSearchParams ? raw.toString() : '';
      /* Flashing over USB goes through esptool, not the settings API, so the
         firmware card is hidden in this tool -- but answer clearly if anything
         reaches here anyway. */
      if (ep === 'update') return resp(false, 'firmware update is not available over USB — use esptool');
      if (!isEndpoint(ep)) throw new Error('unknown endpoint ' + ep);
      try {
        const txt = await board.api(MAP[ep], WITH_BODY.has(ep) ? body : '');
        return resp(true, txt);
      } catch (e) { return resp(false, (e as Error).message || 'error'); }
    }
    return real(input, opts);
  };

  /* Called by the shared page each time it has fresh state. The banner is the
     only version readout on screen while the flash panel is open, so it has to
     be the board's version rather than the page's. */
  window.camlinkOnState = (s: State) => {
    const ct = document.getElementById('usbConnTxt');
    if (ct) ct.textContent = 'c3-camlink ' + s.fw;
  };

  const byId = <T extends HTMLElement = HTMLElement>(id: string): T | null =>
    document.getElementById(id) as T | null;

  function boot(): void {
    const gate = byId('usbGate');
    const btn = byId<HTMLButtonElement>('usbConnect');
    if (!gate || !btn) return;
    if (!('serial' in navigator)) {
      const ns = byId('usbNoSerial');
      if (ns) ns.style.display = '';
      btn.disabled = true;
    }
    /* An arrow bound after the guard above, not a hoisted declaration: a
       `function` here could in principle be called before `btn` was checked,
       so the compiler cannot see that it is non-null inside. */
    const ensureUsbcfg = async (): Promise<boolean> => {
      let m: { usbcfg?: boolean };
      try { m = JSON.parse(await board.api('mode')) as { usbcfg?: boolean }; }
      catch (e) {
        /* Only a firmware that answers "unknown-verb" is old-but-alive; a
           timeout or a lost link is a dead connection and must not pass. */
        const msg = (e as Error).message;
        if (/unknown-verb/.test(msg)) return true;
        throw new Error('module did not answer: ' + msg);
      }
      if (m.usbcfg) return true;
      btn.textContent = 'Entering setup…';
      try { await board.api('usbcfg'); } catch { /* reboots; the reply may be cut off */ }
      return await board.reopen();
    };
    board.onlost = why => {
      const conn = byId('usbConn');
      if (conn) conn.style.display = 'none';
      const nd = byId('usbNoData');
      if (nd) {
        nd.textContent = why.replace(/\.$/, '') + '. Unplug and replug the module, then Connect again.';
        nd.style.display = '';
      }
      gate.style.display = ''; btn.disabled = false; btn.textContent = 'Connect over USB';
    };
    btn.onclick = async () => {
      btn.disabled = true; btn.textContent = 'Connecting…';
      const nd = byId('usbNoData');
      if (nd) nd.style.display = 'none';
      try {
        await board.connect();
        if (!await ensureUsbcfg()) throw new Error('module did not come back in setup mode');
        gate.style.display = 'none';
        const conn = byId('usbConn');
        if (conn) conn.style.display = 'block';
      } catch (e) {
        btn.disabled = false; btn.textContent = 'Connect over USB';
        alert('Connect failed: ' + (e as Error).message);
      }
    };
    const doneBtn = byId<HTMLButtonElement>('done');
    if (doneBtn) {
      doneBtn.textContent = 'Test mode (normal boot)';
      doneBtn.title = 'Reboot the board into normal flight mode so the OSD and camera run over USB';
      doneBtn.onclick = async () => {
        if (!confirm('Reboot the board into normal flight mode for one boot?\n\nThe camera link and the OSD will run, drawn to your flight controller, so you can watch warnings in the goggles. This works over USB (which powers your FC). Reconnect here afterwards to return to setup.')) return;
        try { await board.api('normal'); } catch { /* it is rebooting */ }
        try { await board.disconnect(); } catch { /* already gone */ }
        alert('Board is rebooting into normal mode. Watch your OSD / goggles.\n\nReload this page and Connect to return to setup.');
        window.location.reload();
      };
    }
    // Keep the connection badge honest even if a reconnect left it out of sync.
    /* 'block', not '': the stylesheet hides #usbConn, so clearing the inline
       style falls back to display:none and the badge never appears at all. */
    setInterval(() => {
      const c = byId('usbConn');
      if (c) c.style.display = window._board?.connected ? 'block' : 'none';
    }, 800);
    const disc = byId('usbDisc');
    if (disc) {
      disc.onclick = async () => {
        await board.disconnect();
        const conn = byId('usbConn'); if (conn) conn.style.display = 'none';
        const nodata = byId('usbNoData'); if (nodata) nodata.style.display = 'none';
        gate.style.display = ''; btn.disabled = false; btn.textContent = 'Connect over USB';
        window.location.reload();   /* clean slate for the next connect */
      };
    }
    // Data watchdog: if nothing arrives shortly after connecting, say so.
    let sawData = false;
    const of = window.fetch.bind(window);
    window.fetch = async (u: RequestInfo | URL, o?: RequestInit): Promise<Response> => {
      const r = await of(u, o);
      if (typeof u === 'string' && u.indexOf('/api/state') === 0 && r.ok) {
        sawData = true;
        const nodata = byId('usbNoData');
        if (nodata) nodata.style.display = 'none';
      }
      return r;
    };
    setTimeout(() => {
      if (board.connected && !sawData) {
        const nodata = byId('usbNoData');
        if (nodata) nodata.style.display = '';
      }
    }, 4000);
    // Firmware-over-USB is not supported by this tool; hide that card. It must
    // stay in the DOM: fill() writes to #fwVer inside it, and removing it made
    // fill() throw before it populated anything else.
    const fw = [...document.querySelectorAll('section')].find(s => {
      const h = s.querySelector('h2');
      return h && h.textContent?.trim() === 'Firmware';
    });
    if (fw) fw.style.display = 'none';
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
  else boot();
})();
