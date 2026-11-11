#pragma once
#include <pgmspace.h>

#include "Version.h"

// The microphone spectrum (its tab, page and script) is compiled in only with PV_HAS_SPECTRUM, see the
// #if blocks between the string pieces below.
static const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-title" content="ParaVario">
<meta name="apple-mobile-web-app-status-bar-style" content="default">
<link rel="apple-touch-icon" href="/apple-touch-icon.png">
<link rel="icon" href="/favicon.ico">
<title>ParaVario</title>
<style>
:root{--bg:#f2f3f5;--card:#fff;--fg:#15171b;--mut:#6b7280;--acc:#0b6bcb;--bad:#c62828;--ok:#1b8a4b;--warn:#b26a00;--bd:#dfe2e6}
@media(prefers-color-scheme:dark){:root{--bg:#0e1013;--card:#191c21;--fg:#e9ebef;--mut:#9aa1ab;--acc:#5aa7ff;--bad:#ff6b6b;--ok:#4cc38a;--warn:#f0b44c;--bd:#2a2f37}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;padding:0 12px 96px}
footer{text-align:center;color:var(--mut);font-size:12px;padding:20px 0 4px}
h1{font-size:20px;margin:0}
h2{font-size:15px;margin:0 0 10px}
.tabs{display:flex;gap:4px;border-bottom:1px solid var(--bd);margin:0 -12px 10px;padding:0 12px}
.tabs{overflow-x:auto;scrollbar-width:none}
.tabs button{border:0;border-bottom:2px solid transparent;border-radius:0;background:none;color:var(--mut);padding:10px 9px;white-space:nowrap}
.tabs button[aria-selected=true]{border-bottom-color:var(--acc);color:var(--acc)}
div.page[hidden]{display:none}
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
.row span{flex:1 1 40%;min-width:0}
.row small{color:var(--mut);margin-left:4px}
.row input[type=number],.row input[type=text],.row input[type=password],.row select{width:9.5em;max-width:50vw;font:inherit;padding:7px 8px;border:1px solid var(--bd);border-radius:8px;background:var(--bg);color:var(--fg)}
.row input[type=checkbox]{width:22px;height:22px}
.row input:disabled{opacity:.45}
.row b{font-weight:600;text-align:right;flex:0 1 auto;max-width:60%;min-width:0;overflow-wrap:anywhere}
.log{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-top:1px solid var(--bd);gap:8px}
.log a{color:var(--acc);word-break:break-all}
.mut{color:var(--mut);font-size:13px}
#toast{position:fixed;left:50%;top:12px;transform:translateX(-50%);background:var(--fg);color:var(--bg);padding:8px 14px;border-radius:20px;font-size:14px;opacity:0;transition:opacity .2s;pointer-events:none;max-width:90vw}
#toast.show{opacity:.92}
.ready{display:flex;justify-content:space-between;align-items:center;gap:10px;border-radius:12px;padding:12px 14px;margin-bottom:8px;font-size:16px;color:#fff}
.ready.ok{background:var(--ok)}.ready.wait{background:var(--warn)}.ready.hide{display:none}
.ready.appr{background:var(--acc)}.ready.emerg{background:var(--bad)}
.ready span{font-size:14px;opacity:.95;text-align:right}
.dbgbar{display:flex;gap:8px;margin:0 12px 8px}.dbgbar button{flex:1;padding:8px}
#dbglog{margin:0 12px 6px;max-height:55vh;overflow:auto;font:11px/1.35 ui-monospace,Menlo,Consolas,monospace;white-space:pre-wrap;word-break:break-all;background:var(--bg);border:1px solid var(--bd);border-radius:8px;padding:8px}
#dbglog .E{color:var(--bad)}#dbglog .W{color:var(--warn)}#dbglog .D{color:var(--mut)}
.prog{height:8px;background:var(--bg);border:1px solid var(--bd);border-radius:4px;overflow:hidden;margin:10px 0 6px}
#fwbar{height:100%;width:0;background:var(--acc);transition:width .2s}
#fwfile,#fwpass{width:100%;font:inherit;margin-bottom:8px}#fwgo{width:100%}
#fwpass{padding:8px;border:1px solid var(--bd);border-radius:8px;background:var(--bg);color:var(--fg)}
)HTML"
#if PV_HAS_SPECTRUM
R"HTML(#specCv{width:100%;height:280px;display:block}
.specbar{display:flex;gap:8px;align-items:center;margin-top:8px}
.specbar select{padding:8px;border:1px solid var(--bd);border-radius:8px;background:var(--bg);color:var(--fg);font:inherit}
)HTML"
#endif
R"HTML(
body.nogps .needgps,body.nomic .needmic,body.nospec .needspec{display:none}
#helpDoc{font-size:14px;line-height:1.5;overflow-wrap:anywhere}
#helpDoc h1{font-size:20px;margin:14px 0 6px}#helpDoc h2{font-size:17px;margin:18px 0 6px}#helpDoc h3{font-size:15px;margin:14px 0 4px}
#helpDoc h4,#helpDoc h5,#helpDoc h6{font-size:14px;margin:10px 0 4px}
#helpDoc p{margin:6px 0}#helpDoc ul,#helpDoc ol{margin:6px 0;padding-left:22px}#helpDoc li{margin:2px 0}
#helpDoc a{color:var(--acc)}#helpDoc code{font:12.5px ui-monospace,Menlo,Consolas,monospace;background:var(--bg);border-radius:4px;padding:0 3px}
#helpDoc pre{font:11.5px/1.35 ui-monospace,Menlo,Consolas,monospace;background:var(--bg);border:1px solid var(--bd);border-radius:8px;padding:8px;overflow-x:auto;white-space:pre}
#helpDoc blockquote{margin:8px 0;padding:2px 10px;border-left:3px solid var(--acc);color:var(--mut)}
#helpDoc hr{border:0;border-top:1px solid var(--bd);margin:14px 0}
#helpDoc .tw{overflow-x:auto;margin:8px 0}#helpDoc table{border-collapse:collapse;font-size:13px}
#helpDoc th,#helpDoc td{border:1px solid var(--bd);padding:4px 7px;text-align:left;vertical-align:top;overflow-wrap:normal}#helpDoc th{background:var(--bg)}
.rbar{margin-bottom:6px}
summary .help{float:right;font-size:12px;font-weight:400}
details:not([open])>summary .help{display:none}
summary .help a{margin-left:12px;color:var(--acc)}
#helpBody a.doc{display:block;padding:10px 12px;border:1px solid var(--bd);border-radius:10px;margin:8px 0;color:var(--acc);text-decoration:none;font-weight:600}
#helpBody a.doc small{display:block;color:var(--mut);font-size:12px;font-weight:400}
#helpBody .jump{margin:6px 0 2px;color:var(--mut);font-size:13px}#helpBody .jump a{margin-right:12px;color:var(--acc);white-space:nowrap}
.tile.wide{grid-column:span 2}
.btns .wide{grid-column:span 2}
.tile.graph{display:flex;flex-direction:column;padding:6px 8px}
.tile.graph .k{margin-bottom:2px}
#hgCv{width:100%;height:60px;flex:1 1 auto;display:block}
.tiles.more{margin-top:10px}
.badge{font-size:11px;background:var(--warn);color:#000;border-radius:6px;padding:1px 6px;margin-left:6px;vertical-align:middle}
</style></head><body>
<nav class="tabs" role="tablist" aria-label="Main views">
<button type="button" role="tab" aria-selected="true" data-tab="flight">Flight</button>
<button type="button" role="tab" aria-selected="false" data-tab="settings">Settings</button>
)HTML"
#if PV_HAS_SPECTRUM
R"HTML(<button type="button" role="tab" aria-selected="false" data-tab="spectrum" class="needmic needspec">Spectrum</button>
)HTML"
#endif
R"HTML(
<button type="button" role="tab" aria-selected="false" data-tab="system">Logs &amp; system</button>
<button type="button" role="tab" aria-selected="false" data-tab="help">Help</button>
</nav>
<div id="flightPage" class="page" role="tabpanel">
<div id="ready" class="ready wait"><b id="ready-t">Checking…</b><span id="ready-s"></span></div>
<section class="tiles" id="tilesMain">
<div class="tile" id="tile-height"><div class="k">Height AGL</div><div class="v" id="t-agl">–</div><div class="s" id="t-msl"></div></div>
<div class="tile graph" id="tile-hgraph"><div class="k">Height · last 10 min</div><canvas id="hgCv"></canvas></div>
<div class="tile" id="tile-vario"><div class="k">Vario</div><div class="v" id="t-vario">–</div><div class="s" id="t-mute"></div></div>
<div class="tile needgps"><div class="k">Ground speed</div><div class="v" id="t-speed">–</div><div class="s" id="t-track"></div></div>
<div class="tile needgps"><div class="k">Glide ratio</div><div class="v" id="t-glide">–</div><div class="s" id="t-glides"></div></div>
<div class="tile"><div class="k">Flight time</div><div class="v" id="t-flight">–</div><div class="s" id="t-ftime"></div></div>
<div class="tile needgps"><div class="k">Landing zone</div><div class="v" id="t-lz">–</div><div class="s" id="t-lzs"></div></div>
<div class="tile needgps"><div class="k">Wind</div><div class="v" id="t-wind">–</div><div class="s" id="t-winds"></div></div>
</section>
<section class="card"><h2>Actions</h2><div class="btns">
<button data-a="approach" id="b-approach" class="needgps">Landing approach</button>
<button data-a="emergency" id="b-emergency" class="dng needgps">Emergency glide</button>
<button data-a="mute" id="b-mute">Mute vario</button>
<button data-a="test_voice">Test voice</button>
<button data-a="lz_here" class="needgps">Set LZ here</button>
<button data-a="lz_takeoff" class="needgps">LZ = takeoff</button>
<button type="button" id="b-fs" class="wide" hidden>Full screen</button>
</div></section>
<section class="tiles more" id="tilesMore">
<div class="tile needgps"><div class="k">GPS</div><div class="v" id="t-gps">–</div><div class="s" id="t-gpss"></div></div>
<div class="tile"><div class="k">Battery</div><div class="v" id="t-batt">–</div><div class="s" id="t-batts"></div></div>
<div class="tile needmic"><div class="k">Engine RPM</div><div class="v" id="t-rpm">–</div><div class="s" id="t-rpms"></div></div>
<div class="tile needmic"><div class="k">Background noise</div><div class="v" id="t-noise">–</div><div class="s" id="t-noises"></div></div>
</section>
 </div>
<div id="settingsPage" class="page" role="tabpanel" hidden>
<section id="settings"></section>
<p id="saveState" class="mut" role="status" aria-live="polite"></p>
</div>
)HTML"
#if PV_HAS_SPECTRUM
R"HTML(<div id="spectrumPage" class="page" role="tabpanel" hidden>
<section class="card"><h2>Microphone spectrum</h2>
<canvas id="specCv"></canvas>
<div class="specbar"><button id="specpause">Pause</button>
<label class="mut">Floor <select id="specfloor"><option value="100">-100 dBFS</option><option value="80">-80 dBFS</option><option value="60">-60 dBFS</option></select></label></div>
<p class="mut" id="specinfo">Waiting for data…</p></section>
</div>
)HTML"
#endif
R"HTML(
<div id="systemPage" class="page" role="tabpanel" hidden>
<section class="card"><h2>Flight logs</h2><div id="logs" class="mut">Loading…</div></section>
<details class="card" id="dbg"><summary>Logging <small class="mut" id="dbginfo"></small></summary>
<div class="dbgbar"><button id="dbgpause">Pause</button><button id="dbgclear">Clear</button><button id="dbgsave">Save as file</button></div>
<pre id="dbglog"></pre></details>
<section class="card"><h2>Firmware update</h2>
<p class="mut" id="fwver"></p>
<input type="file" id="fwfile" accept=".bin">
<input type="password" id="fwpass" placeholder="Update password" autocomplete="off">
<button class="pri" id="fwgo">Install update</button>
<div class="prog"><div id="fwbar"></div></div><p class="mut" id="fwmsg">Choose firmware.bin (program) or littlefs.bin (voice clips &ndash; erases flight logs) and enter the update password.</p>
</section>
<section class="card"><h2>System</h2><div class="btns">
<button data-a="restart">Restart</button>
<button data-a="defaults" class="dng">Factory defaults</button>
</div><p class="mut" id="sys"></p></section>
</div>
<div id="helpPage" class="page" role="tabpanel" hidden>
<section class="card" id="helpList"><h2>Help and guides</h2>
<div id="helpBody"></div></section>
<section class="card" id="helpReader" hidden>
<div class="rbar"><button type="button" id="helpBack">&larr; Back</button></div>
<article id="helpDoc"></article></section>
</div>
<footer id="hver"></footer>
<div id="toast"></div>
<script>
const $=s=>document.querySelector(s);
const FT=3.28084;
let schema=[],cfg={},openGroups=new Set();
let saveTimer=0,saveRevision=0,saveInFlight=false,saveAgain=false,restartRequired=false;
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

// Tile layout. Hidden tiles (hardware not fitted) do not count.
//  - Main group: if its cells (tiles plus the height graph) come to an odd number, the Height
//    tile takes the full width and the graph moves under it, beside Vario; otherwise the graph
//    sits beside the Height tile.
//  - Lower group: a tile left alone on the last row takes the full width.
const shown=e=>getComputedStyle(e).display!=='none';
function layout(){
  const main=$('#tilesMain'),h=$('#tile-height'),g=$('#tile-hgraph'),v=$('#tile-vario');
  const full=[...main.children].filter(shown).length%2===1;
  h.classList.toggle('big',full);
  (full?v:h).after(g);
  const more=[...$('#tilesMore').children];
  more.forEach(e=>e.classList.remove('wide'));
  const vis=more.filter(shown);
  if(vis.length%2===1)vis[vis.length-1].classList.add('wide');
  drawHeight();
}
let layKey='';
const fR=r=>r>=50?'>50 : 1':r.toFixed(1)+' : 1';

// Help. The guides live in the project's docs folder on GitHub. They are shown inside this page as plain
// content, without the GitHub project around them: the Markdown is fetched from GitHub's raw file server
// (always the latest version) and turned into HTML by the small converter below. It needs the phone to
// have internet; if it cannot be fetched the reader says so and offers the GitHub address.
const HELPRAW=')HTML" PV_HELP_RAW_URL R"HTML(',HELPWEB=')HTML" PV_HELP_WEB_URL R"HTML(';
const DOCS={basic:'User_Guide_(basic).md',adv:'User_Guide_(advanced).md',ref:'Feature_Reference.md'};
const docUrl=(f,base)=>base+encodeURI(f).replace(/\(/g,'%28').replace(/\)/g,'%29');
const webLink=(f,a)=>docUrl(f,HELPWEB)+(a?'#'+a:'');
// Settings group -> its entry in the Feature Reference (b = the part every vario has, a = the part for GPS / microphone)
const GROUP_HELP={'Units':{b:'basic-settings-units-and-audio',a:'adv-settings-units-and-audio'},
  'Audio':{b:'basic-settings-units-and-audio',a:'adv-settings-units-and-audio'},'Engine RPM':{a:'adv-settings-engine-rpm'},
  'Vario':{b:'basic-settings-vario'},'Height calls':{b:'basic-settings-height-calls'},
  'Takeoff & landing':{b:'basic-settings-takeoff-and-landing',a:'adv-settings-takeoff-and-landing'},
  'Glide':{a:'adv-settings-glide'},'Landing zone':{a:'adv-settings-landing-zone'},'Landing approach':{a:'adv-settings-landing-approach'},
  'Emergency':{a:'adv-settings-emergency'},'Wind':{a:'adv-settings-wind'},'GPS':{a:'adv-settings-gps'},
  'Bluetooth':{b:'basic-settings-bluetooth',a:'adv-settings-bluetooth'},'Wi-Fi':{b:'basic-settings-wi-fi'},
  'Battery':{b:'basic-settings-battery'},'Logging':{a:'adv-settings-logging'}};
let hwNow={gps:true,mic:true,spectrum:true};
// A link to a guide section. The address is the GitHub one (for long-press or copy); a tap opens the reader.
function mkLink(t,file,anchor,cls){const a=document.createElement('a');a.href=webLink(file,anchor);a.textContent=t;if(cls)a.className=cls;
  a.onclick=e=>{e.preventDefault();openHelp(file,anchor)};return a}
// A 'Help' link on each settings group that opens its entry in the Feature Reference.
function addGroupHelp(){
  const adv=hwNow.gps||hwNow.mic;
  document.querySelectorAll('#settings details').forEach(d=>{
    const s=d.querySelector('summary'),old=s.querySelector('.help');if(old)old.remove();
    const g=GROUP_HELP[s.firstChild.textContent];if(!g)return;
    const span=document.createElement('span');span.className='help';
    if(g.b)span.appendChild(mkLink('Help',DOCS.ref,g.b));
    if(g.a&&adv)span.appendChild(mkLink(g.b?'More help':'Help',DOCS.ref,g.a));
    if(span.childNodes.length)s.appendChild(span);
  });
}
// The Help tab: the user guide for this vario, the feature reference, and shortcuts into the reference
// for the tiles, buttons and sounds this vario has. It does not mention versions: a given build is
// one or the other, so it simply links to the right material.
function buildHelp(){
  const adv=hwNow.gps||hwNow.mic,el=$('#helpBody');el.innerHTML='';
  const doc=(t,sub,file)=>{const a=mkLink(t,file,'','doc'),s=document.createElement('small');s.textContent=sub;a.appendChild(s);el.appendChild(a)};
  doc('User guide','How to use this vario, from switching on to landing',adv?DOCS.adv:DOCS.basic);
  doc('Feature reference','Every tile, button, sound and setting explained, with examples',DOCS.ref);
  const jump=(label,items)=>{const d=document.createElement('div');d.className='jump';d.append(label+' ');
    items.forEach(([t,a])=>{d.appendChild(mkLink(t,DOCS.ref,a));d.append(' ')});el.appendChild(d)};
  const items=[['Tiles','basic-tiles'],['Buttons','basic-buttons'],['Sounds','basic-sounds'],['Settings','basic-settings-vario'],['Logs & system','basic-system']];
  if(adv){items.push(['More tiles','adv-tiles'],['More buttons','adv-buttons']);
    if(hwNow.gps)items.push(['Landing guidance','adv-guidance']);
    items.push(['More sounds','adv-sounds']);
    if(hwNow.spectrum!==false)items.push(['Spectrum','adv-spectrum'])}
  jump('Jump to:',items);
}

// A small Markdown converter for the guides: headings, paragraphs, lists (nested), tables, code blocks,
// quotes, rules, links, bold, italics, code, and the <a id="..."></a> anchors the guides use. Text is
// escaped; nothing else from the document reaches the page as HTML. Element ids get an 'hd-' prefix
// so they cannot clash with the page's own.
const hesc=s=>s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
function hInline(t){
  const codes=[];t=t.replace(/`([^`]+)`/g,(m,c)=>{codes.push(c);return '\u0001'+(codes.length-1)+'\u0001'});
  t=hesc(t);
  t=t.replace(/\[([^\]]+)\]\(((?:[^()\s]|\([^()]*\))+)\)/g,(m,x,u)=>'<a href="'+u.replace(/"/g,'%22')+'">'+x+'</a>');
  t=t.replace(/\*\*([^*]+)\*\*/g,'<b>$1</b>').replace(/(^|[^*\w])\*([^*\s][^*]*)\*/g,'$1<i>$2</i>');
  return t.replace(/\u0001(\d+)\u0001/g,(m,i)=>'<code>'+hesc(codes[i])+'</code>');
}
const hSlug=t=>t.toLowerCase().replace(/[^a-z0-9 -]/g,'').replace(/ /g,'-');
function hMd(src){
  const L=src.replace(/\r/g,'').split('\n'),out=[],LI=/^( *)([-*]|\d+\.) +(.*)$/;let i=0;
  while(i<L.length){
    const l=L[i];let m;
    if(!l.trim()){i++;continue}
    if(m=/^<a id="([\w-]+)"><\/a>\s*$/.exec(l)){out.push('<a id="hd-'+m[1]+'"></a>');i++;continue}
    if(/^```/.test(l)){const b=[];i++;while(i<L.length&&!/^```/.test(L[i]))b.push(L[i++]);i++;out.push('<pre>'+hesc(b.join('\n'))+'</pre>');continue}
    if(m=/^(#{1,6}) +(.*)$/.exec(l)){out.push('<h'+m[1].length+' id="hd-'+hSlug(m[2].replace(/[`*]/g,''))+'">'+hInline(m[2])+'</h'+m[1].length+'>');i++;continue}
    if(/^-{3,}\s*$/.test(l)){out.push('<hr>');i++;continue}
    if(/^>/.test(l)){const b=[];while(i<L.length&&/^>/.test(L[i]))b.push(L[i++].replace(/^> ?/,''));out.push('<blockquote>'+hMd(b.join('\n'))+'</blockquote>');continue}
    if(/^\|/.test(l)&&i+1<L.length&&/^\|[\s:|-]+\|\s*$/.test(L[i+1])){
      const row=r=>r.replace(/^\||\|\s*$/g,'').split('|').map(c=>c.trim());
      let h='<div class="tw"><table><tr>'+row(l).map(c=>'<th>'+hInline(c)+'</th>').join('')+'</tr>';i+=2;
      while(i<L.length&&/^\|/.test(L[i]))h+='<tr>'+row(L[i++]).map(c=>'<td>'+hInline(c)+'</td>').join('')+'</tr>';
      out.push(h+'</table></div>');continue}
    if(LI.test(l)){
      const items=[];
      while(i<L.length){const q=LI.exec(L[i]);
        if(q){items.push({ind:q[1].length,tag:/\d/.test(q[2])?'ol':'ul',t:q[3]});i++}
        else if(items.length&&/^ +\S/.test(L[i])){items[items.length-1].t+=' '+L[i].trim();i++}
        else break}
      let h='';const st=[];
      for(const it of items){
        while(st.length&&it.ind<st[st.length-1].ind)h+='</li></'+st.pop().tag+'>';
        if(!st.length||it.ind>st[st.length-1].ind){h+='<'+it.tag+'><li>';st.push({ind:it.ind,tag:it.tag})}
        else h+='</li><li>';
        h+=hInline(it.t)}
      while(st.length)h+='</li></'+st.pop().tag+'>';
      out.push(h);continue}
    const b=[l];i++;
    while(i<L.length&&L[i].trim()&&!/^(```|#{1,6} |>|\||<a id|-{3,}\s*$)/.test(L[i])&&!LI.test(L[i]))b.push(L[i++]);
    out.push('<p>'+hInline(b.join(' '))+'</p>');
  }
  return out.join('\n');
}

// The reader: shows one guide inside the Help tab, with Back to the previous guide or the list.
let helpStack=[],helpCur=null;
function helpScroll(anchor){const t=anchor&&document.getElementById('hd-'+anchor);(t||$('#helpReader')).scrollIntoView()}
async function openHelp(file,anchor,back){
  if(!back&&helpCur)helpStack.push(helpCur);
  helpCur={file,anchor};
  selectTab('help');
  $('#helpList').hidden=true;$('#helpReader').hidden=false;
  const el=$('#helpDoc');el.textContent='Loading…';
  try{
    const r=await fetch(docUrl(file,HELPRAW),{cache:'no-cache'});if(!r.ok)throw r.status;
    el.innerHTML=hMd(await r.text());
  }catch(e){
    el.innerHTML='<p>Could not load the guide. The phone needs internet access for this: turn mobile data on, or leave the vario\'s Wi-Fi for a moment, then try again.</p>';
  }
  helpScroll(anchor);
}
$('#helpBack').onclick=()=>{
  if(helpStack.length){const p=helpStack.pop();openHelp(p.file,p.anchor,true)}
  else{$('#helpReader').hidden=true;$('#helpList').hidden=false;helpCur=null}
};
$('#helpDoc').onclick=e=>{
  const a=e.target.closest('a[href]');if(!a)return;
  const h=a.getAttribute('href'),m=/^(?:\.\/|docs\/)?([^#:]+\.md)(?:#(.*))?$/.exec(h);
  if(h[0]==='#'){e.preventDefault();helpScroll(h.slice(1))}
  else if(m){e.preventDefault();openHelp(decodeURIComponent(m[1]),m[2])}
  else if(/^https?:/.test(h)){a.target='_blank';a.rel='noopener'}
  else e.preventDefault();
};

// Rolling height graph: the vario keeps the last ten minutes, fetched every few seconds.
let hgData=null,hgBusy=false;
async function pollHeight(){
  if(hgBusy||document.hidden||$('#flightPage').hidden)return;hgBusy=true;
  try{
    const b=await fetch('/api/height',{cache:'no-store'}).then(r=>r.arrayBuffer());
    const dv=new DataView(b),n=dv.getUint16(0,true),a=new Array(n);
    for(let i=0;i<n;i++)a[i]=dv.getInt16(4+2*i,true)/4;
    hgData={step:dv.getUint16(2,true),a};drawHeight();
  }catch(e){}finally{hgBusy=false}
}
function drawHeight(){
  const c=$('#hgCv');if(!c||!shown(c)||!c.clientWidth)return;
  const dpr=window.devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;
  if(c.width!==Math.round(W*dpr)||c.height!==Math.round(H*dpr)){c.width=Math.round(W*dpr);c.height=Math.round(H*dpr)}
  const g=c.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,W,H);
  const d=hgData;if(!d||d.a.length<2)return;
  const cs=getComputedStyle(document.body),acc=cs.getPropertyValue('--acc'),mut=cs.getPropertyValue('--mut'),bd=cs.getPropertyValue('--bd');
  const lo=Math.min(0,...d.a),hi=Math.max(...d.a,lo+30),N=300;
  const X=i=>W-(d.a.length-1-i)*W/(N-1),Y=m=>H-4-(m-lo)/(hi-lo)*(H-14);
  g.strokeStyle=bd;g.lineWidth=1;g.beginPath();g.moveTo(0,Math.round(Y(0))+.5);g.lineTo(W,Math.round(Y(0))+.5);g.stroke();
  g.beginPath();g.moveTo(X(0),Y(lo));d.a.forEach((m,i)=>g.lineTo(X(i),Y(m)));g.lineTo(X(d.a.length-1),Y(lo));g.closePath();
  g.globalAlpha=.25;g.fillStyle=acc;g.fill();g.globalAlpha=1;
  g.strokeStyle=acc;g.lineWidth=1.5;g.beginPath();d.a.forEach((m,i)=>i?g.lineTo(X(i),Y(m)):g.moveTo(X(i),Y(m)));g.stroke();
  g.fillStyle=mut;g.font='10px system-ui,sans-serif';g.textAlign='left';g.textBaseline='top';g.fillText(fH(hi),2,0);
}
setInterval(pollHeight,3000);addEventListener('resize',drawHeight);

async function poll(){
  try{
    const r=await fetch('/api/status',{cache:'no-store'});const s=await r.json();
    // Optional hardware: tiles, buttons and tabs for parts that are not fitted are hidden.
    const hw=s.hw||{gps:true,mic:true,spectrum:true},G=s.gps||{};
    document.body.classList.toggle('nogps',!hw.gps);document.body.classList.toggle('nomic',!hw.mic);document.body.classList.toggle('nospec',hw.spectrum===false);
    hwNow=hw;
    const k=hw.gps+'/'+hw.mic;if(k!==layKey){layKey=k;layout();buildHelp();addGroupHelp()}
    txt('t-agl',fH(s.agl));txt('t-msl',fH(s.altMsl)+' MSL');
    txt('t-vario',(s.vario>=0?'+':'')+s.vario.toFixed(1)+' m/s');txt('t-mute',s.muted?'muted':'');
    txt('t-speed',G.fix?fS(G.speed):'–');
    const L=s.glide||{};txt('t-glide',L.now!=null?fR(L.now):'–');txt('t-glides',(cfg.glideLongS||60)+' s avg '+(L.avg!=null?fR(L.avg):'–'));txt('t-track',G.fix?'track '+Math.round(G.track)+'°':'');
    // Flight time: the elapsed time is the main value while flying; on the ground it says Grounded and,
    // after a flight, the length of the last one.
    txt('t-flight',s.flying?fT(s.flightTime):'Grounded');
    txt('t-ftime',s.flying?'Flying'+(s.logging?' · logging':''):s.flightTime?'last flight '+fT(s.flightTime):'');
    $('#b-approach').classList.toggle('on',s.mode=='approach');$('#b-emergency').classList.toggle('on',s.mode=='emergency');
    $('#b-mute').textContent=s.muted?'Unmute vario':'Mute vario';
    txt('t-lz',!s.lz.valid?'not set':s.lz.dist!=null?fD(s.lz.dist):'set');
    txt('t-lzs',s.lz.valid?(s.lz.brg!=null?Math.round(s.lz.brg)+'° · ':'')+fH(s.lz.elev)+' MSL':'');
    txt('t-wind',s.wind.valid?Math.round(s.wind.from)+'°':'unknown');txt('t-winds',s.wind.valid?fS(s.wind.speed)+' · '+s.wind.src:'');
    txt('t-gps',!G.link?'no data':G.sats+' sats');
    const h=G.hdop,gq=!G.fix||!(h<=20)?'none':h<=2?'strong':h<=5?'good':h<=10?'ok':'poor';
    txt('t-gpss',(G.fix?'Fix':'Searching')+' · signal '+gq);
    // The banner doubles as the guidance-mode indicator: Emergency / Approach take it over
    // (even in flight, when the ready banner is normally hidden).
    const rd=$('#ready');
    if(s.mode=='emergency'){
      rd.className='ready emerg';txt('ready-t','Emergency');
      txt('ready-s',s.range!=null?'range '+fD(s.range):'');
    }else if(s.mode=='approach'){
      rd.className='ready appr';txt('ready-t','Approach');txt('ready-s',s.approach);
    }else{
      rd.className='ready '+(s.flying?'hide':s.ready?'ok':'wait');
      txt('ready-t',s.ready?'Ready to fly':s.notReady);
      txt('ready-s',hw.gps?G.sats+' / '+s.minSats+' satellites':'');
    }
    txt('t-batt',s.batt.v<2.5?'USB':s.batt.pct+'%');txt('t-batts',s.batt.v.toFixed(2)+' V'+(s.batt.min!=null?' · ~'+(s.batt.min>=60?Math.floor(s.batt.min/60)+' h ':'')+(s.batt.min%60)+' min':''));
    if(s.mic){
      txt('t-rpm',!s.mic.ok?'no mic':s.mic.rpm>0?Math.round(s.mic.rpm)+' rpm':'off');
      txt('t-rpms',s.mic.ok?'est '+Math.round(s.mic.raw)+' · conf '+s.mic.conf.toFixed(1):'');
      txt('t-noise',s.mic.noise!=null?s.mic.noise.toFixed(0)+' dBFS':'–');
      txt('t-noises',s.mic.ok?'vario vol '+s.vol.vario+'%':'');
    }
    txt('fwver','Installed: '+(s.version||'unknown')+(s.git?' · git '+s.git:'')+(s.debug?' (debug build)':' (release build)'));
    txt('hver','v'+(s.version||'unknown')+(s.debug?' debug':''));
    txt('sys','Uptime '+fT(s.uptime)+' · heap '+Math.round(s.heap/1024)+' kB · BLE clients '+s.ble+' · baro '+(s.baro.ok?(s.baro.pressure/100).toFixed(1)+' hPa':'missing'));
    wifiInfo=s.wifi;showWifi();
  }catch(e){}
}

function setVal(f,v){
  if(cfg[f.k]===v)return;
  cfg[f.k]=v;saveRevision++;txt('saveState','Unsaved changes');
  clearTimeout(saveTimer);saveTimer=setTimeout(saveSettings,650);
}
// Text boxes that are greyed out while their "Announce" checkbox is off.
const GATE={boundariesUp:'boundaryClimbing',boundaries:'boundaryDescending'};
function field(f){
  const row=document.createElement('label');row.className='row';
  const c=conv(f.u);const sp=document.createElement('span');sp.textContent=f.l;
  if(c.s&&f.t!=='bool'&&f.t!=='enum'&&f.t!=='str'){const sm=document.createElement('small');sm.textContent='('+c.s+')';sp.appendChild(sm)}
  if(f.r){const sm=document.createElement('small');sm.textContent='· restart';sp.appendChild(sm)}
  let i;
  if(f.t==='bool'){i=document.createElement('input');i.type='checkbox';i.checked=!!cfg[f.k];
    i.onchange=()=>{setVal(f,i.checked);document.querySelectorAll('input[data-gate="'+f.k+'"]').forEach(x=>x.disabled=!i.checked)}}
  else if(f.t==='enum'){i=document.createElement('select');f.o.split('|').forEach((o,n)=>i.add(new Option(o,n)));i.value=cfg[f.k];
    i.onchange=()=>{setVal(f,+i.value);if(/Unit$/.test(f.k))render()}}
  else if(f.t==='str'){i=document.createElement('input');i.type=/Pass$/.test(f.k)?'password':'text';i.maxLength=f.n;i.value=cfg[f.k];i.onchange=()=>setVal(f,i.value)}
  else if(f.t==='list'){i=document.createElement('input');i.type='text';i.inputMode='decimal';
    i.value=String(cfg[f.k]).split(',').filter(x=>x).map(v=>Math.round(v*c.f)).join(', ');
    if(GATE[f.k]){i.dataset.gate=GATE[f.k];i.disabled=!cfg[GATE[f.k]]}
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
    if(g==='Wi-Fi')d.insertAdjacentHTML('beforeend','<div class="row"><span>Vario network</span><b id="w-ap">–</b></div>'+
      '<div class="row"><span>Server network</span><b id="w-server">–</b></div><div class="row"><span>Server IP address</span><b id="w-server-ip">–</b></div>');
    groups[g].forEach(f=>d.appendChild(field(f)));root.appendChild(d);
  }
  showWifi();addGroupHelp();
}
let wifiInfo=null;
function showWifi(){
  const w=wifiInfo;if(!w||!document.getElementById('w-server'))return;
  txt('w-ap',(w.apSsid?w.apSsid+' · ':'')+w.apIp+' · '+w.apClients+' connected');
  const q=w.rssi>-60?'good':w.rssi>-70?'fair':'weak';
  let status=!w.serverSsid?'Off (not set)':w.server=='connected'?'Connected to '+w.serverSsid+' · signal '+q+' ('+w.rssi+' dBm)':
    w.server=='connecting'?'Connecting to '+w.serverSsid+'…':w.serverSsid+' – '+w.server;
  if(w.restart)status+=' · restart to apply changes';
  txt('w-server',status);
  txt('w-server-ip',w.serverIp?w.serverIp+' (paravario.local)':'–');
}
async function loadSettings(){
  [schema,cfg]=await Promise.all([fetch('/api/schema').then(r=>r.json()),fetch('/api/settings').then(r=>r.json())]);
  render();
}
async function saveSettings(){
  if(saveInFlight){saveAgain=true;return}
  saveInFlight=true;saveAgain=false;
  const revision=saveRevision;
  txt('saveState','Saving…');
  try{
    const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)});
    const j=await r.json();if(!r.ok)throw j.error;
    restartRequired=restartRequired||j.restart;
    if(revision===saveRevision){cfg=j.settings;txt('saveState',restartRequired?'Saved · restart required':'Saved')}
    else saveAgain=true;
  }catch(e){if(revision===saveRevision)txt('saveState','Save failed: '+e)}
  finally{
    saveInFlight=false;
    if(saveAgain){saveAgain=false;saveSettings()}
  }
}

function selectTab(name){
  document.querySelectorAll('button[data-tab]').forEach(b=>b.setAttribute('aria-selected',b.dataset.tab===name));
  document.querySelectorAll('.page').forEach(p=>p.hidden=p.id!==name+'Page');
  if(name==='system')loadLogs();
)HTML"
#if PV_HAS_SPECTRUM
R"HTML(  const was=specOn;specOn=name==='spectrum';if(specOn&&!was)specLoop();
)HTML"
#endif
R"HTML(
}
)HTML"
#if PV_HAS_SPECTRUM
R"HTML(let specSeq=0,specOn=false,specPaused=false,specBusy=false,specData=null,specLast=0,specFps=0;
async function specLoop(){
  if(!specOn)return;
  if(!specPaused&&!specBusy&&!document.hidden){
    specBusy=true;
    try{
      const r=await fetch('/api/spectrum?since='+specSeq,{cache:'no-store'});
      if(r.status===200){
        const b=await r.arrayBuffer(),dv=new DataView(b),now=performance.now();
        specSeq=dv.getUint32(0,true);
        specData={hz:dv.getFloat32(4,true),tone:dv.getFloat32(8,true),db:new Uint8Array(b,12)};
        if(specLast)specFps=specFps?specFps*.7+300/(now-specLast):1000/(now-specLast);
        specLast=now;drawSpec();
      }
    }catch(e){}finally{specBusy=false}
  }
  setTimeout(specLoop,40);
}
function drawSpec(){
  const d=specData;if(!d)return;
  const c=$('#specCv'),dpr=window.devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;
  if(c.width!==Math.round(W*dpr)||c.height!==Math.round(H*dpr)){c.width=Math.round(W*dpr);c.height=Math.round(H*dpr)}
  const g=c.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,W,H);
  const cs=getComputedStyle(document.body),acc=cs.getPropertyValue('--acc'),mut=cs.getPropertyValue('--mut'),bd=cs.getPropertyValue('--bd'),warn=cs.getPropertyValue('--warn');
  const L=40,R=12,T=6,B=20,pw=W-L-R,ph=H-T-B,n=d.db.length,fmax=d.hz*n,floor=+$('#specfloor').value;
  const X=f=>L+f/fmax*pw,Y=db=>T+Math.min(1,Math.max(0,-db/floor))*ph;
  g.font='11px system-ui,sans-serif';g.lineWidth=1;g.fillStyle=mut;g.strokeStyle=bd;
  g.textAlign='right';g.textBaseline='middle';
  for(let db=0;db>=-floor;db-=20){const y=Math.round(Y(db))+.5;g.beginPath();g.moveTo(L,y);g.lineTo(W-R,y);g.stroke();g.fillText(db,L-4,y)}
  g.textAlign='center';g.textBaseline='top';
  for(let f=0;f<=fmax;f+=100){const x=Math.round(X(f))+.5;g.beginPath();g.moveTo(x,T);g.lineTo(x,T+ph);g.stroke();g.fillText(f>=1000?f/1000+'k':f,x,T+ph+4)}
  g.beginPath();g.moveTo(X(0),T+ph);
  let pk=1;
  for(let k=0;k<n;k++){const db=d.db[k]/2.5-100;g.lineTo(X((k+.5)*d.hz),Y(db));if(k>0&&d.db[k]>d.db[pk])pk=k}
  g.lineTo(X(fmax),T+ph);g.closePath();g.globalAlpha=.25;g.fillStyle=acc;g.fill();g.globalAlpha=1;
  g.strokeStyle=acc;g.lineWidth=1.5;g.beginPath();
  for(let k=0;k<n;k++){const x=X((k+.5)*d.hz),y=Y(d.db[k]/2.5-100);k?g.lineTo(x,y):g.moveTo(x,y)}
  g.stroke();
  if(d.tone>0){g.strokeStyle=warn;g.setLineDash([4,4]);g.beginPath();g.moveTo(X(d.tone),T);g.lineTo(X(d.tone),T+ph);g.stroke();g.setLineDash([])}
  txt('specinfo','Peak '+rnd((pk+.5)*d.hz,0)+' Hz at '+rnd(d.db[pk]/2.5-100,0)+' dBFS · bin '+rnd(d.hz,1)+' Hz · '+rnd(specFps,1)+' updates/s'+(d.tone>0?' · speaker tone '+rnd(d.tone,0)+' Hz (dashed)':''));
}
$('#specpause').onclick=()=>{specPaused=!specPaused;$('#specpause').textContent=specPaused?'Resume':'Pause'};
$('#specfloor').onchange=drawSpec;
)HTML"
#endif
R"HTML(
document.querySelectorAll('button[data-tab]').forEach(b=>b.onclick=()=>selectTab(b.dataset.tab));

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
let fwBusy=false;
function setFw(p,m){$('#fwbar').style.width=Math.round(p*100)+'%';txt('fwmsg',m)}
$('#fwgo').onclick=()=>{
  const f=$('#fwfile').files[0];if(!f)return toast('Choose a .bin file first');
  const pw=$('#fwpass').value;if(!pw)return toast('Enter the update password');
  const fs=/littlefs|spiffs/i.test(f.name);
  if(!confirm((fs?'Replace the voice clips with '+f.name+'? This erases the flight logs.':'Install firmware '+f.name+'?')+' The vario restarts afterwards.'))return;
  const fd=new FormData();fd.append('file',f,f.name);
  const x=new XMLHttpRequest();x.open('POST','/api/ota'+(fs?'?target=fs':''));x.setRequestHeader('X-OTA-Password',pw);
  const done=m=>{fwBusy=false;$('#fwgo').disabled=false;setFw(0,m)};
  x.upload.onprogress=e=>{if(e.lengthComputable)setFw(e.loaded/e.total,'Uploading '+Math.round(e.loaded/e.total*100)+'%')};
  x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}
    if(x.status!=200)return done('Update failed: '+(j.error||'HTTP '+x.status));
    setFw(1,'Installed. Restarting – rejoin the ParaVario Wi-Fi if your phone drops it.');setTimeout(()=>location.reload(),10000)};
  x.onerror=()=>done('Update failed: connection lost');
  fwBusy=true;$('#fwgo').disabled=true;setFw(0,'Uploading 0%');x.send(fd);
};
async function boot(){
  await loadSettings().catch(()=>{});
  poll();setInterval(poll,1000);setInterval(pollDbg,1000);
  try{const s=await fetch('/api/status').then(r=>r.json());
    if(s.debug){selectTab('system');$('#dbg').open=true;pollDbg()}}catch(e){}
}
boot();
layout();buildHelp();pollHeight();
// Full screen: hides the browser's address bar and the phone's own bars, much as an installed app would.
// (Installing the page is not possible over plain HTTP; this works anywhere the browser supports it.)
(function(){
  const d=document,r=d.documentElement,req=r.requestFullscreen||r.webkitRequestFullscreen,b=$('#b-fs');
  if(!req||navigator.standalone||(window.matchMedia&&matchMedia('(display-mode: standalone)').matches))return;b.hidden=false;
  const on=()=>!!(d.fullscreenElement||d.webkitFullscreenElement),show=()=>{b.textContent=on()?'Exit full screen':'Full screen'};
  b.onclick=()=>{try{
    if(on())(d.exitFullscreen||d.webkitExitFullscreen).call(d);
    else{const p=r.requestFullscreen?r.requestFullscreen({navigationUI:'hide'}):r.webkitRequestFullscreen();if(p&&p.catch)p.catch(()=>toast('Full screen is not available here'))}
  }catch(e){toast('Full screen is not available here')}};
  d.addEventListener('fullscreenchange',show);d.addEventListener('webkitfullscreenchange',show);
})();
</script></body></html>)HTML";
