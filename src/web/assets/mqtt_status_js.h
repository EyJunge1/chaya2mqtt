#pragma once

#include <Arduino.h>

/** SSE + initial fetch for /mqtt-status; updates #ms (broker/port from data attributes). */
inline const char MQTT_STATUS_JS[] PROGMEM =
    R"ms((function(){var ms=document.getElementById('ms');if(!ms){return;}var b=ms.getAttribute('data-broker'),p=ms.getAttribute('data-port');function apply(d){if(!d)return;if(d.connected){ms.className='hint';ms.textContent='Connected to '+b+':'+p;}else{ms.className='err';ms.textContent='Not connected';}}fetch('/mqtt-status').then(function(r){return r.json();}).then(apply).catch(function(){ms.className='hint';ms.textContent='Status error.';});if(typeof EventSource!=='undefined'){try{var es=new EventSource('/events');es.addEventListener('mqtt',function(ev){try{apply(JSON.parse(ev.data));}catch(e){}});}catch(e){}}})();)ms";
