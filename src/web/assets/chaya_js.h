#pragma once

#include <Arduino.h>

/** Dashboard chaya counters + send via fetch (PROGMEM); uses global showToast. */
inline const char CHAYA_JS[] PROGMEM =
    R"cy((function(){var kPollMs=2000;function poll(){fetch('/chaya-status').then(function(r){return r.json();}).then(function(d){var rx=document.getElementById('chaya-rx'),tx=document.getElementById('chaya-tx');if(rx)rx.textContent=d.rx;if(tx)tx.textContent=d.tx;}).catch(function(){});}setInterval(poll,kPollMs);poll();var f=document.getElementById('chaya-form');if(!f)return;f.addEventListener('submit',function(ev){ev.preventDefault();var tok=f.querySelector('input[name="csrf_token"]');if(!tok)return;var btn=f.querySelector('button[type="submit"]');if(btn)btn.disabled=true;var body='csrf_token='+encodeURIComponent(tok.value);fetch('/chaya-send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:body}).then(function(r){return r.json();}).then(function(j){if(btn)btn.disabled=false;if(window.showToast)(j&&j.ok?showToast('Gesendet!'):showToast('Fehler.',true));}).catch(function(){if(btn)btn.disabled=false;if(window.showToast)showToast('Fehler.',true);});});})();)cy";
