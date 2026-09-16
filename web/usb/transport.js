/* Web Serial transport. The page is the unmodified Wi-Fi settings page; the
   only change is that fetch('/api/...') is routed over the USB serial link
   instead of HTTP, so the exact same UI drives the module over the cable. */
(function(){
  class Board{
    constructor(){this.n=0;this.pending={};this.buf='';this.enc=new TextEncoder();this.closing=false;this.lastError=null;this.onlost=null;}
    get connected(){return !!this.port;}
    async _open(){
      await this.port.open({baudRate:115200});
      this.writer=this.port.writable.getWriter();
      this.buf='';this._read();
    }
    async connect(){
      this.port=await navigator.serial.requestPort();
      await this._open();
    }
    async reopen(){
      for(let i=0;i<30;i++){
        await new Promise(r=>setTimeout(r,500));
        try{const ps=await navigator.serial.getPorts();if(ps.length){this.port=ps[0];await this._open();return true;}}catch(e){}
      }
      return false;
    }
    async disconnect(){
      this.closing=true;
      try{this.reader&&await this.reader.cancel();}catch(e){}
      try{this.writer&&this.writer.releaseLock();}catch(e){}
      try{this.port&&await this.port.close();}catch(e){}
      this.port=null;this.closing=false;
      this._failPending('disconnected');
    }
    _failPending(why){
      const p=this.pending;this.pending={};
      for(const id in p)p[id].rej(new Error(why));
    }
    async _read(){
      const dec=new TextDecoder();this.reader=this.port.readable.getReader();let err=null;
      try{for(;;){const{value,done}=await this.reader.read();if(done)break;
        this.buf+=dec.decode(value,{stream:true});let i;
        while((i=this.buf.indexOf('\n'))>=0){const line=this.buf.slice(0,i).replace(/\r$/,'');this.buf=this.buf.slice(i+1);this._line(line);}}}
      catch(e){err=e;}
      /* A deliberate disconnect() cancels the reader and lands here too; that
         is not a fault. Anything else is the link dying under us and must be
         said out loud -- a silent null here is how "connected, no data" and a
         vanished Disconnect button happened on Linux. */
      const lost=!this.closing;
      this.port=null;
      if(lost){
        const why='USB link lost'+(err&&err.message?': '+err.message:' (stream ended)');
        this.lastError=why;console.error('[c3-camlink] '+why,err);
        this._failPending(why);
        if(this.onlost)this.onlost(why);
      }
    }
    _line(line){
      if(line.startsWith('@api ')){
        const m=line.match(/^@api (\S+) (ok|err) ?([\s\S]*)$/);
        if(m){const p=this.pending[m[1]];if(p){delete this.pending[m[1]];
          if(m[2]==='ok')p.res(m[3]||'{}');else p.rej(new Error(m[3]||'error'));}}
      }
    }
    api(verb,rest){
      const id=String(++this.n);
      const line='api '+id+' '+verb+(rest?' '+rest:'')+'\n';
      if(!this.port||!this.writer) return Promise.reject(new Error(this.lastError||'not connected'));
      return new Promise((res,rej)=>{this.pending[id]={res,rej};
        this.writer.write(this.enc.encode(line)).catch(e=>{if(this.pending[id]){delete this.pending[id];rej(new Error('write failed: '+(e&&e.message||e)));}});
        setTimeout(()=>{if(this.pending[id]){delete this.pending[id];rej(new Error('timeout: no reply from the module in 4s'));}},4000);});
    }
  }
  const board=new Board();window._board=board;

  const real=window.fetch.bind(window);
  function resp(ok,text){return{ok:ok,status:ok?200:400,json:async()=>JSON.parse(text||'{}'),text:async()=>text};}
  window.fetch=async(url,opts)=>{
    opts=opts||{};
    if(typeof url==='string' && url.indexOf('/api/')===0){
      if(!board.connected) throw new Error('not connected');
      const ep=url.split('?')[0].slice(5);
      const body=opts.body?opts.body.toString():'';
      /* Every /api/ endpoint the shared page can reach must be listed here, or
         it fails over USB while working over Wi-Fi -- which is how profile
         switching was silently dead on the cable for several versions. WITH_BODY
         names the ones that carry a form body; the rest are bare verbs, and
         sending a body to those would put junk on the wire. */
      const map={state:'state',scan:'scan',config:'config',osd:'osd',bind:'bind',
                 profile:'profile',forget:'forget',repair:'repair',exit:'exit'};
      const WITH_BODY={config:1,osd:1,bind:1,profile:1};
      if(ep==='update') return resp(false,'firmware update is not available over USB — use esptool');
      const verb=map[ep];
      if(!verb) throw new Error('unknown endpoint '+ep);
      try{const txt=await board.api(verb, WITH_BODY[ep]?body:'');return resp(true,txt);}
      catch(e){return resp(false,e.message||'error');}
    }
    return real(url,opts);
  };

  /* Called by the shared page each time it has fresh state. The banner is the
     only version readout on screen while the flash panel is open, so it has to
     be the board's version rather than the page's. */
  window.camlinkOnState=s=>{
    const ct=document.getElementById('usbConnTxt');
    if(ct)ct.textContent='c3-camlink '+s.fw;
  };

  function boot(){
    const gate=document.getElementById('usbGate');
    const btn=document.getElementById('usbConnect');
    if(!('serial'in navigator)){document.getElementById('usbNoSerial').style.display='';btn.disabled=true;}
    async function ensureUsbcfg(){
      let m=null;
      try{m=JSON.parse(await board.api('mode'));}
      catch(e){
        /* Only a firmware that answers "unknown-verb" is old-but-alive; a
           timeout or a lost link is a dead connection and must not pass. */
        if(/unknown-verb/.test(e.message))return true;
        throw new Error('module did not answer: '+e.message);
      }
      if(m.usbcfg)return true;
      btn.textContent='Entering setup…';
      try{await board.api('usbcfg');}catch(e){}   /* reboots; the reply may be cut off */
      return await board.reopen();
    }
    board.onlost=why=>{
      document.getElementById('usbConn').style.display='none';
      const nd=document.getElementById('usbNoData');nd.textContent=why.replace(/\.$/,'')+'. Unplug and replug the module, then Connect again.';nd.style.display='';
      gate.style.display='';btn.disabled=false;btn.textContent='Connect over USB';
    };
    btn.onclick=async()=>{
      btn.disabled=true;btn.textContent='Connecting…';document.getElementById('usbNoData').style.display='none';
      try{
        await board.connect();
        if(!await ensureUsbcfg()) throw new Error('module did not come back in setup mode');
        gate.style.display='none';document.getElementById('usbConn').style.display='block';
      }catch(e){btn.disabled=false;btn.textContent='Connect over USB';alert('Connect failed: '+e.message);}
    };
    const doneBtn=document.getElementById('done');
    if(doneBtn){
      doneBtn.textContent='Test mode (normal boot)';
      doneBtn.title='Reboot the board into normal flight mode so the OSD and camera run over USB';
      doneBtn.onclick=async()=>{
        if(!confirm('Reboot the board into normal flight mode for one boot?\n\nThe camera link and the OSD will run, drawn to your flight controller, so you can watch warnings in the goggles. This works over USB (which powers your FC). Reconnect here afterwards to return to setup.')) return;
        try{await board.api('normal');}catch(e){}
        try{await board.disconnect();}catch(e){}
        alert('Board is rebooting into normal mode. Watch your OSD / goggles.\n\nReload this page and Connect to return to setup.');
        window.location.reload();
      };
    }
    // Keep the connection badge honest even if a reconnect left it out of sync.
    /* 'block', not '': the stylesheet hides #usbConn, so clearing the inline
       style falls back to display:none and the badge never appears at all. */
    setInterval(()=>{const c=document.getElementById('usbConn');if(c)c.style.display=(window._board&&window._board.connected)?'block':'none';},800);
    document.getElementById('usbDisc').onclick=async()=>{
      await board.disconnect();
      document.getElementById('usbConn').style.display='none';
      document.getElementById('usbNoData').style.display='none';
      gate.style.display='';btn.disabled=false;btn.textContent='Connect over USB';
      window.location.reload();   /* clean slate for the next connect */
    };
    // Data watchdog: if nothing arrives shortly after connecting, say so.
    let sawData=false;
    const of=window.fetch;
    window.fetch=async(u,o)=>{const r=await of(u,o);if(typeof u==='string'&&u.indexOf('/api/state')===0&&r.ok){sawData=true;document.getElementById('usbNoData').style.display='none';}return r;};
    setTimeout(()=>{if(board.connected&&!sawData)document.getElementById('usbNoData').style.display='';},4000);
    // Firmware-over-USB is not supported by this tool; hide that card. It must
    // stay in the DOM: fill() writes to #fwVer inside it, and removing it made
    // fill() throw before it populated anything else.
    const fw=[...document.querySelectorAll('section')].find(s=>{const h=s.querySelector('h2');return h&&h.textContent.trim()==='Firmware';});
    if(fw)fw.style.display='none';
  }
  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',boot);else boot();
})();
