#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "launcher_common.h"
#include "web_relay_icon.h"

#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr int            kWebRelayPort = 6090;
constexpr unsigned short kInputPort    = 7331;

constexpr double kProtocolWidth      = 1920.0;
constexpr double kProtocolHeight     = 1080.0;
constexpr int    kCursorModeDisabled = 0x00034003;

constexpr DWORD kProbeIntervalMs = 1500;

enum : unsigned char { kPktState = 1, kPktPing = 4, kPktMode = 5 };

const char* const kManifest =
    "{\"name\":\"Bandit Mouse\",\"short_name\":\"Bandit Mouse\","
    "\"start_url\":\"/\",\"scope\":\"/\",\"display\":\"standalone\","
    "\"orientation\":\"any\",\"background_color\":\"#071018\","
    "\"theme_color\":\"#0a1420\","
    "\"icons\":[{\"src\":\"/icon-180.png\",\"sizes\":\"180x180\","
    "\"type\":\"image/png\",\"purpose\":\"any\"}]}";

const char* const kWebRelayPage = R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,minimum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="Bandit Mouse">
<meta name="theme-color" content="#0a1420">
<link rel="apple-touch-icon" href="/icon-180.png">
<link rel="icon" type="image/png" sizes="180x180" href="/icon-180.png">
<link rel="manifest" href="/manifest.webmanifest">
<title>Bandit Web Mouse Support</title>
<style>
:root{--line:#243444;--muted:#9eb0bf;--text:#edf4f8;--accent:#70c486;--accent2:#69b7cc;}
*{box-sizing:border-box;-webkit-user-select:none;user-select:none;-webkit-tap-highlight-color:transparent;-webkit-touch-callout:none;}
html,body{margin:0;height:100%;overflow:hidden;position:fixed;inset:0;overscroll-behavior:none;}
body{font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:radial-gradient(circle at top,#102030,#071018 60%);color:var(--text);display:flex;flex-direction:column;height:100dvh;touch-action:none;}
header{display:flex;align-items:center;justify-content:space-between;
  padding:calc(10px + env(safe-area-inset-top)) calc(16px + env(safe-area-inset-right)) 10px calc(16px + env(safe-area-inset-left));
  border-bottom:1px solid var(--line);}
.brand{display:flex;align-items:center;gap:10px;font-weight:600;}
.logo{width:22px;height:22px;border-radius:6px;background:linear-gradient(135deg,var(--accent),var(--accent2));}
.dot{width:10px;height:10px;border-radius:50%;background:#4a5a68;transition:background .2s;}
.dot.live{background:var(--accent);box-shadow:0 0 10px var(--accent);}
.hdr-right{display:flex;align-items:center;gap:10px;color:var(--muted);font-size:13px;font-variant-numeric:tabular-nums;}
#mode{padding:2px 8px;border-radius:999px;border:1px solid var(--line);font-size:11px;letter-spacing:.08em;color:#6c7c8c;}
#mode.menu{border-color:var(--accent2);color:var(--accent2);}
#mode.game{border-color:var(--accent);color:var(--accent);}
.stage{flex:1;display:flex;flex-direction:column;min-height:0;}
#pad{flex:1;margin:14px calc(14px + env(safe-area-inset-right)) 14px calc(14px + env(safe-area-inset-left));border:1px solid var(--line);border-radius:16px;background:linear-gradient(180deg,#0d1822,#0a131c);display:flex;align-items:center;justify-content:center;text-align:center;color:var(--muted);position:relative;overflow:hidden;touch-action:none;overscroll-behavior:none;}
#pad .hint{padding:24px;max-width:430px;transition:opacity .2s;}
#pad .hint b{color:var(--text);display:block;font-size:17px;margin-bottom:6px;}
#pad .hint i{display:block;font-size:12px;font-style:normal;color:#55606f;margin-top:8px;}
#pad.live{border-color:var(--accent);}
#pad.touching .hint{opacity:0;}
.controls{display:flex;flex-direction:column;gap:8px;
  padding:0 calc(14px + env(safe-area-inset-right)) calc(14px + env(safe-area-inset-bottom)) calc(14px + env(safe-area-inset-left));}
.row{display:flex;gap:8px;}
.btn{flex:1 1 0;min-width:60px;min-height:54px;border:1px solid var(--line);border-radius:12px;background:#0e1a24;color:var(--text);font:inherit;font-weight:600;display:flex;align-items:center;justify-content:center;touch-action:none;}
.btn:active,.btn.on{background:#16313f;border-color:var(--accent2);color:#fff;}
.btn.wide{flex:2 1 0;}
.sens{display:flex;align-items:center;gap:10px;color:var(--muted);font-size:13px;padding-top:2px;}
.sens input{flex:1;}
.hidden{display:none!important;}
@media (orientation:landscape) and (max-height:560px){
.stage{flex-direction:row;}
#pad{margin:10px 8px 10px calc(12px + env(safe-area-inset-left));}
.controls{width:240px;justify-content:center;padding:10px calc(12px + env(safe-area-inset-right)) calc(10px + env(safe-area-inset-bottom)) 8px;}
.btn{min-height:44px;}
header{padding:calc(8px + env(safe-area-inset-top)) calc(16px + env(safe-area-inset-right)) 8px calc(16px + env(safe-area-inset-left));}
}
</style>
</head>
<body>
<header>
  <div class="brand"><span class="logo"></span>Bandit Web Mouse Support</div>
  <div class="hdr-right"><span id="stats"></span><span id="mode">--</span><span id="statetext">connecting</span><span class="dot" id="dot"></span></div>
</header>
<div class="stage">
<div id="pad"><div class="hint"><b>Touchpad</b>Drag to move<i>External mouse works too. Tap once to lock the pointer where supported.</i></div></div>
<div class="controls">
  <div class="row">
    <button class="btn wide" id="bl">Left</button>
    <button class="btn" id="bm">Middle</button>
    <button class="btn wide" id="br">Right</button>
  </div>
  <div class="row">
    <button class="btn" id="su">Scroll +</button>
    <button class="btn" id="sd">Scroll -</button>
    <button class="btn hidden" id="fs">Fullscreen</button>
  </div>
  <div class="sens"><span>Speed</span><input type="range" id="sens" min="0.4" max="4" step="0.1" value="1.2"><span id="sensv">1.2x</span></div>
</div>
</div>
<script>
(function(){
  'use strict';
  var PKT_STATE=1,PKT_PING=4,PKT_MODE=5;

  var pendX=0,pendY=0,scroll=0,l=0,r=0,m=0;
  var sens=1.2,dirty=false,captured=false,live=false,menuMode=false;

  var pad=document.getElementById('pad'),dot=document.getElementById('dot');
  var statetext=document.getElementById('statetext'),stats=document.getElementById('stats');
  var modeEl=document.getElementById('mode');
  var sensEl=document.getElementById('sens'),sensv=document.getElementById('sensv');
  sensEl.addEventListener('input',function(){sens=parseFloat(sensEl.value);sensv.textContent=sens.toFixed(1)+'x';});

  function setLive(v){if(v!==live){live=v;dot.classList.toggle('live',v);pad.classList.toggle('live',v);}}
  function setBtn(w,v){if(w===0)l=v;else if(w===1)m=v;else if(w===2)r=v;dirty=true;}
  function clamp(v,lo,hi){return v<lo?lo:(v>hi?hi:v);}

  var ws=null,wsOpen=false,backoff=250,rtt=null,tx=0,smp=0;

  function connect(){
    var proto=(location.protocol==='https:')?'wss://':'ws://';
    try{ws=new WebSocket(proto+location.host+'/ws');}catch(e){setTimeout(connect,backoff);return;}
    ws.binaryType='arraybuffer';
    ws.onopen=function(){wsOpen=true;backoff=250;setLive(true);statetext.textContent=captured?'pointer locked':'connected';};
    ws.onclose=function(){wsOpen=false;setLive(false);statetext.textContent='reconnecting';
      modeEl.textContent='--';modeEl.className='';
      setTimeout(connect,backoff);backoff=Math.min(backoff*2,4000);};
    ws.onerror=function(){};
    ws.onmessage=function(ev){
      if(!(ev.data instanceof ArrayBuffer)||ev.data.byteLength<2)return;
      var v=new DataView(ev.data),t=v.getUint8(0);
      if(t===PKT_PING&&ev.data.byteLength>=5){
        var s=((performance.now()|0)-v.getUint32(1,true));
        if(s>=0&&s<5000)rtt=(rtt===null)?s:(rtt*0.7+s*0.3);
      }else if(t===PKT_MODE){
        menuMode=v.getUint8(1)===1;
        modeEl.textContent=menuMode?'MENU':'GAME';
        modeEl.className=menuMode?'menu':'game';
      }
    };
  }

  var stateBuf=new ArrayBuffer(9),stateV=new DataView(stateBuf);
  var pingBuf=new ArrayBuffer(5),pingV=new DataView(pingBuf);
  var seq=0;

  function sendState(sx,sy,sc){
    if(!wsOpen||ws.readyState!==1)return false;
    if(ws.bufferedAmount>4096)return false;
    stateV.setUint8(0,PKT_STATE);
    stateV.setInt16(1,clamp(sx,-32768,32767),true);
    stateV.setInt16(3,clamp(sy,-32768,32767),true);
    stateV.setUint8(5,(l?1:0)|(r?2:0)|(m?4:0));
    stateV.setInt8(6,clamp(sc,-127,127));
    stateV.setUint16(7,seq,true);
    ws.send(stateBuf);seq=(seq+1)&0xffff;tx++;
    return true;
  }

  function sendPing(){
    if(!wsOpen||ws.readyState!==1)return;
    pingV.setUint8(0,PKT_PING);
    pingV.setUint32(1,performance.now()|0,true);
    ws.send(pingBuf);
  }

  var OPT={passive:false};
  var pts=new Map();
  var lastMx=null,lastMy=null;

  function isMouse(e){return e.pointerType==='mouse'||e.pointerType==='pen';}

  var canLock=('requestPointerLock' in pad)&&('pointerLockElement' in document);
  if(canLock){
    pad.addEventListener('click',function(){if(!captured)pad.requestPointerLock();});
    document.addEventListener('pointerlockchange',function(){
      captured=(document.pointerLockElement===pad);
      lastMx=null;lastMy=null;
      statetext.textContent=captured?'pointer locked':(wsOpen?'connected':'reconnecting');
    });
  }

  function mouseMove(e){
    var mvx=(typeof e.movementX==='number')?e.movementX:0;
    var mvy=(typeof e.movementY==='number')?e.movementY:0;
    var ddx=0,ddy=0;
    if(captured){
      ddx=mvx;ddy=mvy;
    }else{
      if(lastMx===null){lastMx=e.clientX;lastMy=e.clientY;return;}
      ddx=e.clientX-lastMx;ddy=e.clientY-lastMy;
      lastMx=e.clientX;lastMy=e.clientY;
      if(ddx===0&&ddy===0&&(mvx||mvy)){ddx=mvx;ddy=mvy;}
    }
    if(ddx||ddy){pendX+=ddx*sens;pendY+=ddy*sens;smp++;}
  }

  function ptDown(e){
    e.preventDefault();
    if(isMouse(e)){
      setBtn(e.button,1);
      lastMx=e.clientX;lastMy=e.clientY;
      return;
    }
    try{pad.setPointerCapture(e.pointerId);}catch(_){}
    pts.set(e.pointerId,{x:e.clientX,y:e.clientY});
    pad.classList.add('touching');
  }

  function ptMove(e){
    if(isMouse(e)){e.preventDefault();mouseMove(e);return;}
    var st=pts.get(e.pointerId);if(!st)return;
    e.preventDefault();
    var list=(typeof e.getCoalescedEvents==='function')?(e.getCoalescedEvents()||[e]):[e];
    if(!list.length)list=[e];
    var ax=0,ay=0;
    for(var i=0;i<list.length;i++){
      ax+=list[i].clientX-st.x;ay+=list[i].clientY-st.y;
      st.x=list[i].clientX;st.y=list[i].clientY;
      smp++;
    }
    if(pts.size>=2){if(ay<-2||ay>2){scroll+=(ay<0?1:-1);dirty=true;}}
    else{pendX+=ax*sens*1.6;pendY+=ay*sens*1.6;}
  }

  function ptUp(e){
    if(isMouse(e)){e.preventDefault();setBtn(e.button,0);return;}
    if(!pts.has(e.pointerId))return;
    e.preventDefault();
    pts.delete(e.pointerId);
    if(pts.size===0){pad.classList.remove('touching');flushPending();}
  }

  pad.addEventListener('pointerdown',ptDown,OPT);
  pad.addEventListener('pointermove',ptMove,OPT);
  pad.addEventListener('pointerup',ptUp,OPT);
  pad.addEventListener('pointercancel',ptUp,OPT);
  pad.addEventListener('pointerleave',function(e){if(isMouse(e)&&!captured){lastMx=null;lastMy=null;}});
  pad.addEventListener('pointerenter',function(e){if(isMouse(e)&&!captured){lastMx=e.clientX;lastMy=e.clientY;}});
  window.addEventListener('pointerup',function(e){if(isMouse(e))setBtn(e.button,0);});

  pad.addEventListener('touchstart',function(e){e.preventDefault();},OPT);
  pad.addEventListener('touchmove',function(e){e.preventDefault();},OPT);
  pad.addEventListener('touchend',function(e){e.preventDefault();},OPT);
  pad.addEventListener('gesturestart',function(e){e.preventDefault();},OPT);
  pad.addEventListener('contextmenu',function(e){e.preventDefault();},OPT);
  document.addEventListener('touchmove',function(e){e.preventDefault();},OPT);
  pad.addEventListener('wheel',function(e){
    scroll+=(e.deltaY<0?1:-1);dirty=true;e.preventDefault();
  },OPT);

  document.addEventListener('visibilitychange',function(){
    if(document.visibilityState==='visible'){
      pts.clear();pendX=pendY=scroll=0;l=r=m=0;dirty=true;
      lastMx=null;lastMy=null;
      pad.classList.remove('touching');
    }
  });

  function holdBtn(id,w){
    var el=document.getElementById(id);
    var dn=function(e){setBtn(w,1);el.classList.add('on');e.preventDefault();};
    var up=function(e){setBtn(w,0);el.classList.remove('on');e.preventDefault();};
    el.addEventListener('pointerdown',dn,OPT);
    el.addEventListener('pointerup',up,OPT);
    el.addEventListener('pointercancel',up,OPT);
    el.addEventListener('touchstart',function(e){e.preventDefault();},OPT);
  }
  holdBtn('bl',0);holdBtn('bm',1);holdBtn('br',2);

  function scrollBtn(id,a){
    var el=document.getElementById(id);
    var go=function(e){scroll+=a;dirty=true;e.preventDefault();};
    el.addEventListener('pointerdown',go,OPT);
    el.addEventListener('touchstart',function(e){e.preventDefault();},OPT);
  }
  scrollBtn('su',1);scrollBtn('sd',-1);

  var fsEl=document.documentElement;
  var fsReq=fsEl.requestFullscreen||fsEl.webkitRequestFullscreen;
  var fsBtn=document.getElementById('fs');
  if(fsReq){
    fsBtn.classList.remove('hidden');
    fsBtn.addEventListener('click',function(){
      var d=document,isFs=d.fullscreenElement||d.webkitFullscreenElement;
      if(isFs){(d.exitFullscreen||d.webkitExitFullscreen).call(d);}else{fsReq.call(fsEl);}
    });
  }

  var TICK_MS=4;
  var lastKeep=performance.now(),lastPing=lastKeep,lastStat=lastKeep;
  var dispFps=60;

  function emit(ix,iy){
    if(sendState(ix,iy,scroll)){
      pendX-=ix;pendY-=iy;scroll=0;dirty=false;
      return true;
    }
    return false;
  }

  function flushPending(){
    var ix=Math.round(pendX),iy=Math.round(pendY);
    if(ix||iy||scroll||dirty)emit(ix,iy);
  }

  function tick(){
    var now=performance.now();
    var ix=Math.round(pendX),iy=Math.round(pendY);
    if(ix||iy||scroll||dirty){
      if(emit(ix,iy))lastKeep=now;
    }else if(now-lastKeep>=700){
      if(sendState(0,0,0))lastKeep=now;
    }
    if(now-lastPing>=500){sendPing();lastPing=now;}
    if(now-lastStat>=1000){
      var el=now-lastStat;
      stats.textContent=(rtt===null?'--':rtt.toFixed(0)+'ms')+
        ' · '+Math.round(smp*1000/el)+'smp'+
        ' · '+Math.round(tx*1000/el)+'tx'+
        (dispFps<45?' · '+dispFps.toFixed(0)+'fps':'');
      smp=0;tx=0;lastStat=now;
    }
  }
  setInterval(tick,TICK_MS);

  var frames=0,fpsMark=performance.now();
  function rafProbe(now){
    requestAnimationFrame(rafProbe);
    frames++;
    if(now-fpsMark>=1000){dispFps=frames*1000/(now-fpsMark);frames=0;fpsMark=now;}
  }
  requestAnimationFrame(rafProbe);

  if('wakeLock' in navigator){navigator.wakeLock.request('screen').catch(function(){});}
  connect();
})();
</script>
</body>
</html>
)PAGE";

void Sha1(const unsigned char* data, size_t len, unsigned char out[20]) {
    unsigned int h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
    const size_t total = ((len + 8) / 64 + 1) * 64;
    std::vector<unsigned char> m(total, 0);
    if (len) memcpy(m.data(), data, len);
    m[len] = 0x80;
    const unsigned long long bits = (unsigned long long)len * 8;
    for (int i = 0; i < 8; ++i) m[total - 1 - i] = (unsigned char)(bits >> (8 * i));

    for (size_t off = 0; off < total; off += 64) {
        unsigned int w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = ((unsigned int)m[off + i * 4] << 24) | ((unsigned int)m[off + i * 4 + 1] << 16) |
                   ((unsigned int)m[off + i * 4 + 2] << 8) | (unsigned int)m[off + i * 4 + 3];
        }
        for (int i = 16; i < 80; ++i) {
            const unsigned int v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (v << 1) | (v >> 31);
        }
        unsigned int a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            unsigned int f, k;
            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
            const unsigned int t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; ++i) {
        out[i * 4 + 0] = (unsigned char)(h[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(h[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(h[i] >> 8);
        out[i * 4 + 3] = (unsigned char)(h[i]);
    }
}

std::string Base64(const unsigned char* d, size_t n) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve(((n + 2) / 3) * 4);
    for (size_t i = 0; i < n; i += 3) {
        unsigned int v = (unsigned int)d[i] << 16;
        if (i + 1 < n) v |= (unsigned int)d[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned int)d[i + 2];
        o += T[(v >> 18) & 63];
        o += T[(v >> 12) & 63];
        o += (i + 1 < n) ? T[(v >> 6) & 63] : '=';
        o += (i + 2 < n) ? T[v & 63] : '=';
    }
    return o;
}

bool SendAll(SOCKET s, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        const int chunk = send(s, data + sent, (int)((std::min)(len - sent, (size_t)(64 * 1024))), 0);
        if (chunk <= 0) return false;
        sent += (size_t)chunk;
    }
    return true;
}

bool SendHead(SOCKET s, int status, const char* statusText, const char* contentType,
              size_t bodyLen, const char* extra) {
    char head[512];
    const int n = _snprintf_s(head, sizeof(head), _TRUNCATE,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %llu\r\n%s"
        "Connection: close\r\n\r\n",
        status, statusText, contentType, (unsigned long long)bodyLen,
        extra ? extra : "Cache-Control: no-store\r\n");
    return n > 0 && SendAll(s, head, (size_t)n);
}

bool SendResponse(SOCKET s, int status, const char* statusText, const char* contentType,
                  const void* body, size_t bodyLen, const char* extra = nullptr) {
    if (!SendHead(s, status, statusText, contentType, bodyLen, extra)) return false;
    return bodyLen == 0 || SendAll(s, (const char*)body, bodyLen);
}

bool SendFrame(SOCKET s, unsigned char opcode, const void* data, size_t len) {
    unsigned char hdr[10];
    size_t hn = 0;
    hdr[hn++] = (unsigned char)(0x80 | opcode);
    if (len < 126) {
        hdr[hn++] = (unsigned char)len;
    } else if (len < 65536) {
        hdr[hn++] = 126;
        hdr[hn++] = (unsigned char)(len >> 8);
        hdr[hn++] = (unsigned char)len;
    } else {
        hdr[hn++] = 127;
        for (int i = 7; i >= 0; --i) hdr[hn++] = (unsigned char)((unsigned long long)len >> (i * 8));
    }
    if (!SendAll(s, (const char*)hdr, hn)) return false;
    return len == 0 || SendAll(s, (const char*)data, len);
}

inline short RdI16(const unsigned char* p) {
    return (short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

inline double Clamp(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline double MapCoord(double v, double srcExtent, double dstExtent) {
    if (srcExtent <= 1.0 || dstExtent <= 1.0) return 0.0;
    return Clamp(v, 0.0, srcExtent - 1.0) * ((dstExtent - 1.0) / (srcExtent - 1.0));
}

bool FindPair(const std::string& s, const char* key, double* a, double* b) {
    const size_t p = s.find(key);
    if (p == std::string::npos) return false;
    return sscanf_s(s.c_str() + p + strlen(key), "%lf,%lf", a, b) == 2;
}

bool FindDims(const std::string& s, const char* key, int* w, int* h) {
    const size_t p = s.find(key);
    if (p == std::string::npos) return false;
    return sscanf_s(s.c_str() + p + strlen(key), "%dx%d", w, h) == 2;
}

bool FindInt(const std::string& s, const char* key, int* v) {
    const size_t p = s.find(key);
    if (p == std::string::npos) return false;
    return sscanf_s(s.c_str() + p + strlen(key), "%d", v) == 1;
}

class WebRelayServer {
public:
    void Start() {
        if (running_.load()) return;
        if (thread_.joinable()) thread_.join();
        stop_.store(false);
        running_.store(true);
        thread_ = std::thread([this]() { ThreadMain(); });
    }

    void Stop() {
        if (!running_.load()) {
            if (thread_.joinable()) thread_.join();
            return;
        }
        stop_.store(true);
        SOCKET ls = listenSocket_.exchange(INVALID_SOCKET);
        if (ls != INVALID_SOCKET) {
            shutdown(ls, SD_BOTH);
            closesocket(ls);
        }
        if (thread_.joinable()) thread_.join();
        running_.store(false);
    }

    bool Running() const { return running_.load(); }

private:
    struct Conn {
        SOCKET                     fd   = INVALID_SOCKET;
        bool                       isWs = false;
        std::vector<unsigned char> in;
    };

    std::atomic<bool>   running_{ false };
    std::atomic<bool>   stop_{ false };
    std::atomic<SOCKET> listenSocket_{ INVALID_SOCKET };
    std::thread         thread_;

    std::vector<Conn> conns_;
    SOCKET            udp_ = INVALID_SOCKET;
    sockaddr_in       udpDst_{};
    DWORD             lastProbe_ = 0;

    bool   menuMode_ = false;
    double winW_  = kProtocolWidth,  winH_  = kProtocolHeight;
    double menuW_ = 960.0,           menuH_ = 540.0;
    double mx_ = 480.0, my_ = 270.0;

    void ThreadMain() {
        WSADATA wsa = {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { running_.store(false); return; }

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) { WSACleanup(); running_.store(false); return; }

        BOOL reuse = TRUE;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        sockaddr_in addr = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons((unsigned short)kWebRelayPort);
        if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 8) != 0) {
            WriteLogF(L"Web relay bind/listen failed port=%d err=%d", kWebRelayPort, WSAGetLastError());
            closesocket(s);
            WSACleanup();
            running_.store(false);
            return;
        }
        listenSocket_.store(s);

        udp_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        udpDst_.sin_family = AF_INET;
        udpDst_.sin_port   = htons(kInputPort);
        inet_pton(AF_INET, "127.0.0.1", &udpDst_.sin_addr);

        Probe("hello");

        WriteLogF(L"Web mouse relay listening on port %d", kWebRelayPort);

        while (!stop_.load()) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(s, &readSet);
            if (udp_ != INVALID_SOCKET) FD_SET(udp_, &readSet);
            for (size_t i = 0; i < conns_.size() && i < FD_SETSIZE - 2; ++i)
                FD_SET(conns_[i].fd, &readSet);

            timeval tv = {};
            tv.tv_sec  = 0;
            tv.tv_usec = 250000;
            const int ready = select(0, &readSet, nullptr, nullptr, &tv);
            if (stop_.load()) break;

            if (GetTickCount() - lastProbe_ >= kProbeIntervalMs) Probe("ping");

            if (ready <= 0) continue;

            if (udp_ != INVALID_SOCKET && FD_ISSET(udp_, &readSet)) DrainStatus();
            if (FD_ISSET(s, &readSet)) AcceptOne(s);

            for (size_t i = 0; i < conns_.size();) {
                if (FD_ISSET(conns_[i].fd, &readSet) && !Service(conns_[i])) {
                    closesocket(conns_[i].fd);
                    conns_.erase(conns_.begin() + (ptrdiff_t)i);
                } else {
                    ++i;
                }
            }
        }

        for (Conn& c : conns_) if (c.fd != INVALID_SOCKET) closesocket(c.fd);
        conns_.clear();
        if (udp_ != INVALID_SOCKET) { closesocket(udp_); udp_ = INVALID_SOCKET; }
        SOCKET old = listenSocket_.exchange(INVALID_SOCKET);
        if (old != INVALID_SOCKET) closesocket(old);
        WSACleanup();
        WriteLog(L"Web mouse relay stopped");
    }

    void Probe(const char* what) {
        lastProbe_ = GetTickCount();
        if (udp_ == INVALID_SOCKET) return;
        sendto(udp_, what, (int)strlen(what), 0, (const sockaddr*)&udpDst_, sizeof(udpDst_));
    }

    void DrainStatus() {
        for (;;) {
            char buf[512];
            sockaddr_in from{};
            int fromLen = (int)sizeof(from);
            const int n = recvfrom(udp_, buf, (int)sizeof(buf) - 1, 0, (sockaddr*)&from, &fromLen);
            if (n <= 0) return;
            buf[n] = 0;
            OnStatus(std::string(buf, (size_t)n));
            u_long avail = 0;
            if (ioctlsocket(udp_, FIONREAD, &avail) != 0 || avail == 0) return;
        }
    }

    void OnStatus(const std::string& s) {
        int w = 0, h = 0;
        if (FindDims(s, "size=", &w, &h) && w > 1 && h > 1) {
            winW_ = (double)w;
            winH_ = (double)h;
        }
        if (FindDims(s, "menu=", &w, &h) && w > 1 && h > 1) {
            menuW_ = (double)w;
            menuH_ = (double)h;
        }
        if (menuW_ > winW_) menuW_ = winW_;
        if (menuH_ > winH_) menuH_ = winH_;

        double a = 0.0, b = 0.0;
        if (s.rfind("SYNCW:", 0) == 0 && sscanf_s(s.c_str() + 6, "%lf,%lf", &a, &b) == 2) {
            SeedMenu(MapCoord(a, winW_, menuW_), MapCoord(b, winH_, menuH_), false);
            return;
        }
        if (s.rfind("SYNC:", 0) == 0 && sscanf_s(s.c_str() + 5, "%lf,%lf", &a, &b) == 2) {
            SeedMenu(a * (menuW_ / kProtocolWidth), b * (menuH_ / kProtocolHeight), false);
            return;
        }

        double cx = 0.0, cy = 0.0;
        if (menuMode_ && FindPair(s, "cursorw=", &cx, &cy)) {
            SeedMenu(MapCoord(cx, winW_, menuW_), MapCoord(cy, winH_, menuH_), true);
        }

        bool newMenu  = menuMode_;
        bool haveMode = false;
        if (s.rfind("MODE:MENU", 0) == 0)          { newMenu = true;  haveMode = true; }
        else if (s.rfind("MODE:GAMEPLAY", 0) == 0) { newMenu = false; haveMode = true; }
        else {
            int md = 0;
            if (FindInt(s, "mode=", &md)) { newMenu = (md != kCursorModeDisabled); haveMode = true; }
        }

        if (haveMode && newMenu != menuMode_) {
            menuMode_ = newMenu;
            if (menuMode_) SeedMenu(menuW_ * 0.5, menuH_ * 0.5, false);
            WriteLogF(L"Web mouse relay mode -> %s", menuMode_ ? L"MENU" : L"GAMEPLAY");
            BroadcastMode();
        } else if (haveMode) {
            BroadcastMode();
        }
    }

    void SeedMenu(double x, double y, bool fromStatus) {
        mx_ = Clamp(x, 0.0, menuW_ - 1.0);
        my_ = Clamp(y, 0.0, menuH_ - 1.0);
        if (!fromStatus) EmitAbsolute(-1, -1, -1, 0.0);
    }

    void EmitAbsolute(int lb, int rb, int mb, double wheel) {
        if (udp_ == INVALID_SOCKET) return;
        char out[192];
        const int n = _snprintf_s(out, sizeof(out), _TRUNCATE,
            "ABSW:%.4f,%.4f,%d,%d,%d,%.4f,-1,-1",
            MapCoord(mx_, menuW_, winW_), MapCoord(my_, menuH_, winH_), lb, rb, mb, wheel);
        if (n > 0) sendto(udp_, out, n, 0, (const sockaddr*)&udpDst_, sizeof(udpDst_));
    }

    void EmitRelative(double dx, double dy, int lb, int rb, int mb, double wheel) {
        if (udp_ == INVALID_SOCKET) return;
        char out[160];
        const int n = _snprintf_s(out, sizeof(out), _TRUNCATE,
            "%.4f,%.4f,%d,%d,%d,%.4f,-1,-1", dx, dy, lb, rb, mb, wheel);
        if (n > 0) sendto(udp_, out, n, 0, (const sockaddr*)&udpDst_, sizeof(udpDst_));
    }

    void BroadcastMode() {
        const unsigned char pkt[2] = { kPktMode, (unsigned char)(menuMode_ ? 1 : 0) };
        for (Conn& c : conns_) if (c.isWs) SendFrame(c.fd, 0x2, pkt, sizeof(pkt));
    }

    void AcceptOne(SOCKET ls) {
        SOCKET c = accept(ls, nullptr, nullptr);
        if (c == INVALID_SOCKET) return;

        BOOL nodelay = TRUE;
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

        if (conns_.size() >= 8) {
            for (size_t i = 0; i < conns_.size(); ++i) {
                if (!conns_[i].isWs) {
                    closesocket(conns_[i].fd);
                    conns_.erase(conns_.begin() + (ptrdiff_t)i);
                    break;
                }
            }
        }
        Conn nc;
        nc.fd = c;
        conns_.push_back(std::move(nc));
    }

    bool Service(Conn& c) {
        char buf[4096];
        const int got = recv(c.fd, buf, sizeof(buf), 0);
        if (got <= 0) return false;
        c.in.insert(c.in.end(), (unsigned char*)buf, (unsigned char*)buf + got);
        if (c.in.size() > 256 * 1024) return false;
        return c.isWs ? ParseFrames(c) : ParseHttp(c);
    }

    bool ParseHttp(Conn& c) {
        const std::string raw((const char*)c.in.data(), c.in.size());
        const size_t end = raw.find("\r\n\r\n");
        if (end == std::string::npos) return true;

        std::string lower = raw.substr(0, end);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) { return (char)tolower(ch); });

        const size_t kp = lower.find("sec-websocket-key:");
        if (kp != std::string::npos && lower.find("upgrade") != std::string::npos) {
            size_t v = raw.find(':', kp) + 1;
            while (v < raw.size() && (raw[v] == ' ' || raw[v] == '\t')) ++v;
            const size_t lineEnd = raw.find("\r\n", v);
            if (lineEnd == std::string::npos) return false;
            const std::string key = raw.substr(v, lineEnd - v);

            const std::string cat = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            unsigned char digest[20];
            Sha1((const unsigned char*)cat.data(), cat.size(), digest);

            const std::string resp =
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " + Base64(digest, 20) + "\r\n\r\n";
            if (!SendAll(c.fd, resp.data(), resp.size())) return false;

            c.isWs = true;
            c.in.erase(c.in.begin(), c.in.begin() + (ptrdiff_t)(end + 4));
            WriteLog(L"Web mouse relay: client upgraded to WebSocket");

            const unsigned char pkt[2] = { kPktMode, (unsigned char)(menuMode_ ? 1 : 0) };
            SendFrame(c.fd, 0x2, pkt, sizeof(pkt));
            Probe("ping");
            return true;
        }

        size_t sp1 = raw.find(' ');
        size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : raw.find(' ', sp1 + 1);
        std::string target = (sp1 != std::string::npos && sp2 != std::string::npos)
                                 ? raw.substr(sp1 + 1, sp2 - sp1 - 1) : "/";
        const size_t q = target.find('?');
        const std::string path = (q == std::string::npos) ? target : target.substr(0, q);

        if (path == "/" || path == "/index.html") {
            SendResponse(c.fd, 200, "OK", "text/html; charset=utf-8",
                         kWebRelayPage, strlen(kWebRelayPage));
        } else if (path == "/icon-180.png" || path == "/apple-touch-icon.png" ||
                   path == "/apple-touch-icon-precomposed.png" || path == "/favicon.ico") {
            SendResponse(c.fd, 200, "OK", "image/png",
                         kIcon180Png, sizeof(kIcon180Png),
                         "Cache-Control: public, max-age=604800\r\n");
        } else if (path == "/manifest.webmanifest" || path == "/manifest.json") {
            SendResponse(c.fd, 200, "OK", "application/manifest+json",
                         kManifest, strlen(kManifest));
        } else if (path == "/health") {
            SendResponse(c.fd, 200, "OK", "text/plain", "ok", 2);
        } else {
            SendResponse(c.fd, 404, "Not Found", "text/plain", "Not found", 9);
        }
        return false;
    }

    bool ParseFrames(Conn& c) {
        std::vector<unsigned char>& b = c.in;
        for (;;) {
            if (b.size() < 2) return true;

            const int          opcode = b[0] & 0x0F;
            const bool         masked = (b[1] & 0x80) != 0;
            unsigned long long len    = b[1] & 0x7F;
            size_t             hdr    = 2;

            if (len == 126) {
                if (b.size() < 4) return true;
                len = ((unsigned long long)b[2] << 8) | b[3];
                hdr = 4;
            } else if (len == 127) {
                if (b.size() < 10) return true;
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | b[2 + i];
                hdr = 10;
            }
            if (len > 65536) return false;

            const size_t maskOff = hdr;
            const size_t payOff  = hdr + (masked ? 4u : 0u);
            if (b.size() < payOff + (size_t)len) return true;

            if (masked) {
                for (unsigned long long i = 0; i < len; ++i)
                    b[payOff + (size_t)i] ^= b[maskOff + (size_t)(i & 3ull)];
            }

            if (opcode == 0x8) return false;
            if (opcode == 0x9) SendFrame(c.fd, 0xA, b.data() + payOff, (size_t)len);
            else if (opcode == 0x1 || opcode == 0x2)
                OnPacket(c, b.data() + payOff, (size_t)len);

            b.erase(b.begin(), b.begin() + (ptrdiff_t)(payOff + (size_t)len));
        }
    }

    void OnPacket(Conn& c, const unsigned char* d, size_t n) {
        if (n < 1) return;

        if (d[0] == kPktPing && n >= 5) {
            SendFrame(c.fd, 0x2, d, n);
            return;
        }
        if (d[0] != kPktState || n < 9) return;

        const double dx = (double)RdI16(d + 1);
        const double dy = (double)RdI16(d + 3);
        const int buttons = d[5];
        const double wheel = (double)(int)(signed char)d[6];
        const int lb = (buttons & 1) ? 1 : 0;
        const int rb = (buttons & 2) ? 1 : 0;
        const int mb = (buttons & 4) ? 1 : 0;

        if (menuMode_) {
            mx_ = Clamp(mx_ + dx, 0.0, menuW_ - 1.0);
            my_ = Clamp(my_ + dy, 0.0, menuH_ - 1.0);
            EmitAbsolute(lb, rb, mb, wheel);
        } else {
            EmitRelative(dx, dy, lb, rb, mb, wheel);
        }
    }
};

WebRelayServer g_webRelayServer;

}

void StartWebRelayServer() { g_webRelayServer.Start(); }
void StopWebRelayServer()  { g_webRelayServer.Stop(); }
bool WebRelayServerRunning() { return g_webRelayServer.Running(); }

#include <string>
#include <thread>
#include <vector>

#include "launcher_common.h"

#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr int            kWebRelayPort = 6090;
constexpr unsigned short kInputPort    = 7331;

enum : unsigned char { kPktState = 1, kPktPing = 4 };

const char* const kWebRelayPage = R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,minimum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="Bandit Mouse">
<title>Bandit Web Mouse Support</title>
<style>
:root{--line:#243444;--muted:#9eb0bf;--text:#edf4f8;--accent:#70c486;--accent2:#69b7cc;}
*{box-sizing:border-box;-webkit-user-select:none;user-select:none;-webkit-tap-highlight-color:transparent;-webkit-touch-callout:none;}
/* position:fixed + overscroll-behavior:none are what actually stop iOS
   rubber-band scrolling. overflow:hidden alone does not. */
html,body{margin:0;height:100%;overflow:hidden;position:fixed;inset:0;overscroll-behavior:none;}
body{font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:radial-gradient(circle at top,#102030,#071018 60%);color:var(--text);display:flex;flex-direction:column;height:100dvh;touch-action:none;}
header{display:flex;align-items:center;justify-content:space-between;padding:12px 16px;border-bottom:1px solid var(--line);}
.brand{display:flex;align-items:center;gap:10px;font-weight:600;}
.logo{width:22px;height:22px;border-radius:6px;background:linear-gradient(135deg,var(--accent),var(--accent2));}
.dot{width:10px;height:10px;border-radius:50%;background:#4a5a68;transition:background .2s;}
.dot.live{background:var(--accent);box-shadow:0 0 10px var(--accent);}
.hdr-right{display:flex;align-items:center;gap:10px;color:var(--muted);font-size:13px;font-variant-numeric:tabular-nums;}
.stage{flex:1;display:flex;flex-direction:column;min-height:0;}
/* touch-action is NOT an inherited property, so setting it on body did not
   cover the pad. It has to be declared here. */
#pad{flex:1;margin:14px;border:1px solid var(--line);border-radius:16px;background:linear-gradient(180deg,#0d1822,#0a131c);display:flex;align-items:center;justify-content:center;text-align:center;color:var(--muted);position:relative;overflow:hidden;touch-action:none;overscroll-behavior:none;}
#pad .hint{padding:24px;max-width:430px;transition:opacity .2s;}
#pad .hint b{color:var(--text);display:block;font-size:17px;margin-bottom:6px;}
#pad.live{border-color:var(--accent);}
#pad.touching .hint{opacity:0;}
.controls{display:flex;flex-direction:column;gap:8px;padding:0 14px 14px;}
.row{display:flex;gap:8px;}
.btn{flex:1 1 0;min-width:60px;min-height:54px;border:1px solid var(--line);border-radius:12px;background:#0e1a24;color:var(--text);font:inherit;font-weight:600;display:flex;align-items:center;justify-content:center;touch-action:none;}
.btn:active,.btn.on{background:#16313f;border-color:var(--accent2);color:#fff;}
.btn.wide{flex:2 1 0;}
.sens{display:flex;align-items:center;gap:10px;color:var(--muted);font-size:13px;padding-top:2px;}
.sens input{flex:1;}
.hidden{display:none!important;}
@media (orientation:landscape) and (max-height:560px){
.stage{flex-direction:row;}
#pad{margin:10px 8px 10px 12px;}
.controls{width:240px;justify-content:center;padding:10px 12px 10px 8px;}
.btn{min-height:44px;}
header{padding:8px 16px;}
}
</style>
</head>
<body>
<header>
  <div class="brand"><span class="logo"></span>Bandit Web Mouse Support</div>
  <div class="hdr-right"><span id="stats"></span><span id="statetext">connecting</span><span class="dot" id="dot"></span></div>
</header>
<div class="stage">
<div id="pad"><div class="hint"><b>Touchpad</b>Drag to move</div></div>
<div class="controls">
  <div class="row">
    <button class="btn wide" id="bl">Left</button>
    <button class="btn" id="bm">Middle</button>
    <button class="btn wide" id="br">Right</button>
  </div>
  <div class="row">
    <button class="btn" id="su">Scroll +</button>
    <button class="btn" id="sd">Scroll -</button>
    <button class="btn hidden" id="fs">Fullscreen</button>
  </div>
  <div class="sens"><span>Speed</span><input type="range" id="sens" min="0.4" max="4" step="0.1" value="1.2"><span id="sensv">1.2x</span></div>
</div>
</div>
<script>
(function(){
  'use strict';
  var PKT_STATE=1, PKT_PING=4;
  var dx=0,dy=0,scroll=0,l=0,r=0,m=0,sens=1.2,dirty=false,captured=false,live=false;
  var pad=document.getElementById('pad'),dot=document.getElementById('dot');
  var statetext=document.getElementById('statetext'),stats=document.getElementById('stats');
  var sensEl=document.getElementById('sens'),sensv=document.getElementById('sensv');
  sensEl.addEventListener('input',function(){sens=parseFloat(sensEl.value);sensv.textContent=sens.toFixed(1)+'x';});
  function setLive(v){if(v!==live){live=v;dot.classList.toggle('live',v);pad.classList.toggle('live',v);}}
  function setBtn(w,v){if(w===0)l=v;else if(w===1)m=v;else if(w===2)r=v;dirty=true;}

    var ws=null, wsOpen=false, backoff=250, rtt=null, tx=0;
  function connect(){
    var proto=(location.protocol==='https:')?'wss://':'ws://';
    try{ ws=new WebSocket(proto+location.host+'/ws'); }catch(e){ setTimeout(connect,backoff); return; }
    ws.binaryType='arraybuffer';
    ws.onopen=function(){wsOpen=true;backoff=250;setLive(true);statetext.textContent=captured?'mouse captured':'connected';};
    ws.onclose=function(){wsOpen=false;setLive(false);statetext.textContent='reconnecting';
      setTimeout(connect,backoff);backoff=Math.min(backoff*2,4000);};
    ws.onerror=function(){};
    ws.onmessage=function(ev){
      if(!(ev.data instanceof ArrayBuffer)||ev.data.byteLength<5)return;
      var v=new DataView(ev.data);
      if(v.getUint8(0)===PKT_PING){
        var s=((performance.now()|0)-v.getUint32(1,true));
        if(s>=0&&s<5000) rtt=(rtt===null)?s:(rtt*0.7+s*0.3);
      }
    };
  }

  var stateBuf=new ArrayBuffer(9), stateV=new DataView(stateBuf);
  var pingBuf=new ArrayBuffer(5),  pingV=new DataView(pingBuf);
  var seq=0;
  function clamp(v,lo,hi){return v<lo?lo:(v>hi?hi:v);}

  
  function sendState(sx,sy,sc){
    if(!wsOpen||ws.readyState!==1)return false;
    if(ws.bufferedAmount>4096)return false;
    stateV.setUint8(0,PKT_STATE);
    stateV.setInt16(1,clamp(sx,-32768,32767),true);
    stateV.setInt16(3,clamp(sy,-32768,32767),true);
    stateV.setUint8(5,(l?1:0)|(r?2:0)|(m?4:0));
    stateV.setInt8(6,clamp(sc,-127,127));
    stateV.setUint16(7,seq,true);
    ws.send(stateBuf); seq=(seq+1)&0xffff; tx++;
    return true;
  }
  function sendPing(){
    if(!wsOpen||ws.readyState!==1)return;
    pingV.setUint8(0,PKT_PING);
    pingV.setUint32(1,performance.now()|0,true);
    ws.send(pingBuf);
  }

    if('requestPointerLock' in pad){
    pad.addEventListener('click',function(){ if(!captured) pad.requestPointerLock(); });
    document.addEventListener('pointerlockchange',function(){
      captured=(document.pointerLockElement===pad);
      statetext.textContent=captured?'mouse captured':(wsOpen?'connected':'reconnecting');
    });
  }
  pad.addEventListener('mousemove',function(e){
    if(e.pointerType==='touch')return;
    if(captured){dx+=e.movementX*sens;dy+=e.movementY*sens;}
  });
  pad.addEventListener('mousedown',function(e){setBtn(e.button,1);e.preventDefault();});
  window.addEventListener('mouseup',function(e){setBtn(e.button,0);});
  pad.addEventListener('contextmenu',function(e){e.preventDefault();});
  pad.addEventListener('wheel',function(e){scroll+=(e.deltaY<0?1:-1);dirty=true;e.preventDefault();},{passive:false});

    var pts=new Map();          
    var OPT={passive:false};

  function ptDown(e){
    e.preventDefault();
    try{pad.setPointerCapture(e.pointerId);}catch(_){}
    pts.set(e.pointerId,{x:e.clientX,y:e.clientY});
    pad.classList.add('touching');
  }
  function ptMove(e){
    var st=pts.get(e.pointerId); if(!st)return;
    e.preventDefault();
    var list=(typeof e.getCoalescedEvents==='function')?(e.getCoalescedEvents()||[e]):[e];
    if(!list.length)list=[e];
    var ax=0,ay=0;
    for(var i=0;i<list.length;i++){
      ax+=list[i].clientX-st.x; ay+=list[i].clientY-st.y;
      st.x=list[i].clientX;     st.y=list[i].clientY;
    }
    if(pts.size>=2){ scroll+=(ay<-2?1:(ay>2?-1:0)); if(ay<-2||ay>2)dirty=true; }
    else { dx+=ax*sens*1.6; dy+=ay*sens*1.6; }
  }
  function ptUp(e){
    if(!pts.has(e.pointerId))return;
    e.preventDefault();
    pts.delete(e.pointerId);
    if(pts.size===0)pad.classList.remove('touching');
  }
  pad.addEventListener('pointerdown',ptDown,OPT);
  pad.addEventListener('pointermove',ptMove,OPT);
  pad.addEventListener('pointerup',ptUp,OPT);
  pad.addEventListener('pointercancel',ptUp,OPT);   

    pad.addEventListener('touchstart',function(e){e.preventDefault();},OPT);
  pad.addEventListener('touchmove',function(e){e.preventDefault();},OPT);
  pad.addEventListener('touchend',function(e){e.preventDefault();},OPT);
  pad.addEventListener('gesturestart',function(e){e.preventDefault();},OPT);
  document.addEventListener('touchmove',function(e){e.preventDefault();},OPT);

  document.addEventListener('visibilitychange',function(){
    if(document.visibilityState==='visible'){
      pts.clear(); dx=dy=scroll=0; l=r=m=0; dirty=true;
      pad.classList.remove('touching');
    }
  });

    function holdBtn(id,w){
    var el=document.getElementById(id);
    var dn=function(e){setBtn(w,1);el.classList.add('on');e.preventDefault();};
    var up=function(e){setBtn(w,0);el.classList.remove('on');e.preventDefault();};
    el.addEventListener('pointerdown',dn,OPT);
    el.addEventListener('pointerup',up,OPT);
    el.addEventListener('pointercancel',up,OPT);
    el.addEventListener('touchstart',function(e){e.preventDefault();},OPT);
  }
  holdBtn('bl',0);holdBtn('bm',1);holdBtn('br',2);
  function scrollBtn(id,a){
    var el=document.getElementById(id);
    var go=function(e){scroll+=a;dirty=true;e.preventDefault();};
    el.addEventListener('pointerdown',go,OPT);
    el.addEventListener('touchstart',function(e){e.preventDefault();},OPT);
  }
  scrollBtn('su',1);scrollBtn('sd',-1);

  var fsEl=document.documentElement,fsReq=fsEl.requestFullscreen||fsEl.webkitRequestFullscreen,fsBtn=document.getElementById('fs');
  if(fsReq){fsBtn.classList.remove('hidden');fsBtn.addEventListener('click',function(){var d=document,isFs=d.fullscreenElement||d.webkitFullscreenElement;if(isFs){(d.exitFullscreen||d.webkitExitFullscreen).call(d);}else{fsReq.call(fsEl);}});}

 
  var lastSend=0,lastPing=0,lastStat=0,frames=0;
  function loop(now){
    requestAnimationFrame(loop);
      var sx=Math.round(dx),sy=Math.round(dy);
    if((sx||sy||scroll||dirty)&&now-lastSend>=6){
      if(sendState(sx,sy,scroll)){dx-=sx;dy-=sy;scroll=0;dirty=false;lastSend=now;}
    }else if(now-lastSend>=700){
           if(sendState(0,0,0))lastSend=now;
    }
    if(now-lastPing>=500){sendPing();lastPing=now;}
    frames++;
    if(now-lastStat>=1000){
      var fps=frames*1000/(now-lastStat);
      stats.textContent=(rtt===null?'--':rtt.toFixed(0)+'ms')+' · '+tx+'/s'+
                        (fps<45?' · '+fps.toFixed(0)+'fps (Low Power Mode?)':'');
      frames=0;tx=0;lastStat=now;
    }
  }
  requestAnimationFrame(loop);
  if('wakeLock' in navigator){navigator.wakeLock.request('screen').catch(function(){});}
  connect();
})();
</script>
</body>
</html>
)PAGE";

void Sha1(const unsigned char* data, size_t len, unsigned char out[20]) {
    unsigned int h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
    const size_t total = ((len + 8) / 64 + 1) * 64;
    std::vector<unsigned char> m(total, 0);
    if (len) memcpy(m.data(), data, len);
    m[len] = 0x80;
    const unsigned long long bits = (unsigned long long)len * 8;
    for (int i = 0; i < 8; ++i) m[total - 1 - i] = (unsigned char)(bits >> (8 * i));

    for (size_t off = 0; off < total; off += 64) {
        unsigned int w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = ((unsigned int)m[off + i * 4] << 24) | ((unsigned int)m[off + i * 4 + 1] << 16) |
                   ((unsigned int)m[off + i * 4 + 2] << 8) | (unsigned int)m[off + i * 4 + 3];
        }
        for (int i = 16; i < 80; ++i) {
            const unsigned int v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (v << 1) | (v >> 31);
        }
        unsigned int a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            unsigned int f, k;
            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
            const unsigned int t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; ++i) {
        out[i * 4 + 0] = (unsigned char)(h[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(h[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(h[i] >> 8);
        out[i * 4 + 3] = (unsigned char)(h[i]);
    }
}

std::string Base64(const unsigned char* d, size_t n) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve(((n + 2) / 3) * 4);
    for (size_t i = 0; i < n; i += 3) {
        unsigned int v = (unsigned int)d[i] << 16;
        if (i + 1 < n) v |= (unsigned int)d[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned int)d[i + 2];
        o += T[(v >> 18) & 63];
        o += T[(v >> 12) & 63];
        o += (i + 1 < n) ? T[(v >> 6) & 63] : '=';
        o += (i + 2 < n) ? T[v & 63] : '=';
    }
    return o;
}

bool SendAll(SOCKET s, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        const int chunk = send(s, data + sent, (int)((std::min)(len - sent, (size_t)(64 * 1024))), 0);
        if (chunk <= 0) return false;
        sent += (size_t)chunk;
    }
    return true;
}

bool SendResponse(SOCKET s, int status, const char* statusText, const char* contentType,
                  const std::string& body) {
    char head[512];
    const int n = _snprintf_s(head, sizeof(head), _TRUNCATE,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %llu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        status, statusText, contentType, (unsigned long long)body.size());
    if (n <= 0) return false;
    if (!SendAll(s, head, (size_t)n)) return false;
    return body.empty() || SendAll(s, body.data(), body.size());
}


bool SendFrame(SOCKET s, unsigned char opcode, const void* data, size_t len) {
    unsigned char hdr[10];
    size_t hn = 0;
    hdr[hn++] = (unsigned char)(0x80 | opcode);
    if (len < 126) {
        hdr[hn++] = (unsigned char)len;
    } else if (len < 65536) {
        hdr[hn++] = 126;
        hdr[hn++] = (unsigned char)(len >> 8);
        hdr[hn++] = (unsigned char)len;
    } else {
        hdr[hn++] = 127;
        for (int i = 7; i >= 0; --i) hdr[hn++] = (unsigned char)((unsigned long long)len >> (i * 8));
    }
    if (!SendAll(s, (const char*)hdr, hn)) return false;
    return len == 0 || SendAll(s, (const char*)data, len);
}

inline short RdI16(const unsigned char* p) {
    return (short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}
inline unsigned int RdU32(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

class WebRelayServer {
public:
    void Start() {
        if (running_.load()) return;
        if (thread_.joinable()) thread_.join();
        stop_.store(false);
        running_.store(true);
        thread_ = std::thread([this]() { ThreadMain(); });
    }

    void Stop() {
        if (!running_.load()) {
            if (thread_.joinable()) thread_.join();
            return;
        }
        stop_.store(true);
        SOCKET ls = listenSocket_.exchange(INVALID_SOCKET);
        if (ls != INVALID_SOCKET) {
            shutdown(ls, SD_BOTH);
            closesocket(ls);
        }
        if (thread_.joinable()) thread_.join();
        running_.store(false);
    }

    bool Running() const { return running_.load(); }

private:
    struct Conn {
        SOCKET                     fd   = INVALID_SOCKET;
        bool                       isWs = false;
        std::vector<unsigned char> in;
    };

    std::atomic<bool>   running_{ false };
    std::atomic<bool>   stop_{ false };
    std::atomic<SOCKET> listenSocket_{ INVALID_SOCKET };
    std::thread         thread_;

    std::vector<Conn> conns_;
    SOCKET            udp_ = INVALID_SOCKET;
    sockaddr_in       udpDst_{};

    void ThreadMain() {
        WSADATA wsa = {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { running_.store(false); return; }

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) { WSACleanup(); running_.store(false); return; }

        BOOL reuse = TRUE;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        sockaddr_in addr = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons((unsigned short)kWebRelayPort);
        if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 8) != 0) {
            WriteLogF(L"Web relay bind/listen failed port=%d err=%d", kWebRelayPort, WSAGetLastError());
            closesocket(s);
            WSACleanup();
            running_.store(false);
            return;
        }
        listenSocket_.store(s);

        
        udp_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        udpDst_.sin_family = AF_INET;
        udpDst_.sin_port   = htons(kInputPort);
        inet_pton(AF_INET, "127.0.0.1", &udpDst_.sin_addr);

        WriteLogF(L"Web mouse relay listening on port %d (WebSocket)", kWebRelayPort);

        while (!stop_.load()) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(s, &readSet);
            for (size_t i = 0; i < conns_.size() && i < FD_SETSIZE - 1; ++i)
                FD_SET(conns_[i].fd, &readSet);

            timeval tv = {};
            tv.tv_sec  = 0;
            tv.tv_usec = 250000;
            if (select(0, &readSet, nullptr, nullptr, &tv) <= 0) continue;
            if (stop_.load()) break;

            if (FD_ISSET(s, &readSet)) AcceptOne(s);

            for (size_t i = 0; i < conns_.size();) {
                if (FD_ISSET(conns_[i].fd, &readSet) && !Service(conns_[i])) {
                    closesocket(conns_[i].fd);
                    conns_.erase(conns_.begin() + (ptrdiff_t)i);
                } else {
                    ++i;
                }
            }
        }

        for (Conn& c : conns_) if (c.fd != INVALID_SOCKET) closesocket(c.fd);
        conns_.clear();
        if (udp_ != INVALID_SOCKET) { closesocket(udp_); udp_ = INVALID_SOCKET; }
        SOCKET old = listenSocket_.exchange(INVALID_SOCKET);
        if (old != INVALID_SOCKET) closesocket(old);
        WSACleanup();
        WriteLog(L"Web mouse relay stopped");
    }

    void AcceptOne(SOCKET ls) {
        SOCKET c = accept(ls, nullptr, nullptr);
        if (c == INVALID_SOCKET) return;

                BOOL nodelay = TRUE;
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

       
        if (conns_.size() >= 8) {
            for (size_t i = 0; i < conns_.size(); ++i) {
                if (!conns_[i].isWs) {
                    closesocket(conns_[i].fd);
                    conns_.erase(conns_.begin() + (ptrdiff_t)i);
                    break;
                }
            }
        }
        Conn nc;
        nc.fd = c;
        conns_.push_back(std::move(nc));
    }

    bool Service(Conn& c) {
        char buf[4096];
        const int got = recv(c.fd, buf, sizeof(buf), 0);
        if (got <= 0) return false;
        c.in.insert(c.in.end(), (unsigned char*)buf, (unsigned char*)buf + got);
        if (c.in.size() > 256 * 1024) return false;
        return c.isWs ? ParseFrames(c) : ParseHttp(c);
    }

    bool ParseHttp(Conn& c) {
        const std::string raw((const char*)c.in.data(), c.in.size());
        const size_t end = raw.find("\r\n\r\n");
        if (end == std::string::npos) return true;              
        std::string lower = raw.substr(0, end);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) { return (char)tolower(ch); });

        const size_t kp = lower.find("sec-websocket-key:");
        if (kp != std::string::npos && lower.find("upgrade") != std::string::npos) {
            size_t v = raw.find(':', kp) + 1;
            while (v < raw.size() && (raw[v] == ' ' || raw[v] == '\t')) ++v;
            const size_t lineEnd = raw.find("\r\n", v);
            if (lineEnd == std::string::npos) return false;
            const std::string key = raw.substr(v, lineEnd - v);

            const std::string cat = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            unsigned char digest[20];
            Sha1((const unsigned char*)cat.data(), cat.size(), digest);

            const std::string resp =
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " + Base64(digest, 20) + "\r\n\r\n";
            if (!SendAll(c.fd, resp.data(), resp.size())) return false;

            c.isWs = true;
            c.in.erase(c.in.begin(), c.in.begin() + (ptrdiff_t)(end + 4));
            WriteLog(L"Web mouse relay: client upgraded to WebSocket");
            return true;
        }

        
        size_t sp1 = raw.find(' ');
        size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : raw.find(' ', sp1 + 1);
        std::string target = (sp1 != std::string::npos && sp2 != std::string::npos)
                                 ? raw.substr(sp1 + 1, sp2 - sp1 - 1) : "/";
        const size_t q = target.find('?');
        const std::string path = (q == std::string::npos) ? target : target.substr(0, q);

        if (path == "/" || path == "/index.html")
            SendResponse(c.fd, 200, "OK", "text/html; charset=utf-8", kWebRelayPage);
        else if (path == "/health")
            SendResponse(c.fd, 200, "OK", "text/plain", "ok");
        else
            SendResponse(c.fd, 404, "Not Found", "text/plain", "Not found");
        return false;
    }

    bool ParseFrames(Conn& c) {
        std::vector<unsigned char>& b = c.in;
        for (;;) {
            if (b.size() < 2) return true;

            const int           opcode = b[0] & 0x0F;
            const bool          masked = (b[1] & 0x80) != 0;
            unsigned long long  len    = b[1] & 0x7F;
            size_t              hdr    = 2;

            if (len == 126) {
                if (b.size() < 4) return true;
                len = ((unsigned long long)b[2] << 8) | b[3];
                hdr = 4;
            } else if (len == 127) {
                if (b.size() < 10) return true;
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | b[2 + i];
                hdr = 10;
            }
            if (len > 65536) return false;

            const size_t maskOff = hdr;
            const size_t payOff  = hdr + (masked ? 4u : 0u);
            if (b.size() < payOff + (size_t)len) return true;

            if (masked) {
                for (unsigned long long i = 0; i < len; ++i)
                    b[payOff + (size_t)i] ^= b[maskOff + (size_t)(i & 3ull)];
            }

            if (opcode == 0x8) return false;                      
            if (opcode == 0x9) SendFrame(c.fd, 0xA, b.data() + payOff, (size_t)len);
            else if (opcode == 0x1 || opcode == 0x2)
                OnPacket(c, b.data() + payOff, (size_t)len);

            b.erase(b.begin(), b.begin() + (ptrdiff_t)(payOff + (size_t)len));
        }
    }

    void OnPacket(Conn& c, const unsigned char* d, size_t n) {
        if (n < 1) return;

        if (d[0] == kPktPing && n >= 5) {
            SendFrame(c.fd, 0x2, d, n);                 return;
        }
        if (d[0] != kPktState || n < 9) return;

        const int dx = RdI16(d + 1);
        const int dy = RdI16(d + 3);
        const int buttons = d[5];
        const int scroll  = (int)(signed char)d[6];
        const int lb = (buttons & 1) ? 1 : 0;
        const int rb = (buttons & 2) ? 1 : 0;
        const int mb = (buttons & 4) ? 1 : 0;

                char out[96];
        const int k = _snprintf_s(out, sizeof(out), _TRUNCATE,
                                  "%d,%d,%d,%d,%d,%d,-1,-1", dx, dy, lb, rb, mb, scroll);
        if (k > 0 && udp_ != INVALID_SOCKET)
            sendto(udp_, out, k, 0, (const sockaddr*)&udpDst_, sizeof(udpDst_));
    }
};

WebRelayServer g_webRelayServer;

}  

void StartWebRelayServer() { g_webRelayServer.Start(); }
void StopWebRelayServer()  { g_webRelayServer.Stop(); }
bool WebRelayServerRunning() { return g_webRelayServer.Running(); }
