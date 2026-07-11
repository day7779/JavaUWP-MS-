#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>

#include "launcher_common.h"

#pragma comment(lib, "ws2_32.lib")

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

namespace {

constexpr int kWebRelayPort = 6090;
constexpr unsigned short kInputPort = 7331;
constexpr size_t kMaxWsPayload = 4096;

const char* const kWebRelayPage = R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="Bandit Mouse">
<title>Bandit Web Mouse Support</title>
<style>
:root{--line:#243444;--muted:#9eb0bf;--text:#edf4f8;--accent:#70c486;--accent2:#69b7cc;}
*{box-sizing:border-box;-webkit-user-select:none;user-select:none;-webkit-tap-highlight-color:transparent;}
html,body{margin:0;height:100%;overflow:hidden;}
body{font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:radial-gradient(circle at top,#102030,#071018 60%);color:var(--text);display:flex;flex-direction:column;height:100dvh;touch-action:none;}
header{display:flex;align-items:center;justify-content:space-between;padding:12px 16px;border-bottom:1px solid var(--line);}
.brand{display:flex;align-items:center;gap:10px;font-weight:600;}
.logo{width:22px;height:22px;border-radius:6px;background:linear-gradient(135deg,var(--accent),var(--accent2));}
.dot{width:10px;height:10px;border-radius:50%;background:#4a5a68;transition:background .2s;}
.dot.live{background:var(--accent);box-shadow:0 0 10px var(--accent);}
.hdr-right{display:flex;align-items:center;gap:10px;color:var(--muted);font-size:13px;}
.stage{flex:1;display:flex;flex-direction:column;min-height:0;}
#pad{flex:1;margin:14px;border:1px solid var(--line);border-radius:16px;background:linear-gradient(180deg,#0d1822,#0a131c);display:flex;align-items:center;justify-content:center;text-align:center;color:var(--muted);position:relative;overflow:hidden;}
#pad .hint{padding:24px;max-width:430px;}
#pad .hint b{color:var(--text);display:block;font-size:17px;margin-bottom:6px;}
#pad.live{border-color:var(--accent);}
.controls{display:flex;flex-direction:column;gap:8px;padding:0 14px 14px;}
.row{display:flex;gap:8px;}
.btn{flex:1 1 0;min-width:60px;min-height:54px;border:1px solid var(--line);border-radius:12px;background:#0e1a24;color:var(--text);font:inherit;font-weight:600;display:flex;align-items:center;justify-content:center;}
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
  <div class="hdr-right"><span id="statetext">tap pad to start</span><span class="dot" id="dot"></span></div>
</header>
<div class="stage">
<div id="pad"><div class="hint"><b>Touchpad</b>Drag to move. Click with a mouse for 1:1 capture.</div></div>
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
  <div class="sens"><span>Speed</span><input type="range" id="sens" min="0.4" max="3" step="0.1" value="1"><span id="sensv">1.0x</span></div>
</div>
</div>
<script>
(function(){
  'use strict';
  var dx=0,dy=0,scroll=0,l=0,r=0,m=0,sens=1.0,dirty=false,captured=false,live=false;
  var pad=document.getElementById('pad'),dot=document.getElementById('dot'),statetext=document.getElementById('statetext');
  var sensEl=document.getElementById('sens'),sensv=document.getElementById('sensv');
  var ws=null,wsOk=false,lastWsAttempt=-10000,fails=0,lastPing=0,lastSend=0;
  sensEl.addEventListener('input',function(){sens=parseFloat(sensEl.value);sensv.textContent=sens.toFixed(1)+'x';});
  function setLive(v){if(v!==live){live=v;dot.classList.toggle('live',v);pad.classList.toggle('live',v);}}
  function stateLabel(){statetext.textContent=(captured?'mouse captured':'tap pad to start')+(wsOk?'':' (http mode)');}
  function connectWs(now){
    if(ws||now-lastWsAttempt<2000||!('WebSocket' in window))return;
    lastWsAttempt=now;
    try{
      var s=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws');
      s.onopen=function(){if(ws===s){wsOk=true;fails=0;setLive(true);stateLabel();dirty=true;flush();}};
      s.onclose=function(){if(ws===s){ws=null;if(wsOk)setLive(false);wsOk=false;stateLabel();}};
      s.onerror=function(){try{s.close();}catch(err){}};
      s.onmessage=function(){};
      ws=s;
    }catch(err){ws=null;wsOk=false;}
  }
  function postSend(b){fetch('/input',{method:'POST',body:b}).then(function(){fails=0;setLive(true);}).catch(function(){if(++fails>20)setLive(false);});}
  function send(b){
    if(wsOk&&ws&&ws.readyState===1){
      try{ws.send(b);return;}catch(err){wsOk=false;}
    }
    postSend(b);
  }
  function flush(){
    var sx=Math.round(dx),sy=Math.round(dy);
    if(!sx&&!sy&&!scroll&&!dirty)return;
    var now=performance.now();
    if(!wsOk&&!scroll&&!dirty&&now-lastSend<8)return;
    var s=scroll;
    send(sx+','+sy+','+l+','+r+','+m+','+s+',-1,-1');
    dx-=sx;dy-=sy;scroll-=s;dirty=false;lastSend=now;lastPing=now;
  }
  function setBtn(w,v){
    var c=false;
    if(w===0&&l!==v){l=v;c=true;}
    else if(w===1&&m!==v){m=v;c=true;}
    else if(w===2&&r!==v){r=v;c=true;}
    if(c){dirty=true;flush();}
  }
  function syncMouseButtons(bm){
    var nl=(bm&1)?1:0,nr=(bm&2)?1:0,nm=(bm&4)?1:0,c=false;
    if(nl!==l){l=nl;c=true;}
    if(nr!==r){r=nr;c=true;}
    if(nm!==m){m=nm;c=true;}
    if(c){dirty=true;flush();}
  }
  function capturePointer(){
    if(captured||!pad.requestPointerLock)return;
    try{
      var res=pad.requestPointerLock({unadjustedMovement:true});
      if(res&&res.catch)res.catch(function(){if(!captured)pad.requestPointerLock();});
    }catch(err){pad.requestPointerLock();}
  }
  document.addEventListener('pointerlockchange',function(){captured=(document.pointerLockElement===pad);stateLabel();});
  var lastMouse=null,lastPointers=new Map();
  pad.addEventListener('pointerdown',function(e){
    if(e.pointerType==='mouse'){
      capturePointer();
      if(typeof e.buttons==='number'){syncMouseButtons(e.buttons);}
      else if(e.button<=2){setBtn(e.button,1);}
    }else{
      lastPointers.set(e.pointerId,{x:e.clientX,y:e.clientY});
      if(pad.setPointerCapture){try{pad.setPointerCapture(e.pointerId);}catch(err){}}
    }
    e.preventDefault();
  });
  var motionEvent=('onpointerrawupdate' in window)?'pointerrawupdate':'pointermove';
  pad.addEventListener(motionEvent,function(e){
    if(e.pointerType==='mouse'){
      if(typeof e.buttons==='number'){syncMouseButtons(e.buttons);}
      if(captured){
        var samples=e.getCoalescedEvents?e.getCoalescedEvents():[e];
        if(!samples.length)samples=[e];
        var mx=0,my=0;
        for(var i=0;i<samples.length;i++){mx+=samples[i].movementX;my+=samples[i].movementY;}
        if(mx!==0||my!==0){dx+=mx*sens;dy+=my*sens;flush();}
      }else if(lastMouse){
        var hx=(e.clientX-lastMouse.x)*sens,hy=(e.clientY-lastMouse.y)*sens;
        if(hx!==0||hy!==0){dx+=hx;dy+=hy;flush();}
      }
      lastMouse={x:e.clientX,y:e.clientY};
    }else{
      var prev=lastPointers.get(e.pointerId);
      if(prev){
        var tx=(e.clientX-prev.x)*sens*1.6,ty=(e.clientY-prev.y)*sens*1.6;
        if(tx!==0||ty!==0){dx+=tx;dy+=ty;flush();}
        lastPointers.set(e.pointerId,{x:e.clientX,y:e.clientY});
      }
    }
  });
  function endPointer(e){
    if(e.pointerType==='mouse'){
      if(typeof e.buttons==='number'){syncMouseButtons(e.buttons);}
      else if(e.button<=2){setBtn(e.button,0);}
    }else{
      lastPointers.delete(e.pointerId);
    }
  }
  window.addEventListener('pointerup',endPointer,{passive:true});
  window.addEventListener('pointercancel',endPointer,{passive:true});
  pad.addEventListener('pointerleave',function(e){if(e.pointerType==='mouse'){lastMouse=null;}else{lastPointers.delete(e.pointerId);}});
  pad.addEventListener('contextmenu',function(e){e.preventDefault();});
  pad.addEventListener('wheel',function(e){scroll+=(e.deltaY<0?1:-1);dirty=true;flush();e.preventDefault();},{passive:false});
  function holdBtn(id,w){var el=document.getElementById(id);
    var dn=function(e){setBtn(w,1);el.classList.add('on');if(el.setPointerCapture){try{el.setPointerCapture(e.pointerId);}catch(err){}}e.preventDefault();};
    var up=function(e){setBtn(w,0);el.classList.remove('on');e.preventDefault();};
    el.addEventListener('pointerdown',dn);el.addEventListener('pointerup',up);
    el.addEventListener('pointercancel',up);el.addEventListener('lostpointercapture',up);}
  holdBtn('bl',0);holdBtn('bm',1);holdBtn('br',2);
  function scrollBtn(id,a){var el=document.getElementById(id);
    el.addEventListener('pointerdown',function(e){scroll+=a;dirty=true;flush();e.preventDefault();});}
  scrollBtn('su',1);scrollBtn('sd',-1);
  var fsEl=document.documentElement,fsReq=fsEl.requestFullscreen||fsEl.webkitRequestFullscreen,fsBtn=document.getElementById('fs');
  if(fsReq){fsBtn.classList.remove('hidden');fsBtn.addEventListener('click',function(){var d=document,isFs=d.fullscreenElement||d.webkitFullscreenElement;if(isFs){(d.exitFullscreen||d.webkitExitFullscreen).call(d);}else{fsReq.call(fsEl);}});}
  function loop(){
    var now=performance.now();
    connectWs(now);
    flush();
    if(now-lastPing>=500){
      send('0,0,'+l+','+r+','+m+',0,-1,-1');lastPing=now;
    }
    requestAnimationFrame(loop);
  }
  requestAnimationFrame(loop);
})();
</script>
</body>
</html>
)PAGE";

bool SendAll(SOCKET s, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        const int chunk = send(s, data + sent, (int)((std::min)(len - sent, (size_t)(64 * 1024))), 0);
        if (chunk <= 0) return false;
        sent += (size_t)chunk;
    }
    return true;
}

bool SendResponse(SOCKET s, int status, const char* statusText, const char* contentType, const std::string& body) {
    std::ostringstream head;
    head << "HTTP/1.1 " << status << " " << statusText << "\r\n"
         << "Content-Type: " << contentType << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Cache-Control: no-store\r\n"
         << "Access-Control-Allow-Origin: *\r\n"
         << "Connection: keep-alive\r\n\r\n";
    std::string payload = head.str();
    payload += body;
    return SendAll(s, payload.data(), payload.size());
}

bool ReadRequest(SOCKET s, std::string& method, std::string& path, std::string& headers, std::string& body, std::string& leftover) {
    std::string data;
    char buffer[4096];
    size_t headerEnd = std::string::npos;
    while (data.size() < 256 * 1024) {
        const int read = recv(s, buffer, sizeof(buffer), 0);
        if (read <= 0) return false;
        data.append(buffer, (size_t)read);
        headerEnd = data.find("\r\n\r\n");
        if (headerEnd != std::string::npos) break;
    }
    if (headerEnd == std::string::npos) return false;

    headers = data.substr(0, headerEnd);
    const size_t firstLineEnd = headers.find("\r\n");
    const std::string firstLine = headers.substr(0, firstLineEnd == std::string::npos ? headers.size() : firstLineEnd);
    std::istringstream first(firstLine);
    std::string target, version;
    first >> method >> target >> version;
    const size_t q = target.find('?');
    path = q == std::string::npos ? target : target.substr(0, q);

    size_t contentLength = 0;
    std::string lower = headers;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
    const size_t cl = lower.find("content-length:");
    if (cl != std::string::npos) {
        contentLength = (size_t)strtoul(headers.c_str() + cl + 15, nullptr, 10);
    }
    if (contentLength > 4096) contentLength = 4096;

    body = data.substr(headerEnd + 4);
    while (body.size() < contentLength) {
        const int read = recv(s, buffer, sizeof(buffer), 0);
        if (read <= 0) return false;
        body.append(buffer, (size_t)read);
    }
    leftover.clear();
    if (body.size() > contentLength) {
        leftover = body.substr(contentLength);
        body.resize(contentLength);
    }
    return true;
}

bool ValidPacket(const std::string& body) {
    if (body.empty() || body.size() > 96) return false;
    int commas = 0;
    for (char c : body) {
        if (c >= '0' && c <= '9') continue;
        if (c == ',') { ++commas; continue; }
        if (c == '-' || c == '.') continue;
        return false;
    }
    return commas == 7;
}

std::string HeaderValue(const std::string& headers, const char* name) {
    std::string lower = headers;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)tolower(c); });
    key += ":";
    size_t pos = 0;
    while (true) {
        pos = lower.find(key, pos);
        if (pos == std::string::npos) return std::string();
        if (pos == 0 || lower[pos - 1] == '\n') break;
        pos += key.size();
    }
    size_t start = pos + key.size();
    size_t end = headers.find("\r\n", start);
    if (end == std::string::npos) end = headers.size();
    std::string value = headers.substr(start, end - start);
    const size_t first = value.find_first_not_of(" \t");
    const size_t last = value.find_last_not_of(" \t");
    if (first == std::string::npos) return std::string();
    return value.substr(first, last - first + 1);
}

bool HeaderContainsToken(const std::string& value, const char* token) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
    return lower.find(token) != std::string::npos;
}

void Sha1(const unsigned char* data, size_t len, unsigned char out[20]) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    const uint64_t bitLen = (uint64_t)len * 8;
    size_t paddedLen = len + 1;
    while (paddedLen % 64 != 56) ++paddedLen;
    paddedLen += 8;
    std::string padded((const char*)data, len);
    padded.push_back((char)0x80);
    padded.resize(paddedLen, '\0');
    for (int i = 0; i < 8; ++i) {
        padded[paddedLen - 8 + i] = (char)((bitLen >> (56 - i * 8)) & 0xFF);
    }
    for (size_t chunk = 0; chunk < paddedLen; chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = ((uint32_t)(unsigned char)padded[chunk + i * 4] << 24) |
                   ((uint32_t)(unsigned char)padded[chunk + i * 4 + 1] << 16) |
                   ((uint32_t)(unsigned char)padded[chunk + i * 4 + 2] << 8) |
                   ((uint32_t)(unsigned char)padded[chunk + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            const uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (v << 1) | (v >> 31);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            const uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    const uint32_t hs[5] = { h0, h1, h2, h3, h4 };
    for (int i = 0; i < 5; ++i) {
        out[i * 4] = (unsigned char)((hs[i] >> 24) & 0xFF);
        out[i * 4 + 1] = (unsigned char)((hs[i] >> 16) & 0xFF);
        out[i * 4 + 2] = (unsigned char)((hs[i] >> 8) & 0xFF);
        out[i * 4 + 3] = (unsigned char)(hs[i] & 0xFF);
    }
}

std::string Base64(const unsigned char* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        const unsigned int b0 = data[i];
        const unsigned int b1 = i + 1 < len ? data[i + 1] : 0;
        const unsigned int b2 = i + 2 < len ? data[i + 2] : 0;
        out.push_back(table[b0 >> 2]);
        out.push_back(table[((b0 & 0x03) << 4) | (b1 >> 4)]);
        out.push_back(i + 1 < len ? table[((b1 & 0x0F) << 2) | (b2 >> 6)] : '=');
        out.push_back(i + 2 < len ? table[b2 & 0x3F] : '=');
    }
    return out;
}

std::string WebSocketAcceptKey(const std::string& clientKey) {
    const std::string combined = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char digest[20];
    Sha1((const unsigned char*)combined.data(), combined.size(), digest);
    return Base64(digest, sizeof(digest));
}

bool SendWebSocketFrame(SOCKET s, unsigned char opcode, const std::string& payload) {
    std::string frame;
    frame.push_back((char)(0x80 | (opcode & 0x0F)));
    if (payload.size() < 126) {
        frame.push_back((char)payload.size());
    } else {
        frame.push_back((char)126);
        frame.push_back((char)((payload.size() >> 8) & 0xFF));
        frame.push_back((char)(payload.size() & 0xFF));
    }
    frame += payload;
    return SendAll(s, frame.data(), frame.size());
}

class WebSocketReader {
public:
    explicit WebSocketReader(std::string seed) : buffer_(std::move(seed)) {}

    bool NextFrame(SOCKET s, unsigned char& opcode, std::string& payload, const std::atomic<bool>& stop) {
        for (;;) {
            if (stop.load()) return false;
            size_t need = 2;
            if (buffer_.size() >= 2) {
                const unsigned char b0 = (unsigned char)buffer_[0];
                const unsigned char b1 = (unsigned char)buffer_[1];
                const bool masked = (b1 & 0x80) != 0;
                uint64_t len = (uint64_t)(b1 & 0x7F);
                size_t headerLen = 2;
                if (len == 126) headerLen += 2;
                else if (len == 127) headerLen += 8;
                if (masked) headerLen += 4;
                if (buffer_.size() >= headerLen) {
                    if ((b1 & 0x7F) == 126) {
                        len = ((uint64_t)(unsigned char)buffer_[2] << 8) | (uint64_t)(unsigned char)buffer_[3];
                    } else if ((b1 & 0x7F) == 127) {
                        len = 0;
                        for (int i = 0; i < 8; ++i) {
                            len = (len << 8) | (uint64_t)(unsigned char)buffer_[2 + i];
                        }
                    }
                    if (len > kMaxWsPayload || !masked) {
                        return false;
                    }
                    const size_t total = headerLen + (size_t)len;
                    if (buffer_.size() >= total) {
                        opcode = (unsigned char)(b0 & 0x0F);
                        const unsigned char* mask = (const unsigned char*)buffer_.data() + headerLen - 4;
                        payload.assign(buffer_.data() + headerLen, (size_t)len);
                        for (size_t i = 0; i < payload.size(); ++i) {
                            payload[i] = (char)((unsigned char)payload[i] ^ mask[i % 4]);
                        }
                        buffer_.erase(0, total);
                        return true;
                    }
                    need = total;
                } else {
                    need = headerLen;
                }
            }
            char chunk[2048];
            const int read = recv(s, chunk, sizeof(chunk), 0);
            if (read <= 0) return false;
            buffer_.append(chunk, (size_t)read);
            (void)need;
        }
    }

private:
    std::string buffer_;
};

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
    std::atomic<bool> running_{ false };
    std::atomic<bool> stop_{ false };
    std::atomic<SOCKET> listenSocket_{ INVALID_SOCKET };
    std::thread thread_;

    void ThreadMain() {
        WSADATA wsa = {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            running_.store(false);
            return;
        }
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            WSACleanup();
            running_.store(false);
            return;
        }
        BOOL reuse = TRUE;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons((unsigned short)kWebRelayPort);
        if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 8) != 0) {
            WriteLogF(L"Web relay bind/listen failed port=%d err=%d", kWebRelayPort, WSAGetLastError());
            closesocket(s);
            WSACleanup();
            running_.store(false);
            return;
        }
        listenSocket_.store(s);
        WriteLogF(L"Web mouse relay listening on port %d", kWebRelayPort);

        while (!stop_.load()) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(s, &readSet);
            timeval tv = {};
            tv.tv_sec = 0;
            tv.tv_usec = 250000;
            if (select(0, &readSet, nullptr, nullptr, &tv) <= 0) continue;
            SOCKET client = accept(s, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            if (stop_.load()) {
                closesocket(client);
                break;
            }
            BOOL noDelay = TRUE;
            setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));
            std::thread([this, client]() {
                HandleClient(client);
                closesocket(client);
            }).detach();
        }

        SOCKET old = listenSocket_.exchange(INVALID_SOCKET);
        if (old != INVALID_SOCKET) closesocket(old);
        WSACleanup();
        WriteLog(L"Web mouse relay stopped");
    }

    SOCKET OpenInputSocket(sockaddr_in& dst) {
        SOCKET udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        dst = {};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(kInputPort);
        inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);
        if (udp != INVALID_SOCKET) {
            BOOL behavior = FALSE;
            DWORD bytes = 0;
            WSAIoctl(udp, SIO_UDP_CONNRESET, &behavior, sizeof(behavior), nullptr, 0, &bytes, nullptr, nullptr);
        }
        return udp;
    }

    void HandleWebSocket(SOCKET client, const std::string& headers, std::string leftover) {
        const std::string key = HeaderValue(headers, "Sec-WebSocket-Key");
        if (key.empty()) {
            SendResponse(client, 400, "Bad Request", "text/plain", "Missing websocket key");
            return;
        }
        std::ostringstream resp;
        resp << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << WebSocketAcceptKey(key) << "\r\n\r\n";
        const std::string head = resp.str();
        if (!SendAll(client, head.data(), head.size())) return;
        WriteLog(L"Web relay websocket client connected");

        sockaddr_in dst = {};
        SOCKET udp = OpenInputSocket(dst);

        WebSocketReader reader(std::move(leftover));
        unsigned char opcode = 0;
        std::string payload;
        while (!stop_.load() && reader.NextFrame(client, opcode, payload, stop_)) {
            if (opcode == 0x1 || opcode == 0x2) {
                if (udp != INVALID_SOCKET && ValidPacket(payload)) {
                    sendto(udp, payload.data(), (int)payload.size(), 0, (const sockaddr*)&dst, sizeof(dst));
                }
            } else if (opcode == 0x9) {
                if (!SendWebSocketFrame(client, 0xA, payload)) break;
            } else if (opcode == 0x8) {
                SendWebSocketFrame(client, 0x8, std::string());
                break;
            }
        }

        if (udp != INVALID_SOCKET) closesocket(udp);
        WriteLog(L"Web relay websocket client disconnected");
    }

    void HandleClient(SOCKET client) {
        sockaddr_in dst = {};
        SOCKET udp = OpenInputSocket(dst);

        while (!stop_.load()) {
            std::string method, path, headers, body, leftover;
            if (!ReadRequest(client, method, path, headers, body, leftover)) break;

            if (method == "GET" && path == "/ws" &&
                HeaderContainsToken(HeaderValue(headers, "Upgrade"), "websocket")) {
                if (udp != INVALID_SOCKET) {
                    closesocket(udp);
                    udp = INVALID_SOCKET;
                }
                HandleWebSocket(client, headers, std::move(leftover));
                return;
            }

            if (method == "POST" && path == "/input") {
                if (udp != INVALID_SOCKET && ValidPacket(body)) {
                    sendto(udp, body.data(), (int)body.size(), 0, (const sockaddr*)&dst, sizeof(dst));
                }
                if (!SendResponse(client, 204, "No Content", "text/plain", std::string())) break;
            } else if (method == "GET" && (path == "/" || path == "/index.html")) {
                if (!SendResponse(client, 200, "OK", "text/html; charset=utf-8", kWebRelayPage)) break;
            } else if (method == "GET" && path == "/health") {
                if (!SendResponse(client, 200, "OK", "text/plain", "ok")) break;
            } else {
                if (!SendResponse(client, 404, "Not Found", "text/plain", "Not found")) break;
            }
        }

        if (udp != INVALID_SOCKET) closesocket(udp);
    }
};

WebRelayServer g_webRelayServer;

}

void StartWebRelayServer() { g_webRelayServer.Start(); }
void StopWebRelayServer() { g_webRelayServer.Stop(); }
bool WebRelayServerRunning() { return g_webRelayServer.Running(); }
