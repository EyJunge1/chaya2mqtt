#pragma once

#include <Arduino.h>

/** Poll /wifi-status and update #cs (minified IIFE). */
inline const char WIFI_STATUS_JS[] PROGMEM =
    R"wst((function(){var cs=document.getElementById('cs');if(!cs){return;}var kPollMs=2000;function h(x){return String(x==null?'':x).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');}function poll(){fetch('/wifi-status').then(function(r){return r.json();}).then(function(d){if(d.connected){cs.className='hint';cs.innerHTML='Connected: <strong>'+h(d.ssid)+'</strong>, IP '+h(d.ip)+', RSSI '+d.rssi+' dBm';}else{cs.innerHTML='';cs.className='hint';}}).catch(function(){cs.className='hint';cs.textContent='Status error.';});}setInterval(poll,kPollMs);poll();})();)wst";
