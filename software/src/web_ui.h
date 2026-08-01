#pragma once
#include <pgmspace.h>

static const char WEB_UI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Simplebus2-Bridge</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;max-width:480px;margin:0 auto;padding:14px;background:#111827;color:#e5e7eb}
h1{color:#60a5fa;font-size:1.75em;text-align:center;margin-bottom:2px}
.sub{color:#6b7280;font-size:.8em;margin-bottom:14px;text-align:center}
.sec{color:#93c5fd;font-size:.75em;font-weight:700;text-transform:uppercase;letter-spacing:.1em;margin-bottom:10px;margin-top:14px}
.card{background:#1f2937;border-radius:10px;padding:14px 16px;margin-bottom:4px}
.row{display:flex;justify-content:space-between;align-items:center;padding:5px 0;font-size:.88em;border-bottom:1px solid #374151}
.row:last-child{border-bottom:none}
.lbl{color:#9ca3af}
.val{font-weight:600}
.ok{color:#4ade80}
.err{color:#f87171}
.warn{color:#fbbf24}
label{display:block;font-size:.78em;color:#9ca3af;margin-bottom:3px;margin-top:8px}
label:first-child{margin-top:0}
input[type=text],input[type=password],input[type=number],input[type=url]{width:100%;background:#374151;color:#e5e7eb;border:1px solid #4b5563;border-radius:5px;padding:7px 9px;font-size:.88em}
input[type=text]:focus,input[type=password]:focus,input[type=number]:focus,input[type=url]:focus{outline:none;border-color:#60a5fa;background:#1f2937}
.btn{width:100%;padding:10px;border:none;border-radius:6px;font-size:.88em;font-weight:600;cursor:pointer;margin-top:10px}
.g{background:#059669;color:#fff}
.o{background:#d97706;color:#fff}
.r{background:#dc2626;color:#fff}
.notice{color:#fbbf24;font-size:.85em;margin-top:6px;display:none}
.chk{display:flex;align-items:center;gap:8px;margin-top:8px;color:#e5e7eb;font-size:.88em}
.chk input{width:auto;margin:0}
.g2{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:10px}
.g2 .btn{margin-top:0}
.g2>div>label{margin-top:0}
</style>
</head>
<body>
<h1>Simplebus2-Bridge</h1>
<div class="sub">Simplebus2 Bridge to Home Assistant and MQTT</div>

<div class="sec">Status</div>
<div class="card">
<div id="st"><span style="color:#6b7280">Loading&#x2026;</span></div>
</div>

<div class="sec">WiFi</div>
<div class="card">
<form onsubmit="postForm('/api/wifi',this,null);return false">
<label>SSID</label><input name="ssid" type="text" id="wssid" required>
<label>Password</label><input name="password" type="password" placeholder="&#x2022;&#x2022;&#x2022;&#x2022;&#x2022;&#x2022;&#x2022;&#x2022;" autocomplete="new-password">
<button class="btn g" type="submit">Save &amp; Reconnect</button>
</form>
</div>

<div class="sec">MQTT</div>
<div class="card">
<form onsubmit="postForm('/api/config',this,function(d){if(d.ok&&d.restart_required)document.getElementById('pfxn').style.display='block';else if(d.ok)alert('Saved');});return false">
<label>Broker Host</label><input name="mqtt_host" type="text" required>
<div class="g2" style="margin-top:0">
<div><label>User</label><input name="mqtt_user" type="text"></div>
<div><label>Password</label><input name="mqtt_pass" type="password" placeholder="&#x2022;&#x2022;&#x2022;&#x2022;&#x2022;&#x2022;&#x2022;&#x2022;" autocomplete="new-password"></div>
</div>
<label>Topic Prefix</label><input name="mqtt_prefix" type="text" required maxlength="31">
<button class="btn g" type="submit">Save</button>
<div class="notice" id="pfxn">Restart required for prefix change to take effect.</div>
</form>
</div>

<div class="sec">Device Configuration</div>
<div class="card">
<form onsubmit="postForm('/api/config',this,null);return false">
<input type="hidden" name="update_device" value="1">
<div class="g2" style="margin-top:0">
<div><label>Address (0&ndash;255)</label><input name="address" type="number" min="0" max="255"></div>
<div><label>Gain (2&ndash;40)</label><input name="gain" type="number" min="2" max="40"></div>
</div>
<div class="g2" style="margin-top:4px">
<div><label>Threshold mV (100&ndash;1500)</label><input name="threshold" type="number" min="100" max="1500"></div>
<div><label>Ring-to-Open min (1&ndash;1440)</label><input name="rto_minutes" type="number" min="1" max="1440"></div>
</div>
<button class="btn g" type="submit">Save</button>
</form>
</div>

<div class="sec">Firmware Update (OTA)</div>
<div class="card">
<form onsubmit="postForm('/api/ota',this,function(d){if(d.ok)alert('OTA started \u2014 device will reboot.');});return false">
<label>URL</label><input name="url" type="url" placeholder="http://host/simplebus2-firmware.bin" required>
<button class="btn o" type="submit">OTA Flash</button>
</form>
<div style="border-top:1px solid #374151;margin-top:10px;padding-top:10px">
<label style="margin-top:0">File Upload</label>
<input type="file" id="otaFile" accept=".bin" style="width:100%;background:#374151;color:#e5e7eb;border:1px solid #4b5563;border-radius:5px;padding:7px 9px;font-size:.88em">
<button class="btn o" style="margin-top:8px" onclick="otaUpload();return false">Upload &amp; Flash</button>
<div id="otaProg" style="display:none;margin-top:6px;font-size:.82em;color:#fbbf24"></div>
</div>
</div>

<div class="sec">System</div>
<div class="card">
<div class="g2">
<button class="btn o" onclick="sysAction('/api/restart','Restart device?')">Restart</button>
<button class="btn r" onclick="sysAction('/api/factory-reset','Factory reset? All settings will be erased!')">Factory Reset</button>
</div>
</div>

<script>
function pad(n){return ('0'+n).slice(-2);}
function fmtUptime(s){var d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),m=Math.floor((s%3600)/60),sc=s%60;return d+'d '+pad(h)+':'+pad(m)+':'+pad(sc);}
function fmtKB(free,total){return Math.round(free/1024)+'/'+Math.round(total/1024)+' kB ('+(Math.round(free*100/total))+'% free)';}
function qualCls(q){if(q==='Excellent'||q==='Good')return 'ok';if(q==='Ok')return 'warn';return 'err';}
function updateStatus(d){
  var wifiTxt,wifiCls,mqttTxt,mqttCls;
  if(d.wifi){
    wifiTxt='Connected ('+d.rssi+'\u202fdBm, '+d.wifi_quality+')';
    wifiCls='val '+qualCls(d.wifi_quality);
  } else {
    wifiTxt='Disconnected'; wifiCls='val err';
  }
  mqttTxt=d.mqtt?'Connected':'Disconnected';
  mqttCls=d.mqtt?'val ok':'val err';
  document.getElementById('st').innerHTML=
    '<div class="row"><span class="lbl">Firmware</span><span class="val">'+d.fw+'</span></div>'+
    '<div class="row"><span class="lbl">Uptime</span><span class="val">'+fmtUptime(d.uptime_s)+'</span></div>'+
    '<div class="row"><span class="lbl">WiFi</span><span class="'+wifiCls+'">'+wifiTxt+'</span></div>'+
    '<div class="row"><span class="lbl">MQTT</span><span class="'+mqttCls+'">'+mqttTxt+'</span></div>'+
    '<div class="row"><span class="lbl">CPU Temp</span><span class="val">'+d.temp_c.toFixed(1)+'\u202f\u00b0C</span></div>'+
    '<div class="row"><span class="lbl">Free Heap</span><span class="val">'+fmtKB(d.free_heap,d.heap_total)+'</span></div>'+
    '<div class="row"><span class="lbl">Free Flash</span><span class="val">'+fmtKB(d.flash_free,d.flash_total)+'</span></div>';
}
function loadStatus(){fetch('/api/status').then(function(r){return r.json();}).then(updateStatus).catch(function(){});}
function loadConfig(){
  fetch('/api/config').then(function(r){return r.json();}).then(function(d){
    document.querySelector('[name=mqtt_host]').value=d.mqtt_host||'';
    document.querySelector('[name=mqtt_user]').value=d.mqtt_user||'';
    document.querySelector('[name=mqtt_prefix]').value=d.mqtt_prefix||'SimpleBus';
    document.querySelector('[name=address]').value=d.address!==undefined?d.address:9;
    document.querySelector('[name=gain]').value=d.gain||20;
    document.querySelector('[name=threshold]').value=d.threshold||600;
    document.querySelector('[name=rto_minutes]').value=d.rto_minutes||600;
    if(d.wifi_ssid)document.getElementById('wssid').value=d.wifi_ssid;
  });
}
function postForm(url,form,cb){
  var body=new URLSearchParams(new FormData(form)).toString();
  fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
  .then(function(r){return r.json();})
  .then(function(d){if(cb){cb(d);}else{alert(d.ok?'Saved':'Error: '+(d.error||'?'));}})
  .catch(function(){alert('Error');});
}
function sysAction(url,msg){
  if(confirm(msg)){fetch(url,{method:'POST'}).then(function(){alert('Done');}).catch(function(){});}
}
function otaUpload(){
  var f=document.getElementById('otaFile').files[0];
  if(!f){alert('No file selected');return;}
  if(!confirm('Flash '+f.name+'? Device will reboot.'))return;
  var fd=new FormData();fd.append('firmware',f);
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/api/ota-upload');
  var prog=document.getElementById('otaProg');
  prog.style.display='block';prog.textContent='Uploading\u2026';
  xhr.upload.onprogress=function(e){if(e.lengthComputable)prog.textContent='Uploading\u2026 '+Math.round(e.loaded*100/e.total)+'%';};
  xhr.onload=function(){
    try{var d=JSON.parse(xhr.responseText);if(d.ok){prog.textContent='Done \u2014 rebooting\u2026';}else{prog.textContent='Error: '+(d.error||'?');}}
    catch(e){prog.textContent='Error';}
  };
  xhr.onerror=function(){prog.textContent='Upload failed';};
  xhr.send(fd);
}
loadStatus();
loadConfig();
setInterval(loadStatus,5000);
</script>
</body>
</html>
)rawliteral";
