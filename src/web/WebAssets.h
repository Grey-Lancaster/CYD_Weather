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
<dt>Free heap</dt><dd id="st-heap">&ndash;</dd>
<dt>Weather source</dt><dd id="st-wxsrc">&ndash;</dd>
<dt>Weather updated</dt><dd id="st-wxupd">&ndash;</dd>
</dl>
</div>
<form id="f">
<div class="card">
<h2>Network</h2>
<label>WiFi SSID</label><input name="wifiSsid" maxlength="32" autocomplete="off">
<label>WiFi password</label><input name="wifiPass" type="password" maxlength="64" autocomplete="new-password">
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
<label>Timezone (POSIX TZ string)</label><input name="timezone" maxlength="47" placeholder="UTC0">
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
<div class="check"><input name="rotate90" type="checkbox" id="rot90"><label for="rot90">Rotate display 90&deg;</label></div>
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
<script>
async function j(u,o){const r=await fetch(u,o);if(!r.ok)throw new Error('HTTP '+r.status);return r.json()}
function el(i){return document.getElementById(i)}
function setVal(e,v){if(e.type==='checkbox'){e.checked=!!v}else{e.value=v}}
function getVal(e){
if(e.type==='checkbox')return e.checked;
if(e.type==='number'||e.type==='range'||e.tagName==='SELECT')return +e.value;
return e.value}
async function refresh(){try{
const s=await j('/api/status');
el('ver').textContent='v'+s.version;
el('st-wifi').textContent=s.wifi.state+(s.wifi.ssid?' ('+s.wifi.ssid+')':'');
el('st-ip').textContent=s.wifi.ip||'-';
el('st-rssi').textContent=s.wifi.rssi?s.wifi.rssi+' dBm':'-';
el('st-up').textContent=s.uptime;
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
