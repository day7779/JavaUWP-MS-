#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
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
