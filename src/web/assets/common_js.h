#pragma once

#include <Arduino.h>

/** Global toast helper — minified JS (PROGMEM). */
static const char COMMON_JS[] PROGMEM =
    R"ct((function(){function showToast(m,e){var t=document.getElementById('toast');if(!t)return;t.textContent=m;t.className='toast show'+(e?' err':'');clearTimeout(t._i);t._i=setTimeout(function(){t.className='toast';},2500);}window.showToast=showToast;})();)ct";
