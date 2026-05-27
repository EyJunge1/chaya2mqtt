#pragma once

#include <Arduino.h>

/** SSE + initial fetch for /wifi-status; updates #cs. */
inline const char WIFI_STATUS_JS[] PROGMEM =
    R"wst((function(){var cs=document.getElementById('cs');if(!cs){return;}function h(x){return String(x==null?'':x).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');}function apply(d){if(!d)return;if(d.connected){cs.className='hint';cs.innerHTML='Connected: <strong>'+h(d.ssid)+'</strong>, IP '+h(d.ip)+', RSSI '+d.rssi+' dBm';}else{cs.innerHTML='';cs.className='hint';}}fetch('/wifi-status').then(function(r){return r.json();}).then(apply).catch(function(){cs.className='hint';cs.textContent='Status error.';});if(typeof EventSource!=='undefined'){try{var es=new EventSource('/events');es.addEventListener('wifi',function(ev){try{apply(JSON.parse(ev.data));}catch(e){}});}catch(e){}}})();)wst";
