// Beonos: the page a phone sees after joining the "Beonos Setup" network.
// It replaces ESPHome's stock captive portal page and talks to the same
// /config.json and /wifisave endpoints, plus /beonos.json for the extras.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace beonos {

static const char SETUP_PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Set up Beonos</title>
<style>
:root{--bg:#f6f4f0;--fg:#1d1b18;--muted:#6b665e;--card:#fff;--line:#ddd8cf;--accent:#1d1b18;--accent-fg:#fff}
@media (prefers-color-scheme:dark){:root{--bg:#161513;--fg:#f2efe9;--muted:#a39d93;--card:#211f1c;--line:#38342f;--accent:#f2efe9;--accent-fg:#161513}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.45 -apple-system,system-ui,sans-serif}
main{max-width:28rem;margin:0 auto;padding:24px 16px 40px}
h1{font-size:1.5rem;margin:0 0 4px}
p{margin:0 0 16px}
.muted{color:var(--muted)}
ul{list-style:none;margin:0 0 16px;padding:0;background:var(--card);border:1px solid var(--line);border-radius:12px;overflow:hidden}
li button{all:unset;box-sizing:border-box;display:flex;width:100%;padding:14px 16px;gap:12px;align-items:center;cursor:pointer;border-top:1px solid var(--line)}
li:first-child button{border-top:0}
li button[aria-pressed=true]{font-weight:600}
li button span:first-child{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
label{display:block;font-weight:600;margin:0 0 6px}
input[type=text],input[type=password]{width:100%;padding:12px;font:inherit;color:inherit;background:var(--card);border:1px solid var(--line);border-radius:10px;margin:0 0 10px}
.check{display:flex;gap:10px;align-items:flex-start;font-weight:400;margin:0 0 16px}
.check input{width:20px;height:20px;margin:2px 0 0}
.submit{width:100%;padding:14px;font:inherit;font-weight:600;border:0;border-radius:10px;background:var(--accent);color:var(--accent-fg)}
.submit:disabled{opacity:.4}
.address{font-weight:600;word-break:break-all}
.error{padding:12px 14px;border-radius:10px;background:#fde8e4;color:#7a1d0e}
@media (prefers-color-scheme:dark){.error{background:#4a1c14;color:#ffd9d0}}
[hidden]{display:none!important}
</style></head><body><main>
<section id="form">
<h1>Set up Beonos</h1>
<p class="error" id="err" role="alert" hidden></p>
<p class="muted">Choose the Wi-Fi network your Sonos speaker is on. Beonos needs a 2.4&nbsp;GHz network.</p>
<ul id="list"><li><button type="button" disabled><span>Looking for networks&hellip;</span></button></li></ul>
<div id="other" hidden><label for="ssid">Network name</label><input id="ssid" type="text" autocomplete="off" autocapitalize="none" spellcheck="false"></div>
<div id="pw" hidden><label for="psk">Password</label><input id="psk" type="password" autocomplete="current-password" autocapitalize="none" spellcheck="false">
<label class="check"><input id="showpw" type="checkbox"><span>Show password</span></label></div>
<label class="check" id="autowrap" hidden><input id="auto" type="checkbox" checked><span>Install updates automatically<br><span class="muted">Updates install only while the turntable is off.</span></span></label>
<p class="muted">After setup, you can change settings at<br><span class="address" id="addr1">this Beonos's own address</span></p>
<button class="submit" id="go" type="button" disabled>Connect</button>
</section>
<section id="wait" hidden>
<h1>Connecting&hellip;</h1>
<p>Beonos is trying to join <strong class="ssidname"></strong>. Keep this page open; it takes up to a minute.</p>
</section>
<section id="done" hidden>
<h1>Beonos is on your Wi-Fi</h1>
<p>Beonos joined <strong class="ssidname"></strong>. Your phone will drop off this setup network in a moment, so switch it back to your usual Wi-Fi.</p>
<p>To change settings later, open this address on the same Wi-Fi:<br><span class="address" id="addr2"></span></p>
</section>
<script>
(function(){
var list=document.getElementById('list'),other=document.getElementById('other'),pw=document.getElementById('pw'),
ssidIn=document.getElementById('ssid'),pskIn=document.getElementById('psk'),go=document.getElementById('go'),chosen=null;
function el(t,c){var e=document.createElement(t);if(c)e.textContent=c;return e}
function bars(r){return r>-60?'▂▄▆':r>-72?'▂▄':'▂'}
function pick(btn,ap){
 Array.prototype.forEach.call(list.querySelectorAll('button'),function(b){b.setAttribute('aria-pressed','false')});
 btn.setAttribute('aria-pressed','true');chosen=ap;other.hidden=!!ap;pw.hidden=!!(ap&&!ap.lock);
 go.disabled=false;(ap?(ap.lock?pskIn:go):ssidIn).focus();
}
function row(text,right,ap){
 var li=el('li'),b=el('button');b.type='button';b.setAttribute('aria-pressed','false');
 b.appendChild(el('span',text));if(right)b.appendChild(el('span',right));
 b.onclick=function(){pick(b,ap)};li.appendChild(b);list.appendChild(li);
}
function address(name){var a='http://'+name+'.local';document.getElementById('addr1').textContent=a;document.getElementById('addr2').textContent=a}
fetch('/beonos.json').then(function(r){return r.json()}).then(function(j){
 if(j.auto_update_option){document.getElementById('autowrap').hidden=false;document.getElementById('auto').checked=!!j.auto_update}
}).catch(function(){});
fetch('/config.json').then(function(r){return r.json()}).then(function(j){
 address(j.name);list.textContent='';
 var seen={};(j.aps||[]).filter(function(a){return a&&a.ssid}).sort(function(a,b){return b.rssi-a.rssi}).forEach(function(a){
  if(seen[a.ssid])return;seen[a.ssid]=1;row(a.ssid,(a.lock?'🔒 ':'')+bars(a.rssi),a);
 });
 row('Other network…','',null);
}).catch(function(){list.textContent='';row('Other network…','',null)});
document.getElementById('showpw').onchange=function(){pskIn.type=this.checked?'text':'password'};
function show(id){['form','wait','done'].forEach(function(s){document.getElementById(s).hidden=s!==id})}
function fail(msg){var e=document.getElementById('err');e.textContent=msg;e.hidden=false;show('form');go.disabled=false;(pw.hidden?go:pskIn).focus()}
function watch(ssid,started,misses){
 setTimeout(function(){
  fetch('/beonos.json',{cache:'no-store'}).then(function(r){return r.json()}).then(function(j){
   if(j.wifi==='connected')return show('done');
   if(j.wifi==='wrong_password')return fail('Beonos couldn\u2019t join '+ssid+': the password was rejected. Check it and try again.');
   if(j.wifi==='not_found')return fail('Beonos can\u2019t find '+ssid+'. It needs a 2.4\u00a0GHz network within reach.');
   if(j.wifi==='failed'||Date.now()-started>60000)return fail('Beonos couldn\u2019t join '+ssid+'. Check the password and try again.');
   watch(ssid,started,0);
  }).catch(function(){
   // The setup network goes away once Beonos has joined.
   if(misses>=2)return show('done');
   watch(ssid,started,misses+1);
  });
 },1500);
}
go.onclick=function(){
 var ssid=chosen?chosen.ssid:ssidIn.value.trim();if(!ssid){ssidIn.focus();return}
 var auto=document.getElementById('auto'),q='ssid='+encodeURIComponent(ssid)+'&psk='+encodeURIComponent(pw.hidden?'':pskIn.value)+
  (document.getElementById('autowrap').hidden?'':'&auto='+(auto.checked?'1':'0'));
 go.disabled=true;document.getElementById('err').hidden=true;
 Array.prototype.forEach.call(document.querySelectorAll('.ssidname'),function(n){n.textContent=ssid});
 fetch('/wifisave?'+q).then(function(){show('wait');watch(ssid,Date.now(),0)}).catch(function(){go.disabled=false});
};
})();
</script></main></body></html>
)HTML";

}  // namespace beonos
