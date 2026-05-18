#pragma once

#include <Arduino.h>

/** Poll /mqtt-status and update #ms (minified IIFE). Broker/port from data-broker / data-port. */
inline const char MQTT_STATUS_JS[] PROGMEM =
    R"ms((function(){var ms=document.getElementById('ms');if(!ms){return;}var kPollMs=2000;function poll(){fetch('/mqtt-status').then(function(r){return r.json();}).then(function(d){var b=ms.getAttribute('data-broker'),p=ms.getAttribute('data-port');if(d.connected){ms.className='hint';ms.textContent='Connected to '+b+':'+p;}else{ms.className='err';ms.textContent='Not connected';}}).catch(function(){ms.className='hint';ms.textContent='Status error.';});}setInterval(poll,kPollMs);poll();})();)ms";
