#pragma once
#include <pgmspace.h>

static const char kTestPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ParaVario Hardware Test</title>
<style>
:root{--bg:#f2f3f5;--card:#fff;--fg:#15171b;--mut:#6b7280;--acc:#0b6bcb;--bad:#c62828;--ok:#1b8a4b;--warn:#b26a00;--bd:#dfe2e6}
@media(prefers-color-scheme:dark){:root{--bg:#0e1013;--card:#191c21;--fg:#e9ebef;--mut:#9aa1ab;--acc:#5aa7ff;--bad:#ff6b6b;--ok:#4cc38a;--warn:#f0b44c;--bd:#2a2f37}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;padding:0 12px 40px}
header{display:flex;align-items:center;justify-content:space-between;padding:14px 2px 6px}
h1{font-size:19px;margin:0}h2{font-size:15px;margin:0}h3{font-size:13px;margin:12px 0 6px;color:var(--mut);text-transform:uppercase;letter-spacing:.04em}
#conn{font-size:13px;color:var(--mut)}#conn.ok{color:var(--ok)}#conn.bad{color:var(--bad)}
#summary{display:flex;gap:8px;margin:6px 0 4px;flex-wrap:wrap}
.pill{border-radius:20px;padding:4px 12px;font-size:14px;font-weight:600;color:#fff}
.pill.ok{background:var(--ok)}.pill.warn{background:var(--warn)}.pill.fail{background:var(--bad)}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:12px;margin-top:10px}
.head{display:flex;align-items:center;gap:8px}
.dot{width:12px;height:12px;border-radius:50%;background:var(--mut);flex:none}
.dot.ok{background:var(--ok)}.dot.warn{background:var(--warn)}.dot.fail{background:var(--bad)}
.msg{color:var(--mut);font-size:13px;margin:4px 0 0 20px}
.kv{display:grid;grid-template-columns:auto 1fr;gap:2px 12px;font-size:13px;margin:8px 0 0 20px}
.kv b{font-weight:500;color:var(--mut)}.kv span{font-variant-numeric:tabular-nums;word-break:break-all}
.btns{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
button{font:inherit;border:1px solid var(--bd);background:var(--bg);color:var(--fg);border-radius:10px;padding:10px 8px;cursor:pointer}
button.pri{background:var(--acc);border-color:var(--acc);color:#fff}button.stop{color:var(--bad)}
.row{display:flex;gap:8px;align-items:center;margin-top:8px}
.row>*{min-width:0}
input,select{font:inherit;padding:8px;border:1px solid var(--bd);border-radius:8px;background:var(--bg);color:var(--fg)}
input[type=range]{flex:1;padding:0}
input[type=number]{width:6em}
select{flex:1}
.big{font-size:22px;font-weight:600;min-width:5.5em;text-align:right;font-variant-numeric:tabular-nums}
pre{font:11px/1.35 ui-monospace,Menlo,Consolas,monospace;white-space:pre-wrap;word-break:break-all;background:var(--bg);border:1px solid var(--bd);border-radius:8px;padding:8px;margin:8px 0 0;max-height:12em;overflow:auto}
.mut{color:var(--mut);font-size:13px}
label.chk{display:flex;gap:8px;align-items:center;margin-top:10px}
#toast{position:fixed;left:50%;top:12px;transform:translateX(-50%);background:var(--fg);color:var(--bg);padding:8px 14px;border-radius:20px;font-size:14px;opacity:0;transition:opacity .2s;pointer-events:none}
#toast.show{opacity:.92}
</style></head><body>
<header><h1>ParaVario hardware test</h1><span id="conn">connecting…</span></header>
<div id="summary"></div>
<div id="checks"></div>

<section class="card"><div class="head"><span class="dot" id="aud-dot"></span><h2>Audio tests</h2></div>
<div class="msg" id="aud-msg"></div>
<h3>Volume</h3>
<div class="row"><input type="range" id="vol" min="0" max="100" step="5"><span class="big" id="volv"></span></div>
<h3>Tones</h3>
<div class="btns">
<button data-q="do=sweep&a=400&b=1600&ms=600">Chirp</button>
<button data-q="do=tone&a=1000&ms=2000">1 kHz, 2 s</button>
<button data-q="do=sweep&a=150&b=6000&ms=6000">Sweep 150 Hz–6 kHz</button>
<button data-q="do=tone&a=1000&ms=2000&wave=square">1 kHz square (loud)</button>
<button data-q="do=tone&a=300&ms=1500">Low 300 Hz</button>
<button data-q="do=tone&a=3000&ms=1500">High 3 kHz</button>
</div>
<div class="row"><input type="number" id="fq" value="700" min="50" max="7000"> Hz
<input type="number" id="fms" value="1000" min="20" max="10000"> ms
<select id="fw"><option value="sine">Sine</option><option value="square">Square</option></select>
<button class="pri" id="fplay">Play</button></div>
<h3>Vario simulation</h3>
<div class="row"><input type="range" id="vs" min="-5" max="8" step="0.1" value="1.5"><span class="big" id="vsv"></span></div>
<div class="btns" style="margin-top:8px">
<button data-v="0.5">+0.5 m/s</button><button data-v="2">+2 m/s</button>
<button data-v="5">+5 m/s</button><button data-v="-3">−3 m/s (sink)</button>
</div>
<h3>Voice clips</h3>
<div class="row"><select id="clips"><option>loading…</option></select><button class="pri" id="cplay">Play</button></div>
<div class="btns" style="margin-top:8px"><button id="callall">Play all clips</button><button id="cphrase">Sample phrase</button></div>
<div class="mut" id="cstat"></div>
<div class="btns" style="margin-top:12px"><button class="stop" id="stop" style="grid-column:span 2">Stop audio</button></div>
<label class="chk"><input type="checkbox" id="beep"> Beep when the button is pressed</label>
</section>

<section class="card"><h2>GPS raw NMEA</h2><pre id="raw">waiting…</pre></section>
<section class="card"><h2>Tools</h2><div class="btns" style="margin-top:8px">
<button data-act="i2c">Rescan I2C</button><button data-act="restart">Restart</button></div>
<p class="mut" id="sys"></p></section>
<div id="toast"></div>
<script>
const $=s=>document.querySelector(s);
const NAMES={storage:'Storage & voice clips',i2c:'I2C bus',baro:'Barometer (DPS310)',gps:'GPS (M10)',battery:'Battery',button:'Push button',ble:'Bluetooth',wifi:'Wi-Fi'};
const f=(v,d)=>v==null?'–':(+v).toFixed(d);
function toast(t){const e=$('#toast');e.textContent=t;e.classList.add('show');clearTimeout(e._t);e._t=setTimeout(()=>e.classList.remove('show'),1800)}
function details(k,s){
  const o=s[k];
  switch(k){
    case'storage':return o.mounted?{'Voice clips':o.clips,'Used':Math.round(o.used/1024)+' / '+Math.round(o.total/1024)+' kB'}:{};
    case'i2c':return{'Found':o.found.join(', ')||'none'};
    case'baro':return{'Pressure':f(o.pressure/100,2)+' hPa','Temperature':f(o.temp,1)+' °C','Altitude (std)':f(o.alt,1)+' m','Vario':(o.vario>=0?'+':'')+f(o.vario,2)+' m/s','Rate':f(o.rate,0)+' /s','Samples':o.samples};
    case'gps':return{'Bytes':o.bytes,'Baud':o.baud+(o.locked?' (locked)':' (searching)'),'NMEA ok / bad':o.passed+' / '+o.failed,'Satellites':o.sats,'HDOP':f(o.hdop,1),'Position':o.fix?f(o.lat,6)+', '+f(o.lon,6):'–','Altitude':o.fix?f(o.alt,1)+' m':'–','Speed':o.fix?f(o.speed,1)+' km/h':'–','UTC':o.utc||'–'};
    case'battery':return{'At pin (D1)':f(o.mv,0)+' mV','Battery':f(o.v,2)+' V'};
    case'button':return{'State':o.down?'PRESSED':'released','Presses':o.presses,'Last event':o.event,'Last hold':o.lastMs+' ms'};
    case'ble':return{'Clients':o.clients,'Test sentences':o.sent};
    case'wifi':return{'Clients':o.clients};
  }
  return{};
}
function card(k){
  let c=document.getElementById('c-'+k);if(c)return c;
  c=document.createElement('section');c.className='card';c.id='c-'+k;
  c.innerHTML='<div class="head"><span class="dot"></span><h2></h2></div><div class="msg"></div><div class="kv"></div>';
  c.querySelector('h2').textContent=NAMES[k];$('#checks').appendChild(c);return c;
}
async function poll(){
  try{
    const s=await fetch('/api/status',{cache:'no-store'}).then(r=>r.json());
    $('#conn').textContent='live';$('#conn').className='ok';
    const counts={ok:0,warn:0,fail:0};
    for(const k in NAMES){
      const o=s[k],c=card(k);counts[o.state]++;
      c.querySelector('.dot').className='dot '+o.state;c.querySelector('.msg').textContent=o.msg;
      const kv=c.querySelector('.kv');kv.innerHTML='';
      for(const[a,b]of Object.entries(details(k,s))){const x=document.createElement('b');x.textContent=a;const y=document.createElement('span');y.textContent=b;kv.append(x,y)}
    }
    counts[s.audio.state]++;
    $('#aud-dot').className='dot '+s.audio.state;$('#aud-msg').textContent=s.audio.msg+' · now: '+s.audio.mode;
    $('#summary').innerHTML=['ok','warn','fail'].filter(x=>counts[x]).map(x=>'<span class="pill '+x+'">'+counts[x]+' '+(x=='ok'?'OK':x=='warn'?'check':'FAIL')+'</span>').join('');
    if(document.activeElement!==$('#vol')){$('#vol').value=s.audio.volume;$('#volv').textContent=s.audio.volume+'%'}
    $('#beep').checked=s.button.beep;
    $('#raw').textContent=s.gps.raw.length?s.gps.raw.join('\n'):'no NMEA received';
    $('#sys').textContent=s.chip+' rev '+s.rev+' · flash '+(s.flash>>20)+' MB · SDK '+s.sdk+' · heap '+Math.round(s.heap/1024)+' kB · uptime '+s.uptime+' s · reset reason '+s.reset;
    lastAudioBusy=s.audio.busy;
  }catch(e){$('#conn').textContent='offline';$('#conn').className='bad'}
}
let lastAudioBusy=false;
async function audio(q){try{await fetch('/api/audio?'+q,{method:'POST'})}catch(e){toast('Failed')}}
document.querySelectorAll('button[data-q]').forEach(b=>b.onclick=()=>audio(b.dataset.q));
$('#fplay').onclick=()=>audio('do=tone&a='+$('#fq').value+'&ms='+$('#fms').value+'&wave='+$('#fw').value);
$('#vol').oninput=()=>$('#volv').textContent=$('#vol').value+'%';
$('#vol').onchange=()=>audio('do=volume&a='+$('#vol').value);
const vsShow=()=>$('#vsv').textContent=(+$('#vs').value>=0?'+':'')+(+$('#vs').value).toFixed(1)+' m/s';
let vsTimer=null;
$('#vs').oninput=()=>{vsShow();if(!vsTimer)vsTimer=setTimeout(()=>{vsTimer=null;audio('do=vario&a='+$('#vs').value)},150)};vsShow();
document.querySelectorAll('button[data-v]').forEach(b=>b.onclick=()=>{$('#vs').value=b.dataset.v;vsShow();audio('do=vario&a='+b.dataset.v)});
let stopAll=false;
$('#stop').onclick=()=>{stopAll=true;audio('do=stop')};
$('#beep').onchange=()=>audio('do=beep&a='+($('#beep').checked?1:0));
$('#cplay').onclick=()=>audio('do=clip&name='+encodeURIComponent($('#clips').value));
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function waitIdle(){for(let i=0;i<100;i++){await sleep(150);const s=await fetch('/api/status').then(r=>r.json());if(!s.audio.busy)return s}}
async function playSeq(list){
  stopAll=false;
  for(let i=0;i<list.length&&!stopAll;i++){
    $('#cstat').textContent='Playing '+(i+1)+'/'+list.length+': '+list[i];
    await audio('do=clip&name='+encodeURIComponent(list[i]));
    const s=await waitIdle();if(s&&s.audio.error){$('#cstat').textContent=list[i]+': '+s.audio.error;return}
  }
  $('#cstat').textContent=stopAll?'Stopped':'Done';
}
$('#callall').onclick=()=>playSeq([...$('#clips').options].map(o=>o.value));
$('#cphrase').onclick=()=>playSeq(['system_ready','n2','hundred','n50','feet','landing_zone','n3','oclock','turn_left_now']);
document.querySelectorAll('button[data-act]').forEach(b=>b.onclick=async()=>{
  if(b.dataset.act==='restart'&&!confirm('Restart the device?'))return;
  await fetch('/api/action?do='+b.dataset.act,{method:'POST'}).catch(()=>{});toast('Done')});
async function loadClips(){
  try{const c=(await fetch('/api/clips').then(r=>r.json())).sort();const s=$('#clips');s.innerHTML='';
    if(!c.length)s.add(new Option('no clips on device',''));c.forEach(n=>s.add(new Option(n,n)));}catch(e){}
}
loadClips();poll();setInterval(poll,700);
</script></body></html>)HTML";
