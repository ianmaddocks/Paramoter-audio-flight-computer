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
.vrow{display:flex;align-items:center;justify-content:space-between;height:46px}
.vrow span{overflow:hidden;text-overflow:ellipsis}
.arr{width:46px;height:46px;flex:none;color:var(--acc);transition:transform .9s linear}
.arr[hidden]{display:none}
.alerts{display:flex;flex-direction:column;gap:6px;margin-bottom:8px}
.alerts:empty{display:none}
.alert{border-radius:12px;padding:10px 14px;font-size:15px;font-weight:600;color:#fff}
.alert.warn{background:var(--warn)}.alert.bad{background:var(--bad)}
body.flying .groundonly{display:none}
body:not(.flying) .flightonly{display:none}
#t-winds{white-space:normal}
.maintbox{margin:6px 0 2px;padding-left:10px;border-left:3px solid var(--bd)}
.maintbox[hidden]{display:none}
.windrow{display:flex;justify-content:flex-end;height:92px}
.windrow[hidden]{display:none}
.sockw{flex:none;width:92px;height:92px}
.sockw[hidden]{display:none}
.sockg{transform-origin:42px 42px;transition:transform .9s linear}
.so{fill:#E8590C;stroke:#7A2E00;stroke-width:.7}
.sw{fill:#fff;stroke:#7A2E00;stroke-width:.7}
.sring{fill:none;stroke:var(--mut);stroke-width:1.6}
.spole{fill:var(--mut)}
.srow{display:flex;align-items:center;justify-content:space-between;min-height:58px;overflow:visible}
.cmp{flex:none;width:56px;height:56px}
.cmp[hidden]{display:none}
.cring{fill:var(--bg);stroke:var(--mut);stroke-width:1.5}
.cmparrow{fill:var(--acc);transform-origin:42px 42px;transition:transform .9s linear}
.cb{fill:var(--card);stroke:var(--card);stroke-width:1}
.cl,.cn{font:500 15px system-ui,sans-serif;text-anchor:middle}
.cl{fill:var(--mut)}
.cn{fill:var(--bad);font-size:17px}
.tile .v{font-size:22px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tile .s{font-size:12px;color:var(--mut);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:12px;margin-top:10px}
details.card{padding:0}
summary{padding:12px;font-weight:600;cursor:pointer}
details .row{margin:0 12px}
details[open]{padding-bottom:6px}
details.sub{margin:8px 10px 0}
.advnote{margin:0 12px 4px}
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
#areaMap{height:340px;border-radius:10px;border:1px solid var(--bd);background:var(--bg)}
#areaMap[hidden]{display:none}
#areaBox{position:relative}
#areaPts{height:222px;overflow-y:auto;overscroll-behavior:contain;border:1px solid var(--bd);border-radius:8px;padding:0 8px;margin:8px 0}
#areaPts:empty{display:none}
.apt{display:flex;align-items:center;gap:6px;height:44px;border-top:1px solid var(--bd)}
.apt:first-child{border-top:0}
.apt .n{flex:none;width:22px;height:22px;border-radius:50%;background:#f08c00;color:#fff;font:600 12px/22px system-ui,sans-serif;text-align:center}
.apt .c{flex:1;min-width:0;font:13px ui-monospace,Menlo,Consolas,monospace;cursor:pointer;overflow:hidden;text-overflow:ellipsis}
.apt button{padding:4px 9px;min-width:36px}
.apt button:disabled{opacity:.3}
.acn{width:22px;height:22px;border-radius:50%;background:#f08c00;border:2px solid #fff;color:#fff;font:600 12px/18px system-ui,sans-serif;text-align:center}
#areaPaste{width:100%;height:90px;font:12px ui-monospace,Menlo,Consolas,monospace;background:var(--bg);color:var(--fg);border:1px solid var(--bd);border-radius:8px;padding:6px}
body.nogps .needgps,body.nomic .needmic,body.nospec .needspec{display:none}
/* Until the vario has said which parts are fitted (and whether the Spectrum tab is on), hide what may not be there, so it does not flash up and go. */
body.loading .needgps,body.loading .needmic,body.loading .needspec{display:none}
/* Replay: the screen shows a log, so the buttons that act on the vario are hidden */
body.replay #flightActions,body.replay #groundActions,body.replay #b-fs{display:none!important}
#replayBar{border:2px solid var(--acc)}
.rprow{display:flex;gap:8px;align-items:center;margin:8px 0 4px}
.rprow input[type=range]{flex:1;min-width:0}
.rprow button{min-width:56px}
#rpVol{width:100%;height:84px;display:block;margin-top:6px}
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
#helpBody .hbtn{margin:0 0 10px;font-size:14px}
#helpBody .jump{margin:6px 0 2px;color:var(--mut);font-size:13px}#helpBody .jump a{margin-right:12px;color:var(--acc);white-space:nowrap}
.tile.wide{grid-column:span 2}
.volrow{display:flex;align-items:center;justify-content:space-between;gap:8px}
.volb{display:flex;gap:6px;flex:none}.volb button{width:42px;height:36px;padding:0;font-size:22px;line-height:1}
body.replay .volb{display:none}
.btns .wide{grid-column:span 2}
#hgCv{width:100%;height:56px;display:block;margin-top:2px}
.tiles.more{margin-top:10px}
.badge{font-size:11px;background:var(--warn);color:#000;border-radius:6px;padding:1px 6px;margin-left:6px;vertical-align:middle}
</style></head><body class="loading">
<nav class="tabs" role="tablist" aria-label="Main views">
<button type="button" role="tab" aria-selected="true" data-tab="flight">Flight</button>
<button type="button" role="tab" aria-selected="false" data-tab="system">Logs &amp; system</button>
<button type="button" role="tab" aria-selected="false" data-tab="settings">Settings</button>
<button type="button" role="tab" aria-selected="false" data-tab="area" class="needgps">Map</button>
)HTML"
#if PV_HAS_SPECTRUM
R"HTML(<button type="button" role="tab" aria-selected="false" data-tab="spectrum" class="needmic needspec">Spectrum</button>
)HTML"
#endif
R"HTML(
<button type="button" role="tab" aria-selected="false" data-tab="help">Help</button>
</nav>
<div id="flightPage" class="page" role="tabpanel">
<section class="card" id="replayBar" hidden><b>Replay</b> <span class="mut" id="rpName"></span>
<div class="rprow"><button id="rpPlay">Pause</button><input type="range" id="rpSlider" min="0" max="1" value="0" step="1" aria-label="Position in the flight"><button id="rpSpeed">1×</button><button id="rpExit" class="dng">Exit</button></div>
<div class="mut" id="rpTime"></div>
<canvas id="rpVol" height="84"></canvas><div class="mut" id="rpVolTxt"></div></section>
<div id="ready" class="ready wait"><b id="ready-t">Checking…</b><span id="ready-s"></span></div>
<div id="alerts" class="alerts" role="alert" aria-live="assertive"></div>
<section class="tiles" id="tilesMain">
<div class="tile" id="tile-height"><div class="k">Height AGL</div><div class="v" id="t-agl">–</div><div class="s" id="t-msl"></div></div>
<div class="tile"><div class="k">Flight time</div><div class="v" id="t-flight">–</div><div class="s" id="t-ftime"></div></div>
<div class="tile" id="tile-vario"><div class="k">Vario</div><div class="v" id="t-vario">–</div><div class="s" id="t-mute"></div><canvas id="hgCv"></canvas></div>
<div class="tile needgps" id="tile-speed"><div class="k">Ground speed</div><div class="v" id="t-speed"><span id="t-speedv">–</span></div><div class="s srow"><span id="t-track"></span><span class="cmp" hidden><svg viewBox="0 0 84 84" width="56" height="56" aria-hidden="true"><circle class="cring" cx="42" cy="42" r="33"/><g id="a-track" class="cmparrow"><path d="M42 26 L50 41 L45 41 L45 58 L39 58 L39 41 L34 41 Z"/></g><rect class="cb" x="31" y="1" width="22" height="22" rx="6"/><text class="cn" x="42" y="18">N</text><rect class="cb" x="61" y="31" width="22" height="22" rx="6"/><text class="cl" x="72" y="47">E</text><rect class="cb" x="31" y="61" width="22" height="22" rx="6"/><text class="cl" x="42" y="77">S</text><rect class="cb" x="1" y="31" width="22" height="22" rx="6"/><text class="cl" x="12" y="47">W</text></svg></span></div></div>
<div class="tile needgps"><div class="k">Landing zone</div><div class="v vrow" id="t-lz"><span id="t-lzv">–</span><svg id="a-lz" class="arr" viewBox="-20 -20 40 40" aria-hidden="true" hidden><path d="M0 -19 L12 -4 L4.5 -4 L4.5 19 L-4.5 19 L-4.5 -4 L-12 -4 Z" fill="currentColor"/></svg></div><div class="s" id="t-lzs"></div></div>
<div class="tile needgps" id="tile-wind"><div class="k">Wind</div><div class="v" id="t-wind">–</div><div class="windrow" id="wind-sockrow" hidden><span class="sockw" hidden><svg viewBox="0 0 84 84" width="92" height="92" aria-hidden="true"><g id="a-wind" class="sockg"><polygon class="so" points="24,78 60,78 56,60 28,60"/><polygon class="sw" points="28,60 56,60 53,42 31,42"/><polygon class="so" points="31,42 53,42 50,24 34,24"/><polygon class="sw" points="34,24 50,24 47,6 37,6"/><line class="sring" x1="22" y1="78" x2="62" y2="78"/></g></svg></span></div><div class="s" id="t-winds"></div></div>
<div class="tile needgps"><div class="k">Glide ratio</div><div class="v" id="t-glide">–</div><div class="s" id="t-glides"></div></div>
<div class="tile"><div class="k">Battery</div><div class="v" id="t-batt">–</div><div class="s" id="t-batts"></div></div>
</section>
<section class="card flightonly" id="flightActions"><h2>Actions</h2><div class="btns">
<button data-a="approach" id="b-approach" class="needgps">Landing support [..]</button>
<button data-a="emergency" id="b-emergency" class="dng needgps">Emergency support [—]</button>
<button data-a="tell_me" id="b-tell">Status [.]</button>
<button data-a="mute" id="b-mute" class="b-mute">Mute vario [...]</button>
</div></section>
<section class="card groundonly" id="groundActions"><h2>Actions</h2><div class="btns">
<button data-a="tell_me" id="b-status">Status [.]</button>
</div></section>
<div class="btns"><button type="button" id="b-fs" class="wide" hidden>Full screen</button></div>
<section class="tiles more" id="tilesMore">
<div class="tile needgps"><div class="k">GPS</div><div class="v" id="t-gps">–</div><div class="s" id="t-gpss"></div></div>
<div class="tile needmic"><div class="k">Engine RPM</div><div class="v" id="t-rpm">–</div><div class="s" id="t-rpms"></div></div>
<div class="tile" id="tile-volume"><div class="k">Volume</div><div class="vrow volrow"><span class="v" id="t-vol">–</span><span class="volb"><button type="button" id="b-volm" data-d="-5" aria-label="Quieter">−</button><button type="button" id="b-volp" data-d="5" aria-label="Louder">+</button></span></div><div class="s" id="t-vols"></div></div>
<div class="tile needmic" id="tile-noise" hidden><div class="k">Background noise</div><div class="v" id="t-noise">–</div><div class="s" id="t-noises"></div></div>
</section>
 </div>
<div id="settingsPage" class="page" role="tabpanel" hidden>
<section id="settings"></section>
<p id="saveState" class="mut" role="status" aria-live="polite"></p>
<section class="card" id="restartBar" hidden><p>A restart is needed for some of the changes to take effect.</p><div class="btns"><button data-a="restart" class="pri">Restart</button></div></section>
</div>
<div id="areaPage" class="page" role="tabpanel" hidden>
<section class="card"><h2>Map and areas</h2>
<p class="mut" id="areaState">No safe area is set.</p>
<p class="mut">Safe areas (green) are places you want to stay inside: you are told when you leave one. Restricted areas (red) are places to keep out of: you are warned before you enter one, with the way to turn. Add the corners of an area one at a time with the cross on the map.</p>
<div id="areaList"></div>
</section>
<section class="card"><h2 id="areaTitle">New area</h2>
<div id="areaBox"><div id="areaMap" hidden></div>
</div>
<p class="mut" id="areaMapMsg">Open this tab to load the map. It needs internet on this phone (mobile data or another Wi-Fi).</p>
<p class="mut" id="areaHelp"><b>Tap the map</b> to add a corner where you tap. Drag a numbered corner to move it.</p>
<div id="areaPts"></div>
<div class="row"><span>Name</span><input type="text" id="areaName" maxlength="23" autocomplete="off"></div>
<div class="row"><span>Type</span><select id="areaKind"><option value="safe">Safe (green)</option><option value="restricted">Restricted (red)</option></select></div>
<div class="row"><span>Warn me <small id="areaWarnU"></small></span><input type="number" id="areaWarn" min="0" step="10" inputmode="numeric"></div>
<p class="mut" id="areaWarnNote"></p>
<div class="btns">
<button type="button" id="areaUndo">Remove last corner</button>
<button type="button" id="areaCancel">Cancel</button>
<button type="button" id="areaSave" class="pri wide">Save area</button>
</div>
<details class="sub"><summary>Paste corners</summary>
<p class="mut">One corner per line as latitude, longitude, or the coordinates from geojson.io.</p>
<textarea id="areaPaste" spellcheck="false"></textarea>
<div class="btns" style="margin-top:8px"><button type="button" id="areaUse">Use these corners</button></div></details>
</section>
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
<section class="card"><h2>System</h2><div class="btns">
<button data-a="restart">Restart</button>
<button data-a="reset_flight">Reset flight state</button>
<button data-a="defaults" class="dng">Factory defaults</button>
</div><p class="mut" id="sys"></p></section>
</div>
<!-- The firmware update box lives in Settings / Advanced / Firmware Update: the settings list is rebuilt now and then, so
     the box waits here and is put into the group each time (see render). -->
<div id="fwholder" hidden><div id="fwcard">
<p class="mut" id="fwver"></p>
<input type="file" id="fwfile" accept=".bin">
<input type="password" id="fwpass" placeholder="Update password" autocomplete="off">
<button class="pri" id="fwgo">Install update</button>
<div class="prog"><div id="fwbar"></div></div><p class="mut" id="fwmsg">Choose firmware.bin (program) or littlefs.bin (voice clips &ndash; erases flight logs) and enter the update password.</p>
</div></div>
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
// The vario's web server answers one request at a time, every request is a new connection, and the connections' buffers
// come out of a very small memory. So this page's own requests go one after another (the next starts when the last has
// its answer) instead of a burst of six at load time, and none can wait for ever. Requests to other sites (the help
// guides) are left alone.
(()=>{const raw=window.fetch.bind(window);let chain=Promise.resolve();
  window.fetch=(u,o)=>{if(typeof u!=='string'||u[0]!=='/')return raw(u,o);
    const opt=Object.assign({},o||{});if(!opt.signal&&window.AbortSignal&&AbortSignal.timeout)opt.signal=AbortSignal.timeout(20000);
    // (503 = the vario was short of memory at that moment and turned the request away: ask again, a little later each time)
    const go=async()=>{for(let i=0;;i++){const r=await raw(u,opt);if(r.status!==503||i>=4)return r;await new Promise(t=>setTimeout(t,300*(i+1)))}},r=chain.then(go,go);chain=r.then(()=>0,()=>0);return r}})();
const $=s=>document.querySelector(s);
const FT=3.28084;
let schema=[],cfg={},openGroups=new Set();
// The settings list differs with and without a GPS, and the vario finds out whether one is fitted
// after power-on, so the list is fetched again whenever that changes (schemaKey = what it was fetched for).
let schemaKey='',settingsBusy=false;
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
// Direction arrow (up = straight ahead). The angle is smoothed (a third of the way to each new reading) so GPS
// course noise does not make it jump, kept unwrapped so it never spins the long way round at 0/360, and the
// CSS transition moves it between readings. null hides it.
const arrowAng={};
function setArrow(id,deg){const e=document.getElementById(id),w=e.closest('.cmp,.sockw');
  if(deg==null||isNaN(deg)){e.toggleAttribute('hidden',true);if(w)w.hidden=true;delete arrowAng[id];return}
  if(w)w.hidden=false;
  let a=arrowAng[id];
  if(a==null){a=deg;e.style.transition='none';e.style.transform='rotate('+a.toFixed(1)+'deg)';e.getBoundingClientRect();e.style.transition='';e.toggleAttribute('hidden',false)}
  else{a+=(((deg-a)%360+540)%360-180)*0.35;e.toggleAttribute('hidden',false);e.style.transform='rotate('+a.toFixed(1)+'deg)'}
  arrowAng[id]=a}

// Tile layout. Hidden tiles (hardware not fitted) do not count.
//  - Main group: if its cells (tiles plus the height graph) come to an odd number, the Height
//    tile takes the full width and the graph moves under it, beside Vario; otherwise the graph
//    sits beside the Height tile.
//  - Lower group: a tile left alone on the last row takes the full width.
const shown=e=>getComputedStyle(e).display!=='none';
function layout(){
  for(const id of ['#tilesMain','#tilesMore']){
    const ts=[...$(id).children];ts.forEach(e=>e.classList.remove('wide'));
    const vis=ts.filter(shown);
    if(vis.length%2===1)vis[vis.length-1].classList.add('wide');
  }
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
  'Audio':{b:'basic-settings-units-and-audio'},'Engine RPM':{b:'basic-settings-engine-rpm'},
  'Vario':{b:'basic-settings-vario'},'Height calls':{b:'basic-settings-height-calls'},
  'Takeoff & landing':{b:'basic-settings-takeoff-and-landing',a:'adv-settings-takeoff-and-landing'},
  'Glide':{a:'adv-settings-glide'},'Landing zone':{a:'adv-settings-landing-zone'},'Landing approach':{a:'adv-settings-landing-approach'},
  'Areas':{a:'adv-safe-area'},'Emergency':{a:'adv-settings-emergency'},'Wind':{a:'adv-settings-wind'},'GPS':{a:'adv-settings-gps',always:1},
  'Bluetooth':{b:'basic-settings-bluetooth',a:'adv-settings-bluetooth'},'Wi-Fi':{b:'basic-settings-wi-fi'},'Firmware Update':{b:'basic-settings-firmware-update'},
  'Battery':{b:'basic-settings-battery'},'Logging':{a:'adv-settings-logging'}};
let hwNow={gps:true,mic:true,spectrum:true};
// A link to a guide section. The address is the GitHub one (for long-press or copy); a tap opens the reader.
function mkLink(t,file,anchor,cls){const a=document.createElement('a');a.href=webLink(file,anchor);a.textContent=t;if(cls)a.className=cls;
  a.onclick=e=>{e.preventDefault();openHelp(file,anchor)};return a}
// A 'Help' link on each settings group that opens its entry in the Feature Reference.
function addGroupHelp(){
  const adv=hwNow.gps;
  document.querySelectorAll('#settings details').forEach(d=>{
    const s=d.querySelector('summary'),old=s.querySelector('.help');if(old)old.remove();
    const g=GROUP_HELP[s.firstChild.textContent];if(!g)return;
    const span=document.createElement('span');span.className='help';
    if(g.b)span.appendChild(mkLink('Help',DOCS.ref,g.b));
    if(g.a&&(adv||g.always))span.appendChild(mkLink(g.b?'More help':'Help',DOCS.ref,g.a));
    if(span.childNodes.length)s.appendChild(span);
  });
}
// The Help tab: the user guide for this vario (the one for varios with a GPS, or without), the feature
// reference, and shortcuts into the reference for the tiles, buttons and sounds this vario has. It
// does not talk about versions: it simply links to the right material.
function buildHelp(){
  const adv=hwNow.gps,el=$('#helpBody');el.innerHTML='';
  // What the one push button does, right at the top.
  const bp=document.createElement('p');bp.className='hbtn';
  bp.innerHTML=adv?'<b>Push button.</b> One press tells you the status (height, landing zone, wind). Two quick presses start landing support (again to cancel). Hold for 1.5 seconds for emergency support (again to cancel). Three presses mute or unmute the beeps.'
    :'<b>Push button.</b> One press tells you your height. Two or three presses, or a hold, switch the vario beeps off or on.';
  el.appendChild(bp);
  const doc=(t,sub,file)=>{const a=mkLink(t,file,'','doc'),s=document.createElement('small');s.textContent=sub;a.appendChild(s);el.appendChild(a)};
  doc('User guide','How to use this vario, from switching on to landing',adv?DOCS.adv:DOCS.basic);
  doc('Feature reference','Every tile, button, sound and setting explained, with examples',DOCS.ref);
  const jump=(label,items)=>{const d=document.createElement('div');d.className='jump';d.append(label+' ');
    items.forEach(([t,a])=>{d.appendChild(mkLink(t,DOCS.ref,a));d.append(' ')});el.appendChild(d)};
  const items=[['Push button',adv?'adv-buttons':'basic-buttons'],['Tiles','basic-tiles'],['Buttons','basic-buttons'],['Sounds','basic-sounds'],['Settings','basic-settings-vario'],['Logs & system','basic-system']];
  if(hwNow.spectrum!==false)items.push(['Spectrum','basic-spectrum']);
  if(adv)items.push(['GPS tiles','adv-tiles'],['GPS buttons','adv-buttons'],['Landing guidance','adv-guidance'],['GPS sounds','adv-sounds']);
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

// Rolling height graph: the vario keeps the last ten minutes flown (nothing while grounded, kept after landing, a gap
// between flights), fetched every few seconds.
let hgData=null,hgBusy=false;
async function pollHeight(){
  if(rp)return;
  if(hgBusy||document.hidden||$('#flightPage').hidden)return;hgBusy=true;
  try{
    const b=await fetch('/api/height',{cache:'no-store'}).then(r=>r.arrayBuffer());
    const dv=new DataView(b),n=dv.getUint16(0,true),a=new Array(n);
    for(let i=0;i<n;i++){const r=dv.getInt16(4+2*i,true);a[i]=r===-32768?null:r/4}  // null: the gap between two flights
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
  const real=d.a.filter(m=>m!==null);if(real.length<2)return;
  const lo=Math.min(0,...real),hi=Math.max(...real,lo+30),N=300;
  const X=i=>W-(d.a.length-1-i)*W/(N-1),Y=m=>H-4-(m-lo)/(hi-lo)*(H-14);
  g.strokeStyle=bd;g.lineWidth=1;g.beginPath();g.moveTo(0,Math.round(Y(0))+.5);g.lineTo(W,Math.round(Y(0))+.5);g.stroke();
  // one trace per flight: a gap (null samples) ends the line and the fill, and is marked with a dotted vertical line
  const runs=[];let cur=null;d.a.forEach((m,i)=>{if(m===null){cur=null;return}if(!cur){cur=[];runs.push(cur)}cur.push(i)});
  for(const r of runs){
    g.beginPath();g.moveTo(X(r[0]),Y(lo));r.forEach(i=>g.lineTo(X(i),Y(d.a[i])));g.lineTo(X(r[r.length-1]),Y(lo));g.closePath();
    g.globalAlpha=.25;g.fillStyle=acc;g.fill();g.globalAlpha=1;
    g.strokeStyle=acc;g.lineWidth=1.5;g.beginPath();r.forEach((i,k)=>k?g.lineTo(X(i),Y(d.a[i])):g.moveTo(X(i),Y(d.a[i])));g.stroke();
  }
  g.strokeStyle=bd;g.setLineDash([2,3]);d.a.forEach((m,i)=>{if(m===null&&i>0&&d.a[i-1]!==null&&i<d.a.length-1){const x=Math.round(X(i+7))+.5;g.beginPath();g.moveTo(x,2);g.lineTo(x,H-2);g.stroke()}});g.setLineDash([]);
  g.fillStyle=mut;g.font='10px system-ui,sans-serif';g.textAlign='left';g.textBaseline='top';g.fillText(fH(hi),2,0);
  g.textAlign='right';g.fillText('last 10 min flown',W-2,0);
}
setInterval(pollHeight,3000);addEventListener('resize',drawHeight);

let engShown=false;
// The alert strip: what the pilot must know in flight whichever tab or tile is showing, from the status alone.
function setAlerts(s,G,hw,bm){
  const al=[];
  if(s.flying&&!s.baro.ok)al.push(['bad','Barometer failed: vario tones are off']);
  if(s.flying&&hw.gps&&!G.fix)al.push(['warn','No GPS fix']);
  const bv=s.batt.v;
  if(bv>=2.5&&cfg.battLowV!=null){
    const crit=Math.min(cfg.battCriticalV!=null?cfg.battCriticalV:3.3,cfg.battLowV);
    const adv=(bm?' · about '+bm+' remaining':'')+(s.lz.valid&&s.lz.dist!=null?' · LZ '+fD(s.lz.dist):'');
    const off=hw.gps&&s.lz.valid?' · you can switch me off until you need help landing':'';
    if(bv<crit)al.push(['bad','Battery critical'+adv+off]);else if(bv<cfg.battLowV)al.push(['warn','Battery low'+adv+off]);
  }
  const key=JSON.stringify(al),el=$('#alerts');
  if(el.dataset.k===key)return;
  el.dataset.k=key;el.innerHTML='';
  al.forEach(a=>{const d=document.createElement('div');d.className='alert '+a[0];d.textContent=a[1];el.appendChild(d)});
}
setTimeout(()=>document.body.classList.remove('loading'),4000);  // a vario that does not answer: show the page as it is
async function poll(){
  try{
    const r=await fetch('/api/status',{cache:'no-store'});const s=await r.json();
    if(rp){rp.hw=s.hw||rp.hw;return}  // replaying a log: the screen follows the log, not the vario
    renderStatus(s,false);
  }catch(e){}
}
function renderStatus(s,replay){
  {
    // Optional hardware: tiles, buttons and tabs for parts that are not fitted are hidden.
    const hw=s.hw||{gps:true,mic:true,spectrum:true},G=s.gps||{};
    document.body.classList.toggle('nogps',!hw.gps);if(!hw.gps&&!$('#areaPage').hidden)selectTab('flight');document.body.classList.toggle('nomic',!hw.mic);document.body.classList.toggle('nospec',hw.spectrum===false);
    hwNow=hw;
    document.body.classList.remove('loading');
    const k=hw.gps+'/'+hw.mic;if(k!==layKey){layKey=k;layout();buildHelp();addGroupHelp()}
    if(schemaKey&&k!==schemaKey&&!saveInFlight&&!settingsBusy){settingsBusy=true;loadSettings().catch(()=>{}).finally(()=>{settingsBusy=false})}
    document.body.classList.toggle('flying',!!s.flying);
    areaStatus(s);
    {const e=!!cfg.showEngTiles;if(e!==engShown){engShown=e;$('#tile-noise').hidden=!e;layout()}}
    txt('t-agl',fH(s.agl));txt('t-msl',fH(s.altMsl)+' MSL');
    txt('t-vario',(s.vario>=0?'+':'')+s.vario.toFixed(1)+' m/s');txt('t-mute',s.muted?'muted':'');
    txt('t-speedv',G.fix?fS(G.speed):'–');
    const L=s.glide||{};txt('t-glide',L.now!=null?fR(L.now):'–');txt('t-glides',(cfg.glideLongS||60)+' s avg '+(L.avg!=null?fR(L.avg):'–'));txt('t-track',G.fix?'track '+Math.round(G.track)+'°':'');
    // Flight time: the elapsed time is the main value while flying; on the ground it says Ready from power-up, and Landed
    // once a flight has taken place since power-up (with the length of that flight).
    txt('t-flight',s.flying?fT(s.flightTime):s.flights>0?'Landed':'Ready');
    txt('t-ftime',s.flying?'Flying'+(s.logging?' · logging':''):s.flights>0&&s.flightTime?'last flight '+fT(s.flightTime):'');
    $('#b-approach').classList.toggle('on',s.mode=='approach');$('#b-emergency').classList.toggle('on',s.mode=='emergency');
    document.querySelectorAll('.b-mute').forEach(b=>{b.textContent=(s.muted?'Unmute vario':'Mute vario')+' [...]'});
    // Directions are arrows relative to the way the pilot is travelling (up = straight ahead) once the GPS
    // gives a direction of travel; the compass degrees move to the line under the value. Standing still
    // there is no direction of travel: the degrees stay as the value.
    const moving=!!G.moving;  // the vario decides (speed above the show limit for a while, below the hide limit for a while)
    setArrow('a-track',moving?G.track:null);  // ground speed tile: a compass; the arrow shows the track against absolute north (N at the top of the ring)
    txt('t-lzv',!s.lz.valid?'not set':s.lz.dist!=null?fD(s.lz.dist):'set');
    setArrow('a-lz',s.lz.valid&&s.lz.dist!=null&&moving&&s.lz.brg!=null?s.lz.brg-G.track:null);
    txt('t-lzs',s.lz.valid?fH(s.lz.elev)+' MSL':'');
    // The wind speed is the main value; the direction it blows from (and where the figure came from) is the line under the
    // windsock, which appears once there is a direction of travel to turn it against.
    if(!s.wind.valid){txt('t-wind','unknown');setArrow('a-wind',null);txt('t-winds','')}
    else{txt('t-wind',fS(s.wind.speed));setArrow('a-wind',moving?s.wind.from+180-G.track:null);txt('t-winds','from '+Math.round(s.wind.from)+'° · '+s.wind.src)}
    $('#wind-sockrow').hidden=$('#tile-wind .sockw').hidden;
    {const h=G.hdop,gq=!G.fix||!(h<=20)?'none':h<=2?'strong':h<=5?'good':h<=10?'ok':'poor';
    txt('t-gps',!G.link?'no data':gq);
    txt('t-gpss',G.link?G.sats+' satellites · '+(G.fix?'fix':'searching'):'')}
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
    const bm=s.batt.min!=null&&s.batt.min>0?(s.batt.min>=60?Math.floor(s.batt.min/60)+' h ':'')+(s.batt.min%60)+' min':null;
    const bs=s.batt.min!=null?(s.batt.min>=60?Math.floor(s.batt.min/60)+'h ':'')+(s.batt.min%60)+'m':null;  // short form for the tile: it must fit a 320 px wide phone
    txt('t-batt',s.batt.v<2.5?'USB':bs?'~'+bs:s.batt.pct+'%');txt('t-batts',s.batt.v<2.5?'':s.batt.v.toFixed(2)+' V · '+s.batt.pct+'%');
    setAlerts(s,G,hw,bm);
    if(s.mic){
      txt('t-rpm',!s.mic.ok?'no mic':s.mic.rpm>0?Math.round(s.mic.rpm)+' rpm':'off');
      txt('t-rpms',s.mic.ok&&cfg.showEngTiles?'est '+Math.round(s.mic.raw)+' · conf '+s.mic.conf.toFixed(1):'');
      txt('t-noise',s.mic.noise!=null?s.mic.noise.toFixed(0)+' dBFS':'–');
      txt('t-noises',s.mic.ok?'vario vol '+s.vol.vario+'%'+(replay&&s.vol.speech!=null?' · speech '+s.vol.speech+'%':''):'');
    }
    if(s.vol){txt('t-vol',s.vol.vario+' %');
      txt('t-vols','speech '+s.vol.speech+' %'+(!replay&&cfg.varioVolume!=null&&cfg.varioVolume!==s.vol.vario?' · set '+cfg.varioVolume+' %':''))}
    if(!replay){
    txt('fwver','Installed: '+(s.version||'unknown')+(s.git?' · git '+s.git:'')+(s.debug?' (debug build)':' (release build)'));
    txt('hver','v'+(s.version||'unknown')+(s.debug?' debug':''));
    txt('sys','Uptime '+fT(s.uptime)+' · heap '+Math.round(s.heap/1024)+' kB · BLE clients '+s.ble+' · baro '+(s.baro.ok?(s.baro.pressure/100).toFixed(1)+' hPa':'missing'));
    wifiInfo=s.wifi;showWifi();
    // At the bottom of Settings, only while a change that needs a restart is waiting (the vario says so, so it survives a page reload).
    $('#restartBar').hidden=!(s.restartNeeded||(s.wifi&&s.wifi.restart));
    }
  }
}

function syncInputs(f){
  const c=conv(f.u);
  document.querySelectorAll('[data-key="'+f.k+'"]').forEach(i=>{if(i===document.activeElement)return;
    if(f.t==='bool')i.checked=!!cfg[f.k];else if(f.t==='enum'||f.t==='str')i.value=cfg[f.k];
    else if(f.t!=='list')i.value=rnd(cfg[f.k]*c.f,f.t==='double'?7:f.t==='int'?0:c.d)});
}
// The + and - in the Volume tile change the vario volume and the speech volume setting together, 5 points a tap.
document.querySelectorAll('.volb button').forEach(b=>b.onclick=()=>{
  if(!schema.length)return;
  for(const k of ['varioVolume','speechVolume']){const f=schema.find(x=>x.k===k);if(f)setVal(f,Math.max(0,Math.min(100,(+cfg[k]||0)+(+b.dataset.d))))}
  if(cfg.varioVolume!=null)txt('t-vols','set '+cfg.varioVolume+' %')});
function setVal(f,v){
  if(cfg[f.k]===v)return;
  cfg[f.k]=v;saveRevision++;txt('saveState','Unsaved changes');syncInputs(f);
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
    i.onchange=()=>{setVal(f,i.checked);document.querySelectorAll('input[data-gate="'+f.k+'"]').forEach(x=>x.disabled=!i.checked);
      document.querySelectorAll('[data-showif="'+f.k+'"]').forEach(x=>x.hidden=!i.checked)}}
  else if(f.t==='enum'){i=document.createElement('select');f.o.split('|').forEach((o,n)=>i.add(new Option(o,n)));i.value=cfg[f.k];
    i.onchange=()=>{setVal(f,+i.value);if(/Unit$/.test(f.k))render()}}
  else if(f.t==='str'){i=document.createElement('input');i.type=/Pass$/.test(f.k)?'password':'text';i.maxLength=f.n;i.value=cfg[f.k];i.onchange=()=>setVal(f,i.value)}
  else if(f.t==='list'){i=document.createElement('input');i.type='text';
    // A list is typed with commas and spaces ("50, 100, 200"): the phone's decimal keypad has neither, so it gets the ordinary keyboard (on an
    // iPhone the comma and space are on its first page once the "123" key is used), with the correction features off so digits are not changed.
    i.inputMode='text';i.autocapitalize='off';i.autocomplete='off';i.spellcheck=false;i.setAttribute('autocorrect','off');i.placeholder='e.g. 50, 100, 200';
    i.value=String(cfg[f.k]).split(',').filter(x=>x).map(v=>Math.round(v*c.f)).join(', ');
    if(GATE[f.k]){i.dataset.gate=GATE[f.k];i.disabled=!cfg[GATE[f.k]]}
    i.onchange=()=>setVal(f,i.value.split(/[ ,]+/).map(parseFloat).filter(v=>!isNaN(v)&&v>0).map(v=>(v/c.f).toFixed(2)).join(','))}
  else{i=document.createElement('input');i.type='number';i.step='any';i.inputMode='decimal';
    const d=f.t==='double'?7:f.t==='int'?0:c.d;i.value=rnd(cfg[f.k]*c.f,d);
    i.onchange=()=>{const v=parseFloat(i.value);if(!isNaN(v))setVal(f,v/c.f)}}
  i.dataset.key=f.k;
  row.append(sp,i);return row;
}
// Settings that most pilots never need to change sit in one collapsed "Advanced settings" section at the
// end of the list, in this order.
const ADV_GROUPS=['Battery','Wi-Fi','Firmware Update','GPS','Engine RPM','Wind','Logging','Glide'];
function render(){
  $('#fwholder').appendChild($('#fwcard'));  // out of the list before it is cleared
  const root=$('#settings');root.innerHTML='';const groups={};
  for(const f of schema)(groups[f.g]=groups[f.g]||[]).push(f);
  const group=(g,parent,cls)=>{
    const d=document.createElement('details');d.className=cls;d.open=openGroups.has(g);
    d.ontoggle=()=>d.open?openGroups.add(g):openGroups.delete(g);
    const s=document.createElement('summary');s.textContent=g;d.appendChild(s);
    if(g==='Firmware Update')d.appendChild($('#fwcard'));
    if(g==='Wi-Fi')d.insertAdjacentHTML('beforeend','<div class="row"><span>Status</span><b id="w-ap">–</b></div>');
    // The maintenance connection (for the maker and testers) is a sub-section that exists only while its switch is on.
    const MAINT=['serverSsid','serverPass','serverGiveUpS'];
    groups[g].forEach(f=>{
      if(MAINT.includes(f.k))return;
      d.appendChild(field(f));
      if(f.k==='lzElevM'){  // fills the three above from where the vario is now (needs a GPS fix)
        const r=document.createElement('div');r.className='row';
        const b=document.createElement('button');b.type='button';b.id='b-lz-here';b.textContent='Use current position';b.onclick=()=>act('lz_here');
        r.appendChild(b);d.appendChild(r);
      }
      if(f.k==='maintWifi'){
        const m=document.createElement('div');m.className='maintbox';m.dataset.showif='maintWifi';m.hidden=!cfg.maintWifi;
        m.insertAdjacentHTML('beforeend','<div class="row"><span>Status</span><b id="w-server">–</b></div><div class="row"><span>Assigned IP address</span><b id="w-server-ip">–</b></div>');
        groups[g].filter(x=>MAINT.includes(x.k)).forEach(x=>m.appendChild(field(x)));
        m.insertAdjacentHTML('beforeend','<p class="mut">The vario also joins this Wi-Fi so a computer can reach it (http://paravario.local) and update its firmware.</p>');
        d.appendChild(m);
      }});
    parent.appendChild(d);return d};
  // Quick settings: the few a pilot changes most, first. They are the same settings as in their groups below (the
  // two copies follow each other).
  const QUICK=['varioVolume','speechVolume','heightUnit','distUnit','speedUnit','circuitSide','glideRatio','trimAirspeedMs'];
  const qf=QUICK.map(k=>schema.find(f=>f.k===k)).filter(Boolean);
  if(qf.length){
    const q=document.createElement('details');q.className='card';q.id='quickSettings';q.open=!openGroups.has('~quick closed');
    q.ontoggle=()=>q.open?openGroups.delete('~quick closed'):openGroups.add('~quick closed');
    const s=document.createElement('summary');s.textContent='Quick settings';q.appendChild(s);
    qf.forEach(f=>q.appendChild(field(f)));root.appendChild(q);
  }
  for(const g in groups)if(!ADV_GROUPS.includes(g))group(g,root,'card');
  const adv=ADV_GROUPS.filter(g=>groups[g]);
  if(adv.length){
    const o=document.createElement('details');o.className='card';o.id='advSettings';o.open=openGroups.has('Advanced settings');
    o.ontoggle=()=>o.open?openGroups.add('Advanced settings'):openGroups.delete('Advanced settings');
    const s=document.createElement('summary');s.textContent='Advanced settings';o.appendChild(s);
    const n=document.createElement('p');n.className='mut advnote';n.textContent='Settings most pilots never need to change: '+adv.join(', ')+'.';o.appendChild(n);
    adv.forEach(g=>group(g,o,'card sub'));root.appendChild(o);
  }
  showWifi();addGroupHelp();
}
let wifiInfo=null;
function showWifi(){
  const w=wifiInfo;if(!w||!document.getElementById('w-server'))return;
  txt('w-ap',w.apIp+' · '+w.apClients+' connected');
  const q=w.rssi>-60?'good':w.rssi>-70?'fair':'weak';
  let status=!cfg.maintWifi&&!w.maint?'Off':!w.serverSsid?'Off (not set)':w.server=='connected'?'Connected to '+w.serverSsid+' · signal '+q+' ('+w.rssi+' dBm)':
    w.server=='connecting'?'Connecting to '+w.serverSsid+'…':w.serverSsid+' – '+w.server;
  if(w.restart)status+=' · restart to apply changes';
  txt('w-server',status);
  txt('w-server-ip',w.serverIp?w.serverIp+' (paravario.local)':'–');
}
async function loadSettings(){
  const st=await fetch('/api/status',{cache:'no-store'}).then(r=>r.json()).catch(()=>null);
  [schema,cfg]=await Promise.all([fetch('/api/schema').then(r=>r.json()),fetch('/api/settings').then(r=>r.json())]);
  schemaKey=st&&st.hw?st.hw.gps+'/'+st.hw.mic:'';
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
  if(name==='area'){if(aWasFly&&aPos)aFollow=true;areaLoad();areaMapInit();if(aMap)setTimeout(()=>{aMap.invalidateSize();if(!aDraft)areaView()},50)}
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
  if(a==='reset_flight'&&!confirm('Forget the current flight? Use this if the vario thinks it is flying when it is not.'))return;
  try{const r=await fetch('/api/action?do='+a,{method:'POST'});const j=await r.json();if(!r.ok)throw j.error;
    toast(a==='restart'||a==='defaults'?'Restarting…':'Done');
    if(a.startsWith('lz_'))loadSettings();
  }catch(e){toast(a==='lz_here'&&e==='no GPS fix'?'No GPS fix':'Failed: '+e)}
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
      const rb=document.createElement('button');rb.textContent='Replay';rb.onclick=()=>replayStart(f.n);
      row.append(a,sz,rb,del);el.appendChild(row);
    }
    const p=document.createElement('p');p.className='mut';p.textContent=Math.round(j.free/1024)+' kB free: room for about '+Math.max(0,Math.round((j.free-61440)/50/60))+' more minutes of flight before the oldest logs are deleted';el.appendChild(p);
  }catch(e){$('#logs').textContent='Could not load logs'}
}

// ---- Replay: the Flight screen driven by a flight log. The log holds a fix every second (position, height, engine RPM) and the
// slow values every 5 s (wind, battery, satellites, noise, flags); ground speed, track, vario and glide are worked out here.
let rp=null;
function parseIgc(text){
  const d={fix:[],start:0,end:-1,gnd:null,lz:null,first:null};let slow=null,day=0,prev=-1;
  for(const raw of text.split('\n')){
    const l=raw.trim();
    if(l[0]==='B'&&l.length>=35){
      let t=+l.slice(1,3)*3600+ +l.slice(3,5)*60+ +l.slice(5,7);if(prev>=0&&t<prev-43200)day+=86400;prev=t;
      d.fix.push({t:t+day,lat:(+l.slice(7,9)+ +l.slice(9,14)/60000)*(l[14]==='S'?-1:1),lon:(+l.slice(15,18)+ +l.slice(18,23)/60000)*(l[23]==='W'?-1:1),
        ok:l[24]==='A',alt:+l.slice(25,30),rpm:l.length>=39?+l.slice(35,39):0,vv:l.length>=42?+l.slice(39,42):null,sv:l.length>=45?+l.slice(42,45):null,slow});
    }else if(l.startsWith('LXPVD,')){
      const q=l.slice(6).split(',').map(Number);slow={flags:q[0],wd:q[1],ws:q[2],bv:q[3],bp:q[4],bm:q[5],sats:q[6],hdop:q[7],noise:q[8],ss:q[9],vs:q[10]};
      if(!d.first)d.first=slow;if(d.fix.length)d.fix[d.fix.length-1].slow=slow;
    }else if(l.startsWith('LXPVGND,'))d.gnd=+l.slice(8);
    else if(l.startsWith('LXPVLZ,')&&!l.endsWith('none')){const q=l.slice(7).split(',').map(Number);d.lz={lat:q[0],lon:q[1],elev:q[2]}}
    else if(l.startsWith('LXPVVOL,')){const q=l.slice(8).split(',').map(Number);d.vol={quiet:q[0],loud:q[1],auto:q[2]===1,ramp:q[3]}}
    else if(l==='LXPVSTART')d.start=d.fix.length;
    else if(l==='LXPVEND')d.end=d.fix.length-1;
  }
  return d;
}
const rpDist=(a,b)=>Math.hypot((b.lat-a.lat)*111320,(b.lon-a.lon)*111320*Math.cos(a.lat*Math.PI/180));
const rpBrg=(a,b)=>(Math.atan2((b.lon-a.lon)*Math.cos(a.lat*Math.PI/180),b.lat-a.lat)*180/Math.PI+360)%360;
// Windows are in seconds, not fixes: stretches of a long flight may have been thinned to one fix in 5 or in 20 to make room.
const rpBack=(F,i,sec)=>{let j=i-1;while(j>0&&F[i].t-F[j].t<sec)j--;return Math.max(0,j)};  // the fix at least sec before i (or the one before it)
const rpAt=(F,t)=>{let lo=0,hi=F.length-1;while(lo<hi){const m=(lo+hi+1)>>1;if(F[m].t<=t)lo=m;else hi=m-1}return lo};  // the last fix at or before t
function rpSpeed(F,i){if(i<1)return 0;const j=rpBack(F,i,3);return rpDist(F[j],F[i])/Math.max(1,F[i].t-F[j].t)}
function rpTrack(F,i){for(let k=i;k>=1&&k>i-60;k--){const j=rpBack(F,k,3);if(rpDist(F[j],F[k])>3)return rpBrg(F[j],F[k])}return 0}
// "A direction of travel exists": the vario's hysteresis (show above one speed, hide below a lower one, each held), run over the
// seconds before this one so jumping the slider gives the same answer as playing up to it.
function rpMoving(F,i){
  const show=cfg.moveShowMs!=null?cfg.moveShowMs:2,hide=Math.min(cfg.moveHideMs!=null?cfg.moveHideMs:1,show),hold=cfg.moveHoldS!=null?cfg.moveHoldS:3;
  let on=false,since=0;
  for(let k=rpAt(F,F[i].t-40);k<=i;k++){
    if(!F[k].ok){on=false;since=0;continue}
    const v=rpSpeed(F,k),ch=on?v<hide:v>=show;
    if(!ch){since=0;continue}
    if(!since)since=F[k].t;if(F[k].t-since>=hold){on=!on;since=0}
  }
  return on;
}
function rpGlide(F,i,w){
  if(i<1)return null;const j=rpBack(F,i,w);
  const drop=F[j].alt-F[i].alt,dist=rpDist(F[j],F[i]);
  return drop>1&&F[i].t-F[j].t>=2?Math.min(99,dist/drop):null;
}
function replayStatus(i){
  const d=rp.d,F=d.fix,f=F[i],sl=f.slow||d.first||{flags:8,wd:-1,ws:-1,bv:0,bp:0,bm:-1,sats:0,hdop:99,noise:0};
  const gnd=d.gnd!=null?d.gnd:F[0].alt,flying=i>=d.start&&(d.end<0||i<d.end),landed=d.end>=0&&i>=d.end;
  const speed=rpSpeed(F,i),track=rpTrack(F,i),j=i>0?rpBack(F,i,5):0;
  const gs=cfg.glideShortS||3,gl=cfg.glideLongS||20;
  const s={hw:rp.hw||{gps:true,mic:true,spectrum:true},flying,flights:(flying||landed)?1:0,
    flightTime:flying?f.t-F[d.start].t:landed?F[d.end].t-F[d.start].t:0,
    agl:f.alt-gnd,altMsl:f.alt,vario:i>j?(f.alt-F[j].alt)/Math.max(1,f.t-F[j].t):0,muted:!!(sl.flags&2),
    gps:{link:true,fix:f.ok,sats:sl.sats,hdop:sl.hdop/10,speed,track,moving:rpMoving(F,i)},
    glide:{now:rpGlide(F,i,Math.max(3,gs)),avg:rpGlide(F,i,gl)},mode:'none',logging:false,
    lz:d.lz?{valid:true,dist:rpDist(f,d.lz),brg:rpBrg(f,d.lz),elev:d.lz.elev}:{valid:false},
    wind:sl.wd>=0?{valid:true,from:sl.wd,speed:sl.ws/10,src:'log'}:{valid:false},
    ready:true,notReady:'',minSats:cfg.readyMinSats||6,
    batt:{v:sl.bv/100,pct:sl.bp,min:sl.bm>=0?sl.bm:null},
    mic:{ok:!!(sl.flags&16),rpm:f.rpm,raw:f.rpm,conf:5,noise:-sl.noise},vol:{vario:f.vv!=null?f.vv:(cfg.varioVolume||0),speech:f.sv,varioSet:sl.vs,speechSet:sl.ss},
    baro:{ok:!!(sl.flags&8),pressure:101325},wifi:null};
  return s;
}
// The volume chart: the volume applied to the vario (solid) and to speech (dashed) over the whole flight, the volumes the
// settings asked for (thin lines), the volume the loudness adjustment wanted (dotted: the setting raised towards 100 % as the
// background noise rises from the quiet to the loud limit, before the speed limit on changes), and the background noise (grey);
// a vertical line marks where the replay is.
function rpVolText(i){
  const d=rp.d,F=d.fix,vol=d.vol||{quiet:-52,loud:-30,auto:true},slowAt=k=>F[k].slow||d.first;
  const f=F[i],sl=slowAt(i);
  if(f.vv!=null){
    const want=sl&&vol.auto?sl.vs+(100-sl.vs)*Math.max(0,Math.min(1,(-sl.noise-vol.quiet)/Math.max(1,vol.loud-vol.quiet))):(sl?sl.vs:null);
    txt('rpVolTxt','volume now: vario '+f.vv+'%'+(sl?' (set '+sl.vs+'%, wanted '+Math.round(want)+'%)':'')+' · speech '+f.sv+'%'+(sl?' (set '+sl.ss+'%)':'')+(sl?' · noise '+(-sl.noise)+' dBFS (quiet '+vol.quiet+', loud '+vol.loud+')':'')+' — solid vario, dashed speech, dotted wanted, grey noise');
  }else txt('rpVolTxt','this log has no volume record');
}
function rpVolChart(i){
  const c=$('#rpVol');if(!c||!c.clientWidth)return;
  const d=rp.d,F=d.fix,dpr=window.devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;
  if(c.width!==Math.round(W*dpr)||c.height!==Math.round(H*dpr)){c.width=Math.round(W*dpr);c.height=Math.round(H*dpr)}
  const g=c.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,W,H);
  const cs=getComputedStyle(document.body),acc=cs.getPropertyValue('--acc'),mut=cs.getPropertyValue('--mut'),bd=cs.getPropertyValue('--bd'),fg=cs.getPropertyValue('--fg');
  const n=F.length,tot=Math.max(1,F[n-1].t-F[0].t),X=k=>(F[Math.min(n-1,Math.max(0,k))].t-F[0].t)*(W-1)/tot,Y=v=>H-3-Math.max(0,Math.min(100,v))/100*(H-14);
  g.strokeStyle=bd;g.lineWidth=1;[0,50,100].forEach(v=>{g.beginPath();g.moveTo(0,Math.round(Y(v))+.5);g.lineTo(W,Math.round(Y(v))+.5);g.stroke()});
  const slowAt=k=>F[k].slow||d.first;
  // background noise as a grey area (louder = higher), scaled between the quiet and the loud limit
  const vol=d.vol||{quiet:-52,loud:-30,auto:true};
  const line=(fn,color,w,dash)=>{g.strokeStyle=color;g.lineWidth=w;g.setLineDash(dash||[]);g.beginPath();let st=false;
    for(let k=0;k<n;k++){const v=fn(k);if(v==null||isNaN(v)){st=false;continue}if(!st){g.moveTo(X(k),Y(v));st=true}else g.lineTo(X(k),Y(v))}g.stroke();g.setLineDash([])};
  g.globalAlpha=.18;g.fillStyle=mut;g.beginPath();g.moveTo(0,Y(0));
  for(let k=0;k<n;k++){const sl=slowAt(k);const nz=sl?-sl.noise:vol.quiet;g.lineTo(X(k),Y(100*(nz-vol.quiet)/Math.max(1,vol.loud-vol.quiet)))}
  g.lineTo(X(n-1),Y(0));g.closePath();g.fill();g.globalAlpha=1;
  line(k=>{const sl=slowAt(k);return sl?sl.vs:null},mut,1);
  line(k=>{const sl=slowAt(k);return sl?sl.ss:null},mut,1,[3,3]);
  line(k=>{const sl=slowAt(k);if(!sl||!vol.auto)return sl?sl.vs:null;const f=Math.max(0,Math.min(1,(-sl.noise-vol.quiet)/Math.max(1,vol.loud-vol.quiet)));return sl.vs+(100-sl.vs)*f},fg,1,[1,3]);
  line(k=>F[k].sv,acc,1.5,[5,3]);
  line(k=>F[k].vv,acc,2);
  g.strokeStyle=fg;g.lineWidth=1;g.beginPath();g.moveTo(Math.round(X(i))+.5,0);g.lineTo(Math.round(X(i))+.5,H);g.stroke();
}

function rpFrame(){
  const d=rp.d,F=d.fix,tot=F[F.length-1].t-F[0].t,i=rpAt(F,F[0].t+Math.max(0,Math.min(tot,rp.pos)));rp.i=i;
  try{
    renderStatus(replayStatus(i),true);
    const a=[],g0=d.gnd!=null?d.gnd:F[0].alt;for(let tt=Math.max(F[d.start].t,F[i].t-598);tt<=F[i].t;tt+=2)a.push(F[rpAt(F,tt)].alt-g0);
    hgData={step:2,a};drawHeight();
  }catch(e){}
  $('#rpSlider').value=Math.round(rp.pos);
  rpVolText(i);rpVolChart(i);
  const rel=F[i].t-F[0].t,tk=d.start>0?' · takeoff at '+fT(F[d.start].t-F[0].t):'';
  txt('rpTime',fT(rel)+' / '+fT(tot)+tk);
}
function rpTick(){
  if(!rp||!rp.playing)return;
  rp.pos+=rp.speed*0.25;
  const tot=rp.d.fix[rp.d.fix.length-1].t-rp.d.fix[0].t;
  if(rp.pos>=tot){rp.pos=tot;rp.playing=false;txt('rpPlay','Play')}
  rpFrame();
}
async function replayStart(name){
  try{
    const t=await fetch('/api/log?name='+encodeURIComponent(name)).then(r=>r.text());
    const d=parseIgc(t);if(d.fix.length<2){toast('Nothing to replay in '+name);return}
    if(rp)clearInterval(rp.timer);
    rp={name,d,pos:Math.max(0,d.fix[d.start].t-d.fix[0].t-5),i:0,playing:true,speed:1,hw:hwNow,timer:0};
    document.body.classList.add('replay');$('#replayBar').hidden=false;
    txt('rpName',name);$('#rpSlider').max=d.fix[d.fix.length-1].t-d.fix[0].t;txt('rpPlay','Pause');txt('rpSpeed','1×');
    selectTab('flight');rpFrame();rp.timer=setInterval(rpTick,250);
  }catch(e){toast('Could not load the log')}
}
function replayExit(){
  if(!rp)return;clearInterval(rp.timer);rp=null;
  document.body.classList.remove('replay');$('#replayBar').hidden=true;hgData=null;
  poll();pollHeight();
}
$('#rpPlay').onclick=()=>{if(!rp)return;if(!rp.playing&&rp.pos>=rp.d.fix[rp.d.fix.length-1].t-rp.d.fix[0].t)rp.pos=Math.max(0,rp.d.fix[rp.d.start].t-rp.d.fix[0].t-5);rp.playing=!rp.playing;txt('rpPlay',rp.playing?'Pause':'Play')};
$('#rpSpeed').onclick=()=>{if(!rp)return;rp.speed=rp.speed===1?4:rp.speed===4?16:1;txt('rpSpeed',rp.speed+'×')};
$('#rpSlider').oninput=()=>{if(!rp)return;rp.pos=+$('#rpSlider').value;rpFrame()};
$('#rpExit').onclick=replayExit;

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
// ---- Map tab: the areas are drawn on a map (Leaflet, loaded from the internet when this tab is first opened) ----
let aAreas=[],aDraft=null,aIdx=-1,aMap=null,aLayer=null,aMe=null,aTried=false,aPos=null,aFollow=false,aWasFly=false,aMeKey='';
const $a=id=>document.getElementById(id);
function areaStatus(s){
  const g=s.gps;aPos=g&&g.fix?[g.lat,g.lon]:null;
  const a=s.area;let t='';
  if(!a)t=aAreas.some(x=>x.on)?'No GPS fix yet.':'No area is set.';
  else{
    if(a.state==='inside')t='Inside the safe area, '+fD(a.edge)+' from the edge.';
    else if(a.state==='edge')t='Near the edge of the safe area: '+fD(a.edge)+'.';
    else if(a.state==='outside')t='Outside the safe area, '+fD(a.edge)+' from it.';
    if(a.rstate==='near')t+=' Restricted area ahead: '+fD(a.rdist)+'.';
    else if(a.rstate==='inside')t+=' In a restricted area.';
  }
  txt('areaState',t.trim());
  if(s.flying&&!aWasFly&&aPos){aFollow=true}  // taking off: the map follows the pilot until it is dragged
  aWasFly=!!s.flying;
  if(aMap&&aPos){
    const mv=!!(g&&g.moving),trk=mv?Math.round(g.track/5)*5:0,key=(mv?'a':'d')+trk;
    if(!aMe)aMe=L.marker(aPos,{icon:areaMeIcon(mv,trk),interactive:false,keyboard:false,zIndexOffset:1000}).addTo(aMap);
    else if(key!==aMeKey){const el=aMe.getElement();if(el)el.firstChild.innerHTML=areaMeSvg(mv,trk)}  // (setIcon and removing the marker both fail in Leaflet here)
    aMeKey=key;
    aMe.setLatLng(aPos);
    if(aFollow&&!aDraft)aMap.panTo(aPos,{animate:false});
  }
}
function areaMeSvg(moving,trk){  // an arrow along the track while moving, a dot when still
  return moving?'<path d="M14 2l9 23-9-5-9 5z" fill="#2a7fff" stroke="#fff" stroke-width="2" stroke-linejoin="round" transform="rotate('+trk+' 14 14)"/>':'<circle cx="14" cy="14" r="7" fill="#2a7fff" stroke="#fff" stroke-width="2.5"/>';
}
function areaMeIcon(moving,trk){
  return L.divIcon({className:'',iconSize:[28,28],iconAnchor:[14,14],html:'<svg width="28" height="28" viewBox="0 0 28 28" style="overflow:visible;filter:drop-shadow(0 0 2px #000)">'+areaMeSvg(moving,trk)+'</svg>'});
}
function areaView(){  // what the map shows when it opens or the areas change
  if(!aMap)return;
  if(aFollow&&aPos&&!aDraft)aMap.setView(aPos,Math.max(aMap.getZoom(),15));else areaFit();
}
function areaLoad(){return fetch('/api/areas',{cache:'no-store'}).then(r=>r.json()).then(j=>{aAreas=j.a||[];areaList();areaDraw();if(!aDraft)areaView()}).catch(()=>{})}
function areaPost(){
  const body=JSON.stringify({a:aAreas});
  if(body.length>5120){toast('Too many corners in all: at most about 180');areaLoad();return Promise.resolve(false)}
  return fetch('/api/areas',{method:'POST',body:body}).then(async r=>{const j=await r.json();if(!r.ok)throw j.error;toast('Saved');return true})
    .catch(e=>{toast('Not saved: '+e);areaLoad();return false});
}
const AC={safe:'#2a9d5c',restricted:'#e03131'};
const areaCol=a=>a.on?AC[a.k]||AC.safe:'#888';
let aWarnTouched=false;
function areaWarnSet(m){const c=conv('dist');$a('areaWarn').value=Math.round(m*c.f);$a('areaWarnU').textContent='('+c.s+')'}
function areaWarnM(){const c=conv('dist');return Math.max(0,Math.min(5000,Math.round((+$a('areaWarn').value||0)/c.f)))}
function areaKindShow(user){
  const k=$a('areaKind').value;
  if(user===true&&!aWarnTouched)areaWarnSet(k==='restricted'?500:0);
  $a('areaWarnNote').textContent=k==='restricted'?'How far ahead along your track you are warned before entering the area, with the way to turn. 0 = only when you are inside it.':'0 = you are told only when you leave the area. More = you are also told when you are that close to its edge.';
  areaDraw();
}
function areaFormReset(){$a('areaName').value='';$a('areaKind').value='safe';aWarnTouched=false;areaWarnSet(0);$a('areaTitle').textContent='New area';areaKindShow()}
function areaBtn(t,f){const b=document.createElement('button');b.type='button';b.textContent=t;b.onclick=f;b.style.padding='6px 10px';return b}
function areaList(){
  const el=$a('areaList');el.textContent='';
  aAreas.forEach((a,i)=>{
    const d=document.createElement('div');d.className='log';
    const lab=document.createElement('label'),cb=document.createElement('input');cb.type='checkbox';cb.checked=!!a.on;
    cb.onchange=()=>{a.on=cb.checked;areaPost().then(areaDraw)};
    const chip=document.createElement('span');chip.style.cssText='display:inline-block;width:10px;height:10px;border-radius:2px;margin:0 2px 0 6px;background:'+areaCol(a);
    const inf=[a.k==='restricted'?'restricted':'',a.w>0?'warn '+fD(a.w):''].filter(x=>x).join(', ');
    lab.append(cb,chip,' '+a.n+(inf?' ('+inf+')':''));
    const bs=document.createElement('span');
    bs.append(areaBtn('Edit',()=>areaEdit(i)),' ',areaBtn('Copy',()=>areaCopy(i)),' ',areaBtn('Delete',()=>{if(confirm('Delete '+a.n+'?')){aAreas.splice(i,1);if(aIdx===i)areaCancel();areaPost().then(areaLoad)}}));
    d.append(lab,bs);el.append(d);
  });
  if(aAreas.length<25){const p=document.createElement('p');p.append(areaBtn('New area',()=>areaEdit(-1)));el.append(p)}
}
function areaCopy(i){
  if(aAreas.length>=25){toast('At most 25 areas');return}
  const a=aAreas[i];
  aAreas.push({n:(a.n+' copy').slice(0,23),on:a.on,k:a.k||'safe',w:a.w||0,p:a.p.map(p=>p.slice())});
  areaPost().then(areaLoad);
}
function areaEdit(i){
  aIdx=i;aDraft=i>=0?aAreas[i].p.map(p=>p.slice()):[];aPtsN=aDraft.length;
  $a('areaName').value=i>=0?aAreas[i].n:'';$a('areaTitle').textContent=i>=0?'Edit '+aAreas[i].n:'New area';
  $a('areaKind').value=i>=0&&aAreas[i].k==='restricted'?'restricted':'safe';
  aWarnTouched=i>=0;areaWarnSet(i>=0?aAreas[i].w||0:0);areaKindShow();
  areaFit();$a('areaTitle').scrollIntoView({behavior:'smooth'});
}
function areaCancel(){aDraft=null;aIdx=-1;areaFormReset()}
let aPtsN=0;
function areaPoints(){
  const el=$a('areaPts'),top=el.scrollTop;el.textContent='';
  if(!aDraft){aPtsN=0;return}
  const mv=(i,j)=>{const t=aDraft[i];aDraft[i]=aDraft[j];aDraft[j]=t;areaDraw()};
  aDraft.forEach((p,i)=>{
    const r=document.createElement('div');r.className='apt';
    const n=document.createElement('span');n.className='n';n.textContent=i+1;
    const c=document.createElement('span');c.className='c';c.textContent=p[0].toFixed(5)+', '+p[1].toFixed(5);
    c.onclick=()=>{if(aMap)aMap.panTo(p)};
    const up=areaBtn('\u2191',()=>mv(i,i-1)),dn=areaBtn('\u2193',()=>mv(i,i+1)),rm=areaBtn('Delete',()=>{aDraft.splice(i,1);areaDraw()});
    up.disabled=i===0;dn.disabled=i===aDraft.length-1;
    up.setAttribute('aria-label','Move corner '+(i+1)+' up');dn.setAttribute('aria-label','Move corner '+(i+1)+' down');
    r.append(n,c,up,dn,rm);el.append(r);
  });
  el.scrollTop=aDraft.length>aPtsN?el.scrollHeight:top;aPtsN=aDraft.length;
}
function areaDraw(){
  areaPoints();
  if(aDraft)$a('areaTitle').textContent=(aIdx>=0?'Edit '+aAreas[aIdx].n:'New area')+' – '+aDraft.length+' corner'+(aDraft.length===1?'':'s');
  if(!aMap)return;
  aLayer.clearLayers();
  aAreas.forEach((a,i)=>{if(i===aIdx&&aDraft)return;L.polygon(a.p,{color:areaCol(a),weight:2,fillOpacity:.12}).bindTooltip(a.n).addTo(aLayer)});
  if(!aDraft)return;
  const kc=AC[$a('areaKind').value]||AC.safe;
  if(aDraft.length>=3)L.polygon(aDraft,{color:kc,weight:3,dashArray:'6 6',fillColor:kc,fillOpacity:.15}).addTo(aLayer);
  else if(aDraft.length===2)L.polyline(aDraft,{color:kc,weight:3,dashArray:'6 6'}).addTo(aLayer);
  aDraft.forEach((p,i)=>{
    const m=L.marker(p,{draggable:true,icon:L.divIcon({className:'',html:'<div class="acn">'+(i+1)+'</div>',iconSize:[22,22],iconAnchor:[11,11]})});
    m.on('dragend',()=>{const l=m.getLatLng();aDraft[i]=[l.lat,l.lng];areaDraw()});
    m.addTo(aLayer);
  });
}
function areaFit(){
  if(!aMap)return;
  const pts=aDraft&&aDraft.length?aDraft:aAreas.reduce((a,x)=>a.concat(x.p),[]);
  if(pts.length)aMap.fitBounds(L.latLngBounds(pts),{padding:[30,30],maxZoom:17});
  else if(aPos)aMap.setView(aPos,15);else aMap.setView([20,0],2);
}
function areaMapInit(){
  if(aMap||aTried)return;
  aTried=true;
  const m=$a('areaMapMsg');m.textContent='Loading the map…';
  const l=document.createElement('link');l.rel='stylesheet';l.href='https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/leaflet.min.css';document.head.append(l);
  const s=document.createElement('script');s.src='https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/leaflet.min.js';
  s.onload=areaMapStart;
  s.onerror=()=>{aTried=false;m.textContent='The map could not be loaded: this phone needs internet (mobile data, or another Wi-Fi). You can still paste corners below.'};
  document.head.append(s);
}
function areaMapStart(){
  $a('areaMapMsg').hidden=true;$a('areaMap').hidden=false;
  aMap=L.map('areaMap');
  const street=L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19,attribution:'© OpenStreetMap contributors'});
  const sat=L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}',{maxZoom:19,attribution:'Imagery © Esri'});
  street.addTo(aMap);L.control.layers({Map:street,Satellite:sat}).addTo(aMap);
  aLayer=L.layerGroup().addTo(aMap);
  aMeKey='';
  aMap.on('click',e=>{if(!aDraft)areaEdit(-1);aDraft.push([e.latlng.lat,e.latlng.lng]);areaDraw()});
  areaDraw();
  aMap.on('dragstart',()=>{if(aFollow){aFollow=false}});
  areaView();
}
$a('areaUndo').onclick=()=>{if(aDraft&&aDraft.length){aDraft.pop();areaDraw()}};
$a('areaCancel').onclick=areaCancel;
$a('areaKind').onchange=()=>areaKindShow(true);
$a('areaWarn').oninput=()=>{aWarnTouched=true};
areaFormReset();
$a('areaSave').onclick=async()=>{
  if(!aDraft||aDraft.length<3){toast('An area needs at least 3 corners');return}
  const n=($a('areaName').value.trim()||'Area '+(aAreas.length+(aIdx<0?1:0))).slice(0,23);
  const e={n:n,on:aIdx>=0?aAreas[aIdx].on:true,k:$a('areaKind').value,w:areaWarnM(),p:aDraft.map(p=>[+p[0].toFixed(6),+p[1].toFixed(6)])};
  if(aIdx>=0)aAreas[aIdx]=e;else aAreas.push(e);
  if(await areaPost()){aDraft=null;aIdx=-1;areaFormReset();areaLoad()}
};
$a('areaUse').onclick=()=>{
  const t=$a('areaPaste').value,nums=(t.match(/-?\d+(?:\.\d+)?/g)||[]).map(Number);
  if(nums.length<6||nums.length%2){toast('Need at least 3 pairs of numbers');return}
  const geo=t.includes('[');  // geojson order is longitude, latitude
  let p=[];for(let i=0;i<nums.length;i+=2)p.push(geo?[nums[i+1],nums[i]]:[nums[i],nums[i+1]]);
  if(p.length>1&&p[0][0]===p[p.length-1][0]&&p[0][1]===p[p.length-1][1])p.pop();
  if(p.length>24){toast('At most 24 corners');return}
  if(p.some(q=>Math.abs(q[0])>90||Math.abs(q[1])>180)){toast('These are not latitudes and longitudes');return}
  if(!aDraft)areaEdit(-1);
  aDraft=p;areaDraw();areaFit();toast(p.length+' corners');
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
