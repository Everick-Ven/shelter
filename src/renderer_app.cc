#include "src/renderer_app.h"

#include <string>

#include "include/cef_parser.h"
#include "src/common.h"

namespace shelter {

void AppBase::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  registrar->AddCustomScheme(
      kUiScheme, CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
                     CEF_SCHEME_OPTION_CORS_ENABLED |
                     CEF_SCHEME_OPTION_FETCH_ENABLED);
}

void AppBase::OnWebKitInitialized() {
  CefMessageRouterConfig config;  // window.cefQuery / window.cefQueryCancel
  router_ = CefMessageRouterRendererSide::Create(config);
}

namespace {

// «Анти-отпечаток» включён браузерным процессом (сообщение shelter.fp).
bool g_fp_enabled = false;

// window.cefQuery выдаём ТОЛЬКО chrome-UI. Обычные сайты доступа к мосту не получают.
bool IsUiFrame(CefRefPtr<CefFrame> frame) {
  if (!frame || !frame->IsMain()) return false;
  const std::string url = frame->GetURL().ToString();
  return url.rfind(kUiOriginPrefix, 0) == 0;
}

/* Скрипт защиты от снятия отпечатка (принцип как у Tor/Brave fingerprint
   defense): не блокирует API, а вносит стабильный в пределах сессии шум и
   стандартизирует сигналы, так что собранный «отпечаток» становится
   недостоверным, а поведение страниц не ломается. Ставится в
   OnContextCreated — раньше любого скрипта страницы. */
const char kFpScript[] =
    "(function(){'use strict';if(window.__shFp)return;window.__shFp=1;"
    "var seed=(Math.random()*0xFFFFFFFF)>>>0;"
    "function rnd(){seed^=seed<<13;seed>>>=0;seed^=seed>>>17;seed^=seed<<5;"
    "seed>>>=0;return seed/0xFFFFFFFF;}"
    "var marked={};function mark(t){if(marked[t])return;marked[t]=1;"
    "try{console.log('__SHELTER_FP__:'+t);}catch(_){}}"
    // Canvas 2D: разрежённый шум в getImageData + toDataURL/toBlob через
    // зашумлённую копию (подход хамелеона): данные те же визуально, хэш другой.
    "var GI=CanvasRenderingContext2D.prototype.getImageData;"
    "CanvasRenderingContext2D.prototype.getImageData=function(){"
    "var d=GI.apply(this,arguments);var a=d.data;"
    "for(var i=0;i<a.length;i+=97){var n=(rnd()*2)|0;if(n)a[i]^=n;}"
    "mark('canvas');return d;};"
    "function noised(c){try{var t=document.createElement('canvas');"
    "t.width=c.width;t.height=c.height;var x=t.getContext('2d');"
    "x.drawImage(c,0,0);var id=x.getImageData(0,0,t.width,t.height);"
    "x.putImageData(id,0,0);return t;}catch(_){return c;}}"
    "var TD=HTMLCanvasElement.prototype.toDataURL;"
    "HTMLCanvasElement.prototype.toDataURL=function(){"
    "mark('canvas');return TD.apply(noised(this),arguments);};"
    "var TB=HTMLCanvasElement.prototype.toBlob;"
    "HTMLCanvasElement.prototype.toBlob=function(cb,ty,q){"
    "mark('canvas');return TB.call(noised(this),cb,ty,q);};"
    // WebGL: стандартизованные vendor/renderer (как у всех «железок» Tor).
    "function glGet(orig){return function(p){"
    "if(p===37445){mark('webgl');return 'Intel Inc.';}"
    "if(p===37446){mark('webgl');return 'Intel Iris OpenGL Engine';}"
    "return orig.apply(this,arguments);};}"
    "if(window.WebGLRenderingContext)"
    "WebGLRenderingContext.prototype.getParameter="
    "glGet(WebGLRenderingContext.prototype.getParameter);"
    "if(window.WebGL2RenderingContext)"
    "WebGL2RenderingContext.prototype.getParameter="
    "glGet(WebGL2RenderingContext.prototype.getParameter);"
    // Audio: микроскопический шум в getChannelData — на слух неразличим,
    // аудио-отпечаток меняет.
    "var CD=AudioBuffer.prototype.getChannelData;"
    "AudioBuffer.prototype.getChannelData=function(ch){"
    "var d=CD.call(this,ch);"
    "for(var i=0;i<d.length;i+=512)d[i]+=(rnd()-0.5)*1e-5;"
    "mark('audio');return d;};"
    // Стандартизация части navigator-сигналов.
    "try{Object.defineProperty(navigator,'hardwareConcurrency',"
    "{get:function(){return 4;}});}catch(_){}"
    "try{Object.defineProperty(navigator,'deviceMemory',"
    "{get:function(){return 8;}});}catch(_){}"
    "mark('on');})();";

}  // namespace

void AppBase::OnContextCreated(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefV8Context> context) {
  if (router_ && IsUiFrame(frame)) {
    router_->OnContextCreated(browser, frame, context);
    return;
  }
  // Анти-отпечаток: только обычные веб-страницы (http/https), никогда не UI.
  if (g_fp_enabled && context && frame) {
    const std::string url = frame->GetURL().ToString();
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
      CefRefPtr<CefV8Value> retval;
      CefRefPtr<CefV8Exception> exc;
      context->Eval(CefString(kFpScript), CefString("shelter://anti-fp"), 0,
                    retval, exc);
    }
  }
}

void AppBase::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefV8Context> context) {
  if (router_ && IsUiFrame(frame)) {
    router_->OnContextReleased(browser, frame, context);
  }
}

bool AppBase::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       CefProcessId source_process,
                                       CefRefPtr<CefProcessMessage> message) {
  const std::string name = message->GetName().ToString();
  if (name == "shelter.fp" && source_process == PID_BROWSER) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    g_fp_enabled = args && args->GetSize() > 0 && args->GetBool(0);
    return true;
  }
  return router_ &&
         router_->OnProcessMessageReceived(browser, frame, source_process,
                                           message);
}

}  // namespace shelter
