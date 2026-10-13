#pragma once
#include <pgmspace.h>

static const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ParaVario</title>
<style>
:root{--bg:#f2f3f5;--card:#fff;--fg:#15171b;--mut:#6b7280;--acc:#0b6bcb;--bad:#c62828;--ok:#1b8a4b;--warn:#b26a00;--bd:#dfe2e6}
@media(prefers-color-scheme:dark){:root{--bg:#0e1013;--card:#191c21;--fg:#e9ebef;--mut:#9aa1ab;--acc:#5aa7ff;--bad:#ff6b6b;--ok:#4cc38a;--warn:#f0b44c;--bd:#2a2f37}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;padding:0 12px 96px}
header{display:flex;align-items:center;justify-content:space-between;padding:14px 2px 10px}
h1{font-size:20px;margin:0}
h2{font-size:15px;margin:0 0 10px}
#conn{font-size:13px;color:var(--mut)}
#conn.ok{color:var(--ok)}#conn.bad{color:var(--bad)}
.tiles{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
.tile{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:10px 12px;min-width:0}
.tile .k{font-size:12px;color:var(--mut);text-transform:uppercase;letter-spacing:.04em}
.tile .v{font-size:22px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tile .s{font-size:12px;color:var(--mut);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tile.big{grid-column:span 2}.tile.big .v{font-size:40px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:12px;margin-top:10px}
details.card{padding:0}
summary{padding:12px;font-weight:600;cursor:pointer}
details .row{margin:0 12px}
details[open]{padding-bottom:6px}
.btns{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
button{font:inherit;border:1px solid var(--bd);background:var(--bg);color:var(--fg);border-radius:10px;padding:11px 8px;cursor:pointer}
button.pri{background:var(--acc);border-color:var(--acc);color:#fff}
button.dng{color:var(--bad)}
button.on{outline:2px solid var(--acc)}
.row{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:9px 0;border-top:1px solid var(--bd)}
.row:first-of-type{border-top:0}
.row span{flex:1;min-width:0}
.row small{color:var(--mut);margin-left:4px}
.row input[type=number],.row input[type=text],.row input[type=password],.row select{width:9.5em;max-width:50vw;font:inherit;padding:7px 8px;border:1px solid var(--bd);border-radius:8px;background:var(--bg);color:var(--fg)}
.row input[type=checkbox]{width:22px;height:22px}
.mode-emergency{color:var(--bad)}.mode-approach{color:var(--warn)}
.log{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-top:1px solid var(--bd);gap:8px}
.log a{color:var(--acc);word-break:break-all}
.mut{color:var(--mut);font-size:13px}
#savebar{position:fixed;left:0;right:0;bottom:0;padding:10px 12px;background:var(--card);border-top:1px solid var(--bd);display:none;gap:8px}
#savebar.show{display:flex}#savebar button{flex:1}
#toast{position:fixed;left:50%;top:12px;transform:translateX(-50%);background:var(--fg);color:var(--bg);padding:8px 14px;border-radius:20px;font-size:14px;opacity:0;transition:opacity .2s;pointer-events:none;max-width:90vw}
#toast.show{opacity:.92}
.ready{display:flex;justify-content:space-between;align-items:center;gap:10px;border-radius:12px;padding:12px 14px;margin-bottom:8px;font-size:16px;color:#fff}
.ready.ok{background:var(--ok)}.ready.wait{background:var(--warn)}.ready.hide{display:none}
.ready span{font-size:14px;opacity:.95;text-align:right}
.dbgbar{display:flex;gap:8px;margin:0 12px 8px}.dbgbar button{flex:1;padding:8px}
#dbglog{margin:0 12px 6px;max-height:55vh;overflow:auto;font:11px/1.35 ui-monospace,Menlo,Consolas,monospace;white-space:pre-wrap;word-break:break-all;background:var(--bg);border:1px solid var(--bd);border-radius:8px;padding:8px}
#dbglog .E{color:var(--bad)}#dbglog .W{color:var(--warn)}#dbglog .D{color:var(--mut)}
.badge{font-size:11px;background:var(--warn);color:#000;border-radius:6px;padding:1px 6px;margin-left:6px;vertical-align:middle}
</style></head><body>
<header><h1>ParaVario</h1><span id="conn">connecting…</span></header>
<div id="ready" class="ready wait"><b id="ready-t">Checking…</b><span id="ready-s"></span></div>
<section class="tiles">
<div class="tile big"><div class="k">Height AGL</div><div class="v" id="t-agl">–</div><div class="s" id="t-msl"></div></div>
<div class="tile"><div class="k">Vario</div><div class="v" id="t-vario">–</div><div class="s" id="t-mute"></div></div>
<div class="tile"><div class="k">Ground speed</div><div class="v" id="t-speed">–</div><div class="s" id="t-track"></div></div>
<div class="tile"><div class="k">Flight</div><div class="v" id="t-flight">–</div><div class="s" id="t-ftime"></div></div>
<div class="tile"><div class="k">Mode</div><div class="v" id="t-mode">–</div><div class="s" id="t-modes"></div></div>
<div class="tile"><div class="k">Landing zone</div><div class="v" id="t-lz">–</div><div class="s" id="t-lzs"></div></div>
<div class="tile"><div class="k">Wind</div><div class="v" id="t-wind">–</div><div class="s" id="t-winds"></div></div>
<div class="tile"><div class="k">GPS</div><div class="v" id="t-gps">–</div><div class="s" id="t-gpss"></div></div>
<div class="tile"><div class="k">Battery</div><div class="v" id="t-batt">–</div><div class="s" id="t-batts"></div></div>
</section>
<section class="card"><h2>Actions</h2><div class="btns">
<button data-a="approach" id="b-approach">Landing approach</button>
<button data-a="emergency" id="b-emergency" class="dng">Emergency glide</button>
<button data-a="mute" id="b-mute">Mute vario</button>
<button data-a="test_voice">Test voice</button>
<button data-a="lz_here">Set LZ here</button>
<button data-a="lz_takeoff">LZ = takeoff</button>
</div></section>
<section id="settings"></section>
<section class="card"><h2>Flight logs</h2><div id="logs" class="mut">Loading…</div></section>
<details class="card" id="dbg"><summary>Debug log <small class="mut" id="dbginfo"></small></summary>
<div class="dbgbar"><button id="dbgpause">Pause</button><button id="dbgclear">Clear</button><button id="dbgsave">Save as file</button></div>
<pre id="dbglog"></pre></details>
<section class="card"><h2>System</h2><div class="btns">
<button data-a="restart">Restart</button>
<button data-a="defaults" class="dng">Factory defaults</button>
</div><p class="mut" id="sys"></p></section>
<div id="savebar"><button id="revert">Discard</button><button class="pri" id="save">Save settings</button></div>
<div id="toast"></div>
<script>
const $=s=>document.querySelector(s);
const FT=3.28084;
let schema=[],cfg={},saved={},openGroups=new Set();
const rnd=(v,d)=>{const m=Math.pow(10,d);return Math.round(v*m)/m};
function conv(u){
  switch(u){
    case'height':return cfg.heightUnit==1?{f:1,s:'m',d:0}:{f:FT,s:'ft',d:0};
    case'dist':return cfg.distUnit==1?{f:FT,s:'ft',d:0}:{f:1,s:'m',d:0};
    case'speed':return[{f:3.6,s:'km/h',d:1},{f:2.23694,s:'mph',d:1},{f:1.94384,s:'kt',d:1}][cfg.speedUnit||0];
    default:return{f:1,s:u||'',d:2};
  }
}
const fH=m=>{const c=conv('height');return Math.round(m*c.f)+' '+c.s};
const fS=ms=>{const c=conv('speed');return Math.round(ms*c.f)+' '+c.s};
function fD(m){
  if(cfg.distUnit==1){const ft=m*FT;return ft<2640?Math.round(ft)+' ft':rnd(ft/5280,1)+' mi'}
  return m<1000?Math.round(m)+' m':rnd(m/1000,2)+' km';
}
function fT(s){const h=Math.floor(s/3600),m=Math.floor(s%3600/60),x=s%60;return(h?h+':'+String(m).padStart(2,'0'):m)+':'+String(x).padStart(2,'0')}
function toast(t){const e=$('#toast');e.textContent=t;e.classList.add('show');clearTimeout(e._t);e._t=setTimeout(()=>e.classList.remove('show'),2200)}
function txt(id,v){const e=document.getElementById(id);if(e.textContent!==v)e.textContent=v}

async function poll(){
  try{
    const r=await fetch('/api/status',{cache:'no-store'});const s=await r.json();
    $('#conn').textContent='live';$('#conn').className='ok';
    txt('t-agl',fH(s.agl));txt('t-msl',fH(s.altMsl)+' MSL');
    txt('t-vario',(s.vario>=0?'+':'')+s.vario.toFixed(1)+' m/s');txt('t-mute',s.muted?'muted':'');
    txt('t-speed',s.gps.fix?fS(s.gps.speed):'–');txt('t-track',s.gps.fix?'track '+Math.round(s.gps.track)+'°':'');
    txt('t-flight',s.flying?'Flying':'Ground');txt('t-ftime',s.flightTime?fT(s.flightTime)+(s.logging?' · logging':''):'');
    const m=$('#t-mode');m.className='v mode-'+s.mode;
    txt('t-mode',s.mode=='none'?'Normal':s.mode=='approach'?'Approach':'Emergency');
    txt('t-modes',s.mode=='approach'?s.approach:s.mode=='emergency'&&s.range!=null?'range '+fD(s.range):'');
    $('#b-approach').classList.toggle('on',s.mode=='approach');$('#b-emergency').classList.toggle('on',s.mode=='emergency');
    $('#b-mute').textContent=s.muted?'Unmute vario':'Mute vario';
    txt('t-lz',!s.lz.valid?'not set':s.lz.dist!=null?fD(s.lz.dist):'set');
    txt('t-lzs',s.lz.valid?(s.lz.brg!=null?Math.round(s.lz.brg)+'° · ':'')+fH(s.lz.elev)+' MSL':'');
    txt('t-wind',s.wind.valid?Math.round(s.wind.from)+'°':'unknown');txt('t-winds',s.wind.valid?fS(s.wind.speed)+' · '+s.wind.src:'');
    txt('t-gps',!s.gps.link?'no data':s.gps.sats+' sats');
    txt('t-gpss',(s.gps.fix?'Fix':'Searching')+' · HDOP '+(s.gps.hdop<99?s.gps.hdop.toFixed(1):'–'));
    const rd=$('#ready');rd.className='ready '+(s.flying?'hide':s.ready?'ok':'wait');
    txt('ready-t',s.ready?'Ready to fly':s.notReady);
    txt('ready-s',s.gps.sats+' / '+s.minSats+' satellites');
    txt('t-batt',s.batt.v<2.5?'USB':s.batt.pct+'%');txt('t-batts',s.batt.v.toFixed(2)+' V');
    txt('sys','Uptime '+fT(s.uptime)+' · heap '+Math.round(s.heap/1024)+' kB · BLE clients '+s.ble+' · baro '+(s.baro.ok?(s.baro.pressure/100).toFixed(1)+' hPa':'missing'));
  }catch(e){$('#conn').textContent='offline';$('#conn').className='bad'}
}

function setVal(f,v){cfg[f.k]=v;$('#savebar').classList.toggle('show',JSON.stringify(cfg)!==JSON.stringify(saved))}
function field(f){
  const row=document.createElement('label');row.className='row';
  const c=conv(f.u);const sp=document.createElement('span');sp.textContent=f.l;
  if(c.s&&f.t!=='bool'&&f.t!=='enum'&&f.t!=='str'){const sm=document.createElement('small');sm.textContent='('+c.s+')';sp.appendChild(sm)}
  if(f.r){const sm=document.createElement('small');sm.textContent='· restart';sp.appendChild(sm)}
  let i;
  if(f.t==='bool'){i=document.createElement('input');i.type='checkbox';i.checked=!!cfg[f.k];i.onchange=()=>setVal(f,i.checked)}
  else if(f.t==='enum'){i=document.createElement('select');f.o.split('|').forEach((o,n)=>i.add(new Option(o,n)));i.value=cfg[f.k];
    i.onchange=()=>{setVal(f,+i.value);if(/Unit$/.test(f.k))render()}}
  else if(f.t==='str'){i=document.createElement('input');i.type=f.k==='wifiPass'?'password':'text';i.maxLength=f.n;i.value=cfg[f.k];i.onchange=()=>setVal(f,i.value)}
  else if(f.t==='list'){i=document.createElement('input');i.type='text';i.inputMode='decimal';
    i.value=String(cfg[f.k]).split(',').filter(x=>x).map(v=>Math.round(v*c.f)).join(', ');
    i.onchange=()=>setVal(f,i.value.split(/[ ,]+/).map(parseFloat).filter(v=>!isNaN(v)&&v>0).map(v=>(v/c.f).toFixed(2)).join(','))}
  else{i=document.createElement('input');i.type='number';i.step='any';i.inputMode='decimal';
    const d=f.t==='double'?7:f.t==='int'?0:c.d;i.value=rnd(cfg[f.k]*c.f,d);
    i.onchange=()=>{const v=parseFloat(i.value);if(!isNaN(v))setVal(f,v/c.f)}}
  row.append(sp,i);return row;
}
function render(){
  const root=$('#settings');root.innerHTML='';const groups={};
  for(const f of schema)(groups[f.g]=groups[f.g]||[]).push(f);
  for(const g in groups){
    const d=document.createElement('details');d.className='card';d.open=openGroups.has(g);
    d.ontoggle=()=>d.open?openGroups.add(g):openGroups.delete(g);
    const s=document.createElement('summary');s.textContent=g;d.appendChild(s);
    groups[g].forEach(f=>d.appendChild(field(f)));root.appendChild(d);
  }
}
async function loadSettings(){
  [schema,cfg]=await Promise.all([fetch('/api/schema').then(r=>r.json()),fetch('/api/settings').then(r=>r.json())]);
  saved=JSON.parse(JSON.stringify(cfg));render();
}
$('#save').onclick=async()=>{
  try{
    const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)});
    const j=await r.json();if(!r.ok)throw j.error;
    cfg=j.settings;saved=JSON.parse(JSON.stringify(cfg));render();$('#savebar').classList.remove('show');
    if(j.restart&&confirm('Saved. Some changes need a restart. Restart now?'))act('restart');else toast('Settings saved');
  }catch(e){toast('Save failed: '+e)}
};
$('#revert').onclick=()=>{cfg=JSON.parse(JSON.stringify(saved));render();$('#savebar').classList.remove('show')};

async function act(a){
  if(a==='defaults'&&!confirm('Reset all settings to defaults?'))return;
  try{const r=await fetch('/api/action?do='+a,{method:'POST'});const j=await r.json();if(!r.ok)throw j.error;
    toast(a==='restart'||a==='defaults'?'Restarting…':'Done');
    if(a.startsWith('lz_'))loadSettings();
  }catch(e){toast('Failed: '+e)}
}
document.querySelectorAll('button[data-a]').forEach(b=>b.onclick=()=>act(b.dataset.a));

async function loadLogs(){
  try{
    const j=await fetch('/api/logs').then(r=>r.json());const el=$('#logs');el.innerHTML='';
    j.files.sort((a,b)=>b.n.localeCompare(a.n));
    if(!j.files.length)el.textContent='No flights recorded yet.';
    for(const f of j.files){
      const row=document.createElement('div');row.className='log';
      const a=document.createElement('a');a.href='/api/log?name='+encodeURIComponent(f.n);a.download=f.n;a.textContent=f.n;
      const sz=document.createElement('span');sz.className='mut';sz.textContent=Math.round(f.s/1024)+' kB';
      const del=document.createElement('button');del.textContent='Delete';del.className='dng';
      del.onclick=async()=>{if(!confirm('Delete '+f.n+'?'))return;await fetch('/api/log?name='+encodeURIComponent(f.n),{method:'DELETE'});loadLogs()};
      row.append(a,sz,del);el.appendChild(row);
    }
    const p=document.createElement('p');p.className='mut';p.textContent=Math.round(j.free/1024)+' kB free of '+Math.round(j.total/1024)+' kB';el.appendChild(p);
  }catch(e){$('#logs').textContent='Could not load logs'}
}

let dbgNext=0,dbgPaused=false,dbgText=[],dbgBusy=false;
const DBG_MAX=1500;
async function pollDbg(){
  if(dbgBusy||dbgPaused||!$('#dbg').open)return;dbgBusy=true;
  try{
    const t=await fetch('/api/debuglog?since='+dbgNext,{cache:'no-store'}).then(r=>r.text());
    const lines=t.split('\n');const h=/#next=(\d+) dropped=(\d+) debug=(\d)/.exec(lines.shift());
    if(!h)throw 0;
    dbgNext=+h[1];
    const el=$('#dbglog');const atEnd=el.scrollTop+el.clientHeight>=el.scrollHeight-20;
    if(+h[2])addDbg('… '+h[2]+' lines missed');
    lines.filter(l=>l).forEach(addDbg);
    while(el.childNodes.length>DBG_MAX)el.removeChild(el.firstChild);
    if(dbgText.length>DBG_MAX*2)dbgText=dbgText.slice(-DBG_MAX);
    if(atEnd)el.scrollTop=el.scrollHeight;
    txt('dbginfo',(h[3]=='1'?'debug build':'release build')+' · '+dbgText.length+' lines');
  }catch(e){}finally{dbgBusy=false}
}
function addDbg(l){
  dbgText.push(l);const d=document.createElement('div');const m=/^\[[^\]]*\] ([EWID]) /.exec(l);
  if(m)d.className=m[1];d.textContent=l;$('#dbglog').appendChild(d);
}
$('#dbgpause').onclick=()=>{dbgPaused=!dbgPaused;$('#dbgpause').textContent=dbgPaused?'Resume':'Pause'};
$('#dbgclear').onclick=()=>{$('#dbglog').innerHTML='';dbgText=[]};
$('#dbgsave').onclick=()=>{const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([dbgText.join('\n')+'\n'],{type:'text/plain'}));
  a.download='paravario-debug-'+new Date().toISOString().replace(/[:.]/g,'-')+'.txt';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000)};
$('#dbg').ontoggle=()=>{if($('#dbg').open)pollDbg()};
async function boot(){
  await loadSettings().catch(()=>{});
  poll();setInterval(poll,1000);setInterval(pollDbg,1000);loadLogs();
  try{const s=await fetch('/api/status').then(r=>r.json());
    if(s.debug){$('#dbg').open=true;$('h1').insertAdjacentHTML('beforeend','<span class="badge">DEBUG</span>');pollDbg()}}catch(e){}
}
boot();
</script></body></html>)HTML";
