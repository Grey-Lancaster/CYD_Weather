#pragma once

#include <pgmspace.h>

// Settings portal single-page app, served from flash. No external assets —
// works in AP setup mode with no internet access.
//
// Form contract: every input/select `name` equals its AppSettings JSON key.
// The JS below reads/writes generically by element type (checkbox -> bool,
// number/range/select -> numeric, else -> string), so adding a new setting
// needs no JS changes — see WebPortal.h for the full checklist.
static const char WEB_INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD Weather</title>
<style>
:root{color-scheme:dark}
body{font-family:system-ui,sans-serif;background:#12161c;color:#e8eaed;margin:0 auto;padding:1rem;max-width:640px}
h1{font-size:1.3rem}h1 small{color:#96aabe;font-weight:normal}
h2{font-size:1rem;margin-top:0;color:#5aa0ff}
.card{background:#1c232e;border:1px solid #2c3644;border-radius:8px;padding:1rem;margin-bottom:1rem}
.row{display:flex;gap:.6rem}
.row>div{flex:1}
label{display:block;margin:.6rem 0 .2rem;font-size:.85rem;color:#96aabe}
input,select{width:100%;box-sizing:border-box;padding:.45rem;border-radius:6px;border:1px solid #2c3644;background:#12161c;color:#e8eaed}
input[type=range]{padding:0}
input[type=checkbox]{width:auto;margin-top:.5rem}
.check{display:flex;align-items:center;gap:.5rem}
.check label{margin:0}
.hint{margin:.15rem 0 0;font-size:.78rem;color:#6b7d90}
button,a.btn{padding:.5rem 1rem;border:0;border-radius:6px;background:#3b82f6;color:#fff;margin:.8rem .5rem 0 0;cursor:pointer}
a.btn{display:inline-block;text-decoration:none;box-sizing:border-box}
dl{display:grid;grid-template-columns:auto 1fr;gap:.25rem .75rem;margin:0}
dt{color:#96aabe}dd{margin:0}
#msg{margin-top:.6rem;font-size:.9rem;color:#50c878}
</style>
</head>
<body>
<h1>CYD Weather <small id="ver"></small></h1>
<div class="card">
<h2>Status</h2>
<dl>
<dt>WiFi</dt><dd id="st-wifi">&ndash;</dd>
<dt>IP</dt><dd id="st-ip">&ndash;</dd>
<dt>Signal</dt><dd id="st-rssi">&ndash;</dd>
<dt>Uptime</dt><dd id="st-up">&ndash;</dd>
<dt>Last restart</dt><dd id="st-rs">&ndash;</dd>
<dt>Free heap</dt><dd id="st-heap">&ndash;</dd>
<dt>Weather source</dt><dd id="st-wxsrc">&ndash;</dd>
<dt>Weather updated</dt><dd id="st-wxupd">&ndash;</dd>
</dl>
</div>
<form id="f">
<div class="card">
<h2>Network</h2>
<!-- WiFi credentials are set on-device via the touchscreen picker (see
     network/WifiProvisioning.h) — not here, and not in this settings blob
     at all, so there's exactly one place they can live. -->
<label>Hostname</label><input name="hostname" maxlength="23">
<label>NTP server</label><input name="ntpServer" maxlength="47" placeholder="pool.ntp.org">
</div>
<div class="card">
<h2>Location &amp; Weather</h2>
<label>Location name</label><input name="locationName" maxlength="39" placeholder="e.g. Home">
<div class="row">
<div><label>Latitude</label><input name="latitude" type="number" step="0.000001" min="-90" max="90"></div>
<div><label>Longitude</label><input name="longitude" type="number" step="0.000001" min="-180" max="180"></div>
</div>
<label>Timezone</label>
<!-- Values are POSIX TZ rule strings (offset + DST rule), not IANA names —
     same curated list/format as this project's sibling Claude_usage and
     TouchWifiProvisioner/CYD_RollingClock projects, sourced from
     https://github.com/nayarsystems/posix_tz_db. A free-text field here
     previously let "UTC-4" (POSIX's inverted sign convention: that string
     actually means 4 hours *ahead* of UTC) silently produce a clock 8 hours
     off — a dropdown of known-correct values removes that whole class of
     mistake, and bakes in each zone's DST rule so it doesn't drift wrong
     again at the next DST transition. -->
<select name="timezone">
<option value="HST10">Hawaii (US)</option>
<option value="AKST9AKDT,M3.2.0,M11.1.0">Alaska (US)</option>
<option value="PST8PDT,M3.2.0,M11.1.0">Pacific (US)</option>
<option value="MST7MDT,M3.2.0,M11.1.0">Mountain (US)</option>
<option value="CST6CDT,M3.2.0,M11.1.0">Central (US)</option>
<option value="EST5EDT,M3.2.0,M11.1.0">Eastern (US)</option>
<option value="AST4ADT,M3.2.0,M11.1.0">Atlantic Canada</option>
<option value="CST6">Mexico City</option>
<option value="&lt;-05&gt;5">Bogota</option>
<option value="&lt;-03&gt;3">Sao Paulo (Brazil)</option>
<option value="&lt;-03&gt;3">Buenos Aires</option>
<option value="UTC0">UTC</option>
<option value="GMT0BST,M3.5.0/1,M10.5.0">London (UK)</option>
<option value="CET-1CEST,M3.5.0,M10.5.0/3">Western Europe</option>
<option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Eastern Europe</option>
<option value="MSK-3">Moscow</option>
<option value="&lt;+03&gt;-3">Istanbul</option>
<option value="EET-2EEST,M4.5.5/0,M10.5.4/24">Cairo</option>
<option value="SAST-2">Johannesburg</option>
<option value="WAT-1">Lagos</option>
<option value="&lt;+04&gt;-4">Dubai</option>
<option value="IST-2IDT,M3.4.4/26,M10.5.0">Tel Aviv</option>
<option value="PKT-5">Karachi</option>
<option value="IST-5:30">India</option>
<option value="&lt;+06&gt;-6">Dhaka</option>
<option value="&lt;+07&gt;-7">Bangkok</option>
<option value="&lt;+08&gt;-8">Singapore</option>
<option value="HKT-8">Hong Kong</option>
<option value="CST-8">China</option>
<option value="JST-9">Japan</option>
<option value="KST-9">Korea</option>
<option value="AEST-10AEDT,M10.1.0,M4.1.0/3">Sydney (Australia)</option>
<option value="AWST-8">Perth (Australia)</option>
<option value="NZST-12NZDT,M9.5.0,M4.1.0/3">Auckland (NZ)</option>
</select>
<div class="check"><input name="hour12" type="checkbox" id="hr12"><label for="hr12">12-hour clock (AM/PM)</label></div>
<div class="row">
<div><label>Temperature units</label>
<select name="tempUnit"><option value="0">Celsius</option><option value="1">Fahrenheit</option></select></div>
<div><label>Wind units</label>
<select name="windUnit"><option value="0">km/h</option><option value="1">mph</option><option value="2">m/s</option><option value="3">knots</option></select></div>
</div>
<label>Pressure units</label>
<select name="pressureUnit"><option value="0">hPa</option><option value="1">inHg</option><option value="2">mmHg</option></select>
<label>Weather data source</label>
<!-- weatherProvider is the one setting NOT bound generically by name (see
     the form-contract note up top) — it's split across two selects with no
     name attribute so the plain load/submit loop below skips them, and
     wpSync()/load()/the submit handler stitch the pair back into a single
     numeric weatherProvider value. -->
<select id="wpMain"><option value="0">Open-Meteo (Auto)</option><option value="1">Open-Meteo (UK Met Office)</option><option value="2">MET Norway (direct)</option><option value="3">Met Office DataHub (UK)</option><option value="4">Pirate Weather</option><option value="other">Open-Meteo (other country model)&hellip;</option></select>
<div id="wpOtherWrap" style="display:none">
<label>Country model</label>
<select id="wpOther"><option value="5">Germany (DWD ICON)</option><option value="6">USA (NOAA GFS)</option><option value="7">France (M&eacute;t&eacute;o-France)</option><option value="8">ECMWF</option><option value="9">South Korea (KMA)</option><option value="10">Japan (JMA)</option><option value="11">Switzerland (MeteoSwiss)</option><option value="12">Norway (MET, via Open-Meteo)</option><option value="13">Canada (GEM)</option><option value="14">Australia (BOM)</option><option value="15">China (CMA)</option><option value="16">Netherlands (KNMI)</option><option value="17">Denmark (DMI)</option><option value="18">Italy (ItaliaMeteo)</option><option value="19">Austria (GeoSphere)</option></select>
</div>
<label>Met Office DataHub API key (only needed for that data source)</label>
<input name="metOfficeApiKey" type="password" maxlength="2047" autocomplete="off" placeholder="free at datahub.metoffice.gov.uk">
<label>Pirate Weather API key (only needed for that data source)</label>
<input name="pirateWeatherApiKey" type="password" maxlength="63" autocomplete="off" placeholder="free at pirateweather.net">
<label>Weather refresh interval (seconds)</label><input name="weatherRefreshS" type="number" min="60" max="86400">
</div>
<div class="card">
<h2>Display</h2>
<label>Brightness (<span id="bv"></span>)</label>
<input name="brightness" type="range" min="5" max="255" oninput="bv.textContent=this.value">
<label>Dimmed brightness (0&ndash;255)</label><input name="dimBrightness" type="number" min="0" max="255">
<label>Dim timeout (seconds, 0 = never)</label><input name="dimTimeoutS" type="number" min="0" max="65535">
<label>Theme</label>
<select name="theme"><option value="0">Dark</option><option value="1">Light</option><option value="2">Navy</option><option value="3">Midnight Blue</option><option value="4">Slate Green</option><option value="5">Violet Dusk</option><option value="6">Mahogany</option><option value="7">Carbon Red</option><option value="8">Black Red</option><option value="9">Mauve</option></select>
<div class="check"><input name="autoPageRotation" type="checkbox" id="apr"><label for="apr">Auto page rotation</label></div>
<label>Page duration (seconds)</label><input name="pageDurationS" type="number" min="3" max="3600">
<label>Hourly forecast scroll speed (seconds)</label><input name="hourlyShiftS" type="number" min="1" max="30">
<label>Colour order</label>
<select name="colorOrder"><option value="0">RGB</option><option value="1">BGR</option></select>
<label>Screen rotation</label>
<select name="displayFlipped"><option value="0">Normal (USB right)</option><option value="1">Flipped 180&deg; (USB left)</option></select>
<div class="check" id="rot90Row"><input name="rotate90" type="checkbox" id="rot90"><label for="rot90">Rotate display 90&deg;</label></div>
<p class="hint">Enable this if the screen shows portrait and cropped on first start.</p>
<div class="check"><input name="mirrorH" type="checkbox" id="mirh"><label for="mirh">Mirror display</label></div>
<p class="hint">Enable this if the screen shows text and images left-right reversed, as on some CYD panel variants.</p>
<div class="check"><input name="invertColors" type="checkbox" id="inv"><label for="inv">Invert display colours</label></div>
<p class="hint">Enable this if colours appear as their negative/inverse, as on some CYD panel variants.</p>
<div class="check"><input name="swapNavigation" type="checkbox" id="swnav"><label for="swnav">Swap left/right page navigation</label></div>
<p class="hint">Enable this if swipe changes pages in the wrong direction, as on some touch controller variants.</p>
</div>
<div class="card">
<h2>System</h2>
<label>Log level</label>
<select name="logLevel">
<option value="0">None</option><option value="1">Error</option><option value="2">Warn</option>
<option value="3">Info</option><option value="4">Debug</option>
</select>
<button type="submit">Save</button>
<button type="button" onclick="armConfirm(this,restartNow)">Restart</button>
<button type="button" onclick="window.open('/mirror','_blank')">Screenshot</button>
<button type="button" onclick="window.open('/touch','_blank')">Touch test</button>
<button type="button" id="recalBtn" style="display:none" onclick="armConfirm(this,recalNow)">Recalibrate touch</button>
<div id="msg"></div>
</div>
</form>
<div class="card">
<h2>Backup &amp; Restore</h2>
<p class="hint">Backups include your WiFi password and any weather-provider API keys in plain text — store the file somewhere private.</p>
<a class="btn" href="/api/settings/backup" download="cyd-weather-settings.json">Download backup</a>
<button type="button" onclick="el('restoreFile').click()">Restore from file&hellip;</button>
<input type="file" id="restoreFile" accept="application/json" style="display:none">
<div id="sdRow" style="display:none">
<button type="button" onclick="sdBackup()">Save to SD card</button>
<button type="button" onclick="armConfirm(this,sdRestore)">Restore from SD card</button>
</div>
</div>
<div class="card">
<h2>Firmware update</h2>
<p class="hint">Choose the plain <b>CYD-Weather-&lt;board&gt;-firmware.bin</b> for your board from the GitHub release (not the <i>-factory</i> file). The device restarts when the upload finishes; settings and WiFi are kept.</p>
<input type="file" id="fwFile" accept=".bin">
<button type="button" onclick="armConfirm(this,fwUpdate)">Update</button>
<div id="fwMsg" class="hint"></div>
</div>
<script>
function fwUpdate(){
const f=el('fwFile').files[0],m=el('fwMsg');
if(!f){m.textContent='Choose a firmware .bin first.';return}
const x=new XMLHttpRequest(),d=new FormData();d.append('firmware',f,f.name);
x.upload.onprogress=e=>{if(e.lengthComputable)m.textContent='Uploading '+Math.round(e.loaded*100/e.total)+'%'};
x.onload=()=>{if(x.status===200){m.textContent='Done. Restarting...';setTimeout(()=>location.reload(),15000)}else{let t=x.responseText;try{t=JSON.parse(t).error}catch(e){}m.textContent='Update failed: '+t}};
x.onerror=()=>{m.textContent='Connection lost (the device may already be restarting).'};
x.open('POST','/update');x.send(d);
}
async function j(u,o){const r=await fetch(u,o);if(!r.ok)throw new Error('HTTP '+r.status);return r.json()}
function el(i){return document.getElementById(i)}
function setVal(e,v){if(e.type==='checkbox'){e.checked=!!v}else{e.value=v}}
function getVal(e){
if(e.type==='checkbox')return e.checked;
if(e.type==='number'||e.type==='range')return +e.value;
// SELECTs are numeric-valued (theme, units, ...) except timezone, whose
// option values are POSIX TZ rule strings (e.g. "EST5EDT,M3.2.0,M11.1.0") —
// +value on those would come out NaN, so only coerce when it actually parses.
if(e.tagName==='SELECT'){const n=+e.value;return isNaN(n)?e.value:n}
return e.value}
async function refresh(){try{
const s=await j('/api/status');
el('ver').textContent='v'+s.version;
el('st-wifi').textContent=s.wifi.state+(s.wifi.ssid?' ('+s.wifi.ssid+')':'');
el('st-ip').textContent=s.wifi.ip||'-';
el('st-rssi').textContent=s.wifi.rssi?s.wifi.rssi+' dBm':'-';
el('st-up').textContent=s.uptime;
el('st-rs').textContent=s.lastRestart||'-';
el('recalBtn').style.display=s.recalSupported?'':'none';
{const h=s.rotate90Supported===false?'none':'';el('rot90Row').style.display=h;el('rot90Row').nextElementSibling.style.display=h}
el('st-heap').textContent=(s.heapFree/1024).toFixed(1)+' KB';
el('st-wxsrc').textContent=s.weather.provider;
el('st-wxupd').textContent=s.weather.valid?s.weather.lastUpdate+(s.weather.failed?' (last fetch failed, retrying)':''):(s.weather.failed?'Fetch failed':'Fetching…');
}catch(e){}}
function wpSync(){el('wpOtherWrap').style.display=el('wpMain').value==='other'?'':'none'}
el('wpMain').addEventListener('change',wpSync);
async function load(){try{
const s=await j('/api/settings');const f=el('f');
for(const k in s){if(f.elements[k])setVal(f.elements[k],s[k])}
el('bv').textContent=f.elements.brightness.value;
// weatherProvider >= 5 is one of the "other country" models (see WeatherUnits.h) — reflect it in wpOther and flip wpMain to the "other" placeholder.
if(s.weatherProvider>=5){el('wpMain').value='other';el('wpOther').value=s.weatherProvider}else{el('wpMain').value=s.weatherProvider}
wpSync();
}catch(e){}}
let msgTimer=null;
function flashMsg(text,ms){el('msg').textContent=text;clearTimeout(msgTimer);if(ms)msgTimer=setTimeout(()=>{el('msg').textContent=''},ms)}
el('f').addEventListener('submit',async ev=>{
ev.preventDefault();const f=ev.target,b={};
for(const e of f.elements){if(!e.name)continue;b[e.name]=getVal(e)}
b.weatherProvider=el('wpMain').value==='other'?+el('wpOther').value:+el('wpMain').value;
const now=new Date().toLocaleTimeString();
try{const r=await j('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});
if(r.restartRequired)flashMsg('Saved at '+now+' - restart required to apply hostname',0);
// weatherProvider changes trigger an immediate re-fetch (not the usual
// refresh-interval wait) — point at the Weather updated/source rows above
// rather than just claiming success, since "Saved" alone doesn't say
// whether the new source has actually loaded yet.
else if('weatherProvider' in b)flashMsg('Saved at '+now+' - watch "Weather source/updated" above for the new data',6000);
else flashMsg('Saved at '+now,4000);
refresh();
}catch(e){flashMsg('Save failed: '+e.message,0)}});
// window.confirm()/alert() are unreliable inside a phone OS's captive-portal
// mini-browser (iOS Captive Network Assistant, Android's captive sign-in
// webview) — some silently block them outright, which looks like the button
// just doing nothing. This tap-twice pattern needs nothing but plain DOM/JS,
// so it works there too. 3s window to tap again, then it disarms itself.
function armConfirm(btn,action){
if(btn.dataset.armed){clearTimeout(+btn.dataset.timer);btn.textContent=btn.dataset.label;delete btn.dataset.armed;action();return}
btn.dataset.label=btn.textContent;btn.dataset.armed='1';btn.textContent='Tap again to confirm';
btn.dataset.timer=setTimeout(()=>{btn.textContent=btn.dataset.label;delete btn.dataset.armed},3000);
}
async function recalNow(){try{await fetch('/api/recal',{method:'POST'})}catch(e){}flashMsg('Restarting - touch the corners shown on the screen',0)}
async function restartNow(){try{await fetch('/api/restart',{method:'POST'})}catch(e){}flashMsg('Restarting…',0)}
el('restoreFile').addEventListener('change',async ev=>{
const file=ev.target.files[0];ev.target.value='';if(!file)return;
try{const text=await file.text();
await j('/api/settings/restore',{method:'POST',headers:{'Content-Type':'application/json'},body:text});
flashMsg('Restoring… device will restart',0);
}catch(e){flashMsg('Restore failed: '+e.message,0)}});
async function sdBackup(){try{
await j('/api/sdcard/backup',{method:'POST'});flashMsg('Saved to SD card',4000);
}catch(e){flashMsg('SD backup failed: '+e.message,0)}}
async function sdRestore(){
try{await j('/api/sdcard/restore',{method:'POST'});flashMsg('Restoring… device will restart',0);
}catch(e){flashMsg('SD restore failed: '+e.message,0)}}
async function checkSdCard(){try{
const s=await j('/api/sdcard/status');el('sdRow').style.display=s.present?'':'none';
}catch(e){}}
load();refresh();checkSdCard();setInterval(refresh,5000);
</script>
</body>
</html>
)HTML";

// Screen-mirror viewer — a live-ish snapshot of the TFT for showing someone
// else what's on the device without a camera. Reachable at GET /mirror; the
// actual image comes from GET /mirror.bmp (see WebPortal::handleMirrorImage,
// which reads the panel back over SPI row-by-row — no framebuffer exists to
// read from directly, see DisplayManager's class comment).
static const char WEB_MIRROR_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD Weather — Screen Mirror</title>
<style>
:root{color-scheme:dark}
body{font-family:system-ui,sans-serif;background:#12161c;color:#e8eaed;margin:0 auto;padding:1rem;max-width:480px;text-align:center}
h1{font-size:1.1rem;margin-bottom:.2rem}
img{width:100%;border:1px solid #2c3644;border-radius:6px;image-rendering:pixelated;margin-top:.6rem}
.row{display:flex;gap:.6rem;justify-content:center;align-items:center;margin-top:.6rem}
button{padding:.5rem 1rem;border-radius:6px;border:1px solid #2c3644;background:#1c232e;color:#e8eaed;font-size:1rem}
label{font-size:.85rem;color:#96aabe;display:flex;align-items:center;gap:.4rem}
</style>
</head>
<body>
<h1>CYD Weather — Screen Mirror</h1>
<div class="row">
<button onclick="refresh()">Refresh</button>
<button onclick="save()">Save PNG</button>
<label><input type="checkbox" id="auto" onchange="toggleAuto()"> Auto-refresh (2s)</label>
</div>
<img id="shot" src="/mirror.bmp" alt="device screen">
<script>
function refresh(){document.getElementById('shot').src='/mirror.bmp?'+Date.now()}
function save(){
const i=document.getElementById('shot'),c=document.createElement('canvas');
c.width=i.naturalWidth;c.height=i.naturalHeight;c.getContext('2d').drawImage(i,0,0);
c.toBlob(b=>{const a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='cyd-weather.png';a.click()},'image/png');
}
let timer=null;
function toggleAuto(){
const on=document.getElementById('auto').checked;
if(timer){clearInterval(timer);timer=null}
if(on)timer=setInterval(refresh,2000);
}
</script>
</body>
</html>
)HTML";

// Touch test: shows where the board thinks each tap landed, drawn on a copy of the
// dashboard area (the numbers are in dashboard pixels, 0,0 = top-left). For checking
// touch alignment after changing orientation settings; polls GET /api/touch.
static const char WEB_TOUCH_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD Weather - Touch Test</title>
<style>
:root{color-scheme:dark}
body{font-family:system-ui,sans-serif;background:#12161c;color:#e8eaed;margin:0 auto;padding:1rem;max-width:480px;text-align:center}
h1{font-size:1.1rem;margin-bottom:.2rem}
canvas{width:100%;max-width:480px;border:1px solid #2c3644;border-radius:6px;background:#0b0f14;margin-top:.6rem}
p{font-size:.9rem;color:#96aabe}
#num{font-family:ui-monospace,monospace;color:#e8eaed;min-height:2.6em}
</style>
</head>
<body>
<h1>Touch test</h1>
<p>Tap the screen of the board (try each corner). A dot appears where the board thinks you touched. The top-left of the box is the top-left of the dashboard.</p>
<canvas id="c" width="320" height="240"></canvas>
<div id="num">waiting for a tap...</div>
<script>
const c=document.getElementById('c'),g=c.getContext('2d'),n=document.getElementById('num');
let W=320,H=240,last=null;
function draw(){
g.clearRect(0,0,c.width,c.height);
g.strokeStyle='#2c3644';g.strokeRect(.5,.5,W-1,H-1);
g.fillStyle='#96aabe';g.font='12px sans-serif';
g.textAlign='left';g.fillText('top-left',6,16);
g.textAlign='right';g.fillText('top-right',W-6,16);g.fillText('bottom-right',W-6,H-6);
g.textAlign='left';g.fillText('bottom-left',6,H-6);
if(last){g.fillStyle='#4aa3ff';g.beginPath();g.arc(last.x,last.y,7,0,7);g.fill();
g.strokeStyle='#fff';g.beginPath();g.moveTo(last.x-12,last.y);g.lineTo(last.x+12,last.y);g.moveTo(last.x,last.y-12);g.lineTo(last.x,last.y+12);g.stroke()}
}
async function poll(){
try{
const r=await fetch('/api/touch',{cache:'no-store'}),t=await r.json();
if(t.w){W=t.w;H=t.h;c.width=W;c.height=H}
if(t.seen){last=t;n.textContent='x='+t.x+' y='+t.y+'  (before correction: '+t.px+', '+t.py+')  '+(t.ageMs<1500?'now':Math.round(t.ageMs/1000)+' s ago')}
}catch(e){}
draw();
}
draw();setInterval(poll,300);
</script>
</body>
</html>
)HTML";
