import { ESPLoader, Transport } from "https://cdn.jsdelivr.net/npm/esptool-js@0.5.4/bundle.js";
const $=s=>document.querySelector(s);
const flog=t=>{const e=$('#flashLog');if(e){e.textContent=(e.textContent+t+'\n').split('\n').slice(-60).join('\n');e.scrollTop=9e9;}};
const setBar=f=>{const w=$('#flashBarWrap'),b=$('#flashBar');if(w){w.style.display='';b.style.width=Math.round(f*100)+'%';}};
async function binStr(ab){const u=new Uint8Array(ab);let s='';const C=0x8000;for(let i=0;i<u.length;i+=C)s+=String.fromCharCode.apply(null,u.subarray(i,i+C));return s;}
async function fetchBin(name){flog('Fetching '+name+'…');const r=await fetch('firmware/'+name,{cache:'no-store'});if(!r.ok)throw new Error(name+' not found');return binStr(await r.arrayBuffer());}
async function fromFile(f){return binStr(await f.arrayBuffer());}
async function flash(getData, address, label){
  const btns=[$('#flashUpdate'),$('#flashFull')].filter(Boolean);btns.forEach(b=>b.disabled=true);setBar(0);
  let transport;
  try{
    flog('Select the c3-camlink serial port in the chooser…');
    const port=await navigator.serial.requestPort();
    transport=new Transport(port,false);
    const loader=new ESPLoader({transport,baudrate:115200,romBaudrate:115200,
      terminal:{clean(){},writeLine(d){flog(d);},write(d){}}});
    const chip=await loader.main();
    flog('Connected: '+chip);
    const data=await getData();
    flog(label+': writing '+data.length+' bytes at 0x'+address.toString(16)+' — do not unplug…');
    await loader.writeFlash({fileArray:[{data,address}],flashSize:'keep',eraseAll:false,compress:true,
      reportProgress:(i,w,t)=>setBar(w/t)});
    setBar(1);
    flog('Written. Restarting the board…');
    await loader.after();
    flog('Done. The board is booting into setup — click “Connect over USB”.');
  }catch(e){flog('Flash failed: '+(e&&e.message?e.message:e));}
  finally{try{await transport?.disconnect();}catch(e){}btns.forEach(b=>b.disabled=false);}
}
$('#flashToggle').onclick=()=>{const p=$('#flashPanel');p.style.display=p.style.display==='none'?'':'none';};
// Update: write only the app slot (0x20000), leaving NVS -- and your settings -- untouched.
$('#flashUpdate').onclick=()=>flash(()=>fetchBin('c3-camlink-app.bin'), 0x20000, 'Update (keeps settings)');
// Full install: the merged image at 0x0, which resets settings. For a blank board or a clean slate.
$('#flashFull').onclick=()=>{ if(confirm('A full install resets saved settings. Use this for a blank board or a clean slate. Continue?')) flash(()=>fetchBin('c3-camlink-full.bin'), 0, 'Full install'); };
$('#flashFile').onchange=e=>{if(e.target.files[0])flash(()=>fromFile(e.target.files[0]), 0, 'Custom .bin');};
