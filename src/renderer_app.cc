#include "src/renderer_app.h"

#include <string>

#include "include/cef_command_line.h"

#include "include/cef_parser.h"
#include "src/common.h"
#include "src/protection_flags.h"

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

// Автосогласие cookies включено браузерным процессом (сообщение
// shelter.cookies). По умолчанию — включено: баннер согласия не должен
// появляться перед пользователем вообще, ещё до ответа UI настройками.
bool g_auto_consent = true;

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

/* Автосогласие cookies (стандарт 2026: у каждого CMP есть вариант «только
   необходимое»). Скрипт ставится в каждый веб-фрейм до кода страницы:
   прячет известные баннеры согласия стилем, находит кнопку минимального
   набора и нажимает её один раз; если минимального варианта нет — «отклонить
   всё», и лишь затем единственная кнопка «принять всё». Если решить не
   удалось, стиль снимается, чтобы спрятанный баннер не заблокировал сайт. */
const char kCookieScript[] = R"SHCONSENT((function () {
  'use strict';
  if (window.__shConsent) return;
  window.__shConsent = 1;

  /* Автосогласие cookies: пользователь не должен видеть баннер вообще.
     1) в документ сразу ставится стиль, скрывающий известные CMP-контейнеры;
     2) сканер ищет в них кнопку «только необходимое» и нажимает её;
     3) если минимального варианта нет — «отклонить всё», и лишь затем
        «принять всё» (единственная кнопка на сайте);
     4) если решить не удалось за 2.5 с, стиль снимается, чтобы баннер не
        остался скрытым, но нерешённым, и не заблокировал сайт. */

  var STYLE_ID = 'sh-cookie-hide';

  /* Известные CMP и типовые контейнеры баннеров согласия. */
  var HIDE = [
    '#onetrust-banner-sdk', '#onetrust-pc-sdk', '.onetrust-pc-dark-filter',
    '#ot-sdk-btn-floating', '#CybotCookiebotDialog',
    '#CybotCookiebotDialogBodyUnderlay', '#cookiebanner',
    '#usercentrics-root', '[data-testid="uc-container"]', '.uc-banner-modal',
    '.uc-banner', '#qc-cmp2-container', '.qc-cmp2-container', '#didomi-host',
    '.didomi-popup-container', '.didomi-host', '#gdpr-banner',
    '#cookie-consent', '#cookieConsent', '#cookie-notice',
    '#cookie-law-info-bar', '.cookie-banner', '.cookie-consent',
    '.consent-banner', '.gdpr-banner', '.privacy-banner', '.cookie-popup',
    '.cookie-alert', '.cookies-eu-banner', '.js-cookie-consent', '.cc-window',
    '.cc-banner', '#cc--main', '.cc-overlay', '.osano-cm-window',
    '#osano-cm-window', '#iubenda-cs-banner', '.iubenda-cs-container',
    '.iubenda-cs-overlay', '#klaro .cookie-modal', '#klaro .cookie-notice',
    '.klaro .cookie-modal', '.fc-consent-root', '[id^="fc-consent"]',
    '.fc-dialog-overlay', '[yandex-cookie-policy]', '#yandex-cookie-policy',
    '[id*="cookie" i][class*="banner" i]',
    '[class*="cookie" i][class*="banner" i]',
    '[class*="consent" i][class*="banner" i]',
    'iframe[src*="consent" i]', 'iframe[src*="privacy-mgmt" i]',
    'iframe[src*="usercentrics" i]', 'iframe[src*="cookiebot" i]',
    'iframe[src*="onetrust" i]', 'iframe[src*="iubenda" i]',
    'iframe[src*="cookielaw" i]', 'iframe[src*="sourcepoint" i]',
    'iframe[src*="quantcast" i]', 'iframe[src*="didomi" i]',
    'iframe[src*="osano" i]', 'iframe[src*="consentmanager" i]',
    'iframe[id*="sp_message" i]', 'iframe[name*="sp_message" i]'
  ];

  /* Кнопки известных CMP: сначала «только необходимое»/отказ, затем «принять». */
  var KNOWN = [
    ['#onetrust-reject-all-handler', '#onetrust-accept-btn-handler'],
    ['#CybotCookiebotDialogBodyButtonDecline',
     '#CybotCookiebotDialogBodyButtonAccept'],
    ['#didomi-notice-disagree-button', '#didomi-notice-agree-button'],
    ['[data-testid="uc-deny-all-button"]',
     '[data-testid="uc-accept-all-button"]'],
    ['.osano-cm-deny', '.osano-cm-accept'],
    ['.fc-cta-do-not-consent', '.fc-cta-consent'],
    ['.iubenda-cs-reject-btn', '.iubenda-cs-accept-btn']
  ];

  /* Тексты кнопок: минимальный набор (в приоритете), отказ, «принять всё». */
  var MIN_SUB = [
    'только необходим', 'только обяз', 'только техничес',
    'принять необходим', 'принять только', 'принять обяз',
    'разрешить необходим', 'разрешить только', 'использовать необходим',
    'необходимые cookie', 'необходимые куки', 'только необхідн',
    'тільки необхідн', 'лише необхідн', 'необхідні cookie',
    'only necessary', 'necessary only', 'only essential',
    'essential only', 'strictly necessary', 'necessary cookies',
    'essential cookies', 'accept necessary', 'accept only necess',
    'accept only essent', 'accept essential', 'accept required',
    'use necessary', 'only required', 'nur notwendige',
    'nur erforderliche', 'uniquement nécessaire',
    'seulement nécessaire', 'uniquement les cookies nécessaires',
    'accepter les nécessaires', 'solo las necesarias', 'solo necesarias',
    'sólo las necesarias', 'aceptar las necesarias',
    'aceptar solo las necesarias'
  ];
  var REJECT_SUB = [
    'отклонить', 'отказаться', 'не согласен', 'не соглашаться',
    'запретить', 'заблокировать все', 'reject all', 'reject non-essential',
    'reject optional', 'decline all', 'decline', 'deny all', 'refuse all',
    'do not sell', 'rechazar', 'refuser', 'ablehnen', 'відхилити',
    'відмовитись'
  ];
  var MIN_EXACT = [
    'необходимые', 'только необходимые', 'только обязательные',
    'необходимые cookies', 'necessary', 'essential', 'necessary cookies',
    'essentiels', 'nécessaires', 'necesarias', 'esenciales',
    'notwendige'
  ];
  var REJECT_EXACT = [
    'отклонить', 'отклонить все', 'отказаться', 'reject', 'reject all',
    'decline', 'decline all', 'ablehnen', 'rechazar', 'refuser'
  ];
  var ACCEPT_SUB = [
    'принять все', 'принять и закрыть', 'принять и продолжить',
    'принять куки', 'принять cookies', 'разрешить все', 'согласен со всем',
    'accept all', 'allow all', 'accept and close', 'accept and continue',
    'accept cookies', 'alles akzeptieren', 'alle akzeptieren',
    'tout accepter', 'accepter tout', 'aceptar todo', 'aceptar todas',
    'aceptar y cerrar'
  ];
  var ACCEPT_EXACT = [
    'согласен', 'согласна', 'соглашаюсь', 'принять', 'принимаю', 'хорошо',
    'ок', 'окей', 'понятно', 'продолжить', 'i agree', 'agree', 'accept',
    'got it', 'ok', 'okay', 'allow', 'allow all', 'accept all',
    'погоджуюсь', 'прийняти'
  ];

  function norm(text) {
    return String(text == null ? '' : text)
      .replace(/\s+/g, ' ')
      .replace(/ё/g, 'е')
      .trim()
      .toLowerCase();
  }
  function has(text, list) {
    for (var i = 0; i < list.length; i++) {
      if (list[i] && text.indexOf(list[i]) !== -1) return true;
    }
    return false;
  }
  function is(text, list) {
    for (var i = 0; i < list.length; i++) {
      if (text === list[i]) return true;
    }
    return false;
  }
  function labelOf(el) {
    return norm(
      el.getAttribute('aria-label') ||
      el.getAttribute('title') ||
      el.value ||
      el.textContent
    );
  }
  function styleNode() {
    return document.getElementById(STYLE_ID);
  }
  function applyStyle() {
    if (styleNode()) return true;
    var root = document.head || document.documentElement;
    if (!root) return false;
    var s = document.createElement('style');
    s.id = STYLE_ID;
    s.setAttribute('data-shelter', 'consent');
    s.textContent = HIDE.join(',') +
      '{display:none !important;visibility:hidden !important;' +
      'opacity:0 !important;pointer-events:none !important}';
    root.appendChild(s);
    return true;
  }
  function dropStyle() {
    var s = styleNode();
    if (s && s.parentNode) s.parentNode.removeChild(s);
  }

  /* Обход дерева с учётом теневых DOM (Usercentrics и подобные). */
  function eachRoot(root, fn, depth) {
    fn(root);
    if ((depth || 0) > 6) return;
    var all;
    try { all = root.querySelectorAll('*'); } catch (_) { return; }
    for (var i = 0; i < all.length; i++) {
      var sr = all[i].shadowRoot;
      if (sr) eachRoot(sr, fn, (depth || 0) + 1);
    }
  }

  /* Похоже на баннер согласия, а не на пользовательскую панель настроек. */
  var BANNER_WORDS =
    /banner|notice|notification|popup|overlay|modal|dialog|alert|gdpr|consent|cookie-?law|cookie-?consent|cookiebar|cookie-bar|cookie-?notice|onetrust|didomi|usercentrics|osano|iubenda|klaro|cc-window|qc-cmp|cmp-|sp_message/i;

  function looksLikeBanner(el) {
    try {
      var cs = getComputedStyle(el);
      var cls = (el.id || '') + ' ' + (el.className || '');
      if (BANNER_WORDS.test(cls)) return true;
      var role = (el.getAttribute('role') || '').toLowerCase();
      if (role === 'dialog' || role === 'alertdialog') return true;
      if (el.getAttribute('aria-modal') === 'true') return true;
      if (!/cookie|consent|gdpr|cmp|privacy/i.test(cls)) return false;
      if (cs.position === 'fixed' || cs.position === 'sticky') return true;
      if (parseInt(cs.zIndex || '0', 10) >= 100) return true;
      var r = el.getBoundingClientRect();
      if (window.innerWidth > 0 && r.width >= window.innerWidth * 0.5 &&
          r.height > 60) {
        return true;
      }
    } catch (_) {}
    return false;
  }

  function candidatesFor(root, sub, exact) {
    var found = [];
    var nodes;
    try {
      nodes = root.querySelectorAll(
        'button,[role="button"],a,input[type="button"],input[type="submit"]'
      );
    } catch (_) { return found; }
    for (var i = 0; i < nodes.length; i++) {
      var el = nodes[i];
      if (el.disabled) continue;
      var text = labelOf(el);
      if (!text) continue;
      if (has(text, sub) || is(text, exact)) found.push(el);
    }
    return found;
  }

  function collect() {
    var min = [], reject = [], accept = [], i;

    /* 1. Явные кнопки известных CMP. */
    for (i = 0; i < KNOWN.length; i++) {
      eachRoot(document, (function (pair) {
        return function (root) {
          var low = root.querySelector(pair[0]);
          var high = root.querySelector(pair[1]);
          if (low) min.push(low);
          if (high) accept.push(high);
        };
      })(KNOWN[i]));
    }

    /* 2. Кнопки внутри контейнеров, похожих на баннер согласия. */
    eachRoot(document, function (root) {
      var nodes;
      try {
        nodes = root.querySelectorAll(
          '[id*="cookie" i],[class*="cookie" i],[id*="consent" i],' +
          '[class*="consent" i],[id*="gdpr" i],[class*="gdpr" i],' +
          '[id*="cmp" i],[class*="cmp-" i],[data-testid*="uc-" i],' +
          '[role="dialog"],[aria-modal="true"],.cc-window,.fc-dialog-overlay'
        );
      } catch (_) { return; }
      for (var j = 0; j < nodes.length; j++) {
        var box = nodes[j];
        if (box === document.body || box === document.documentElement) continue;
        if (!looksLikeBanner(box)) continue;
        var texts;
        try { texts = box.innerText || ''; } catch (_) { texts = ''; }
        if (!texts && !box.querySelector('button,input,[role="button"]')) {
          continue;
        }
        min = min.concat(candidatesFor(box, MIN_SUB, MIN_EXACT));
        reject = reject.concat(candidatesFor(box, REJECT_SUB, REJECT_EXACT));
        accept = accept.concat(candidatesFor(box, ACCEPT_SUB, ACCEPT_EXACT));
      }
    });

    return min.concat(reject, accept);
  }

  /* Ровно один клик: повторная эмуляция событий включает обработчик дважды
     и может переключить согласие туда-обратно. */
  function clickIt(el) {
    try {
      if (typeof el.click === 'function') { el.click(); return true; }
    } catch (_) {}
    try {
      el.dispatchEvent(new MouseEvent('click', {
        bubbles: true, cancelable: true, view: window
      }));
      return true;
    } catch (_) {}
    return false;
  }

  function knownBanner() {
    for (var i = 0; i < HIDE.length; i++) {
      var el;
      try { el = document.querySelector(HIDE[i]); } catch (_) { continue; }
      if (el && el !== styleNode() && el !== document.body &&
          el !== document.documentElement) {
        return el;
      }
    }
    return null;
  }

  var clicked = false;
  var started = Date.now();
  var scheduled = false;

  /* Один баннер — один клик. Никаких «добьём это, потом нажмём принять всё»:
     повышать согласие задним числом недопустимо. Если клик не сработал,
     скрывающий стиль снимается, чтобы пользователь не остался с баннером,
     который спрятан, но не принят. */
  var scans = 0;
  var observer = null;
  var timer = null;

  function stop() {
    try { if (observer) observer.disconnect(); } catch (_) {}
    try { if (timer) clearInterval(timer); } catch (_) {}
  }

  function tick() {
    if (clicked) return;
    /* Сканирование ограничено: десятки проходов и полторы минуты на страницу,
       чтобы не тратить процессор сайтов с постоянными изменениями DOM. */
    scans++;
    if (scans > 40 || Date.now() - started > 90000) { stop(); return; }
    applyStyle();
    var list = collect();
    var el = list[0] || null;
    if (!el) {
      if (!knownBanner() && Date.now() - started > 60000) { dropStyle(); stop(); }
      return;
    }
    clicked = true;
    started = Date.now();
    clickIt(el);
    setTimeout(function () {
      if (knownBanner()) dropStyle();
    }, 2500);
  }

  function schedule() {
    if (clicked || scheduled) return;
    scheduled = true;
    setTimeout(function () { scheduled = false; tick(); }, 80);
  }

  applyStyle();
  if (!styleNode()) {
    try {
      var boot = new MutationObserver(function () {
        if (applyStyle()) boot.disconnect();
      });
      boot.observe(document, { childList: true, subtree: true });
    } catch (_) {}
  }

  try {
    observer = new MutationObserver(schedule);
    observer.observe(document, { childList: true, subtree: true,
                                 attributes: true,
                                 attributeFilter: ['class', 'style', 'hidden'] });
  } catch (_) {}
  ['DOMContentLoaded', 'load', 'pageshow', 'readystatechange']
    .forEach(function (ev) {
      try { document.addEventListener(ev, schedule, true); } catch (_) {}
    });

  var sweeps = 0;
  timer = setInterval(function () {
    sweeps++;
    if (clicked || sweeps > 60) { stop(); return; }
    schedule();
  }, 1000);

  schedule();
})();)SHCONSENT";

}  // namespace

void AppBase::OnBeforeCommandLineProcessing(
    const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
  // Рендерер рождается с уже известными настройками защит: иначе переход на
  // другой сайт (новый процесс) включал бы автосогласие вопреки тумблеру и
  // выключал анти-отпечаток, который пользователь включил.
  if (process_type.empty() || !command_line) return;
  if (!command_line->HasSwitch(kProtectionSwitch)) return;
  const std::string value =
      command_line->GetSwitchValue(kProtectionSwitch).ToString();
  // Разбор общий с браузерной частью (src/protection_flags.h): чужой формат
  // оставляет значения по умолчанию.
  ParseProtectionSwitch(value, &g_fp_enabled, &g_auto_consent);
}

void AppBase::OnContextCreated(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefV8Context> context) {
  if (router_ && IsUiFrame(frame)) {
    router_->OnContextCreated(browser, frame, context);
    return;
  }
  // Оба скрипта защиты ставятся только обычным веб-страницам (http/https) —
  // никогда UI, never-скриптам и не внутренним схемам. Cookie-скрипт нужен и
  // в подфреймах: баннеры согласия часто живут в отдельном CMP-iframe.
  const std::string url = frame ? frame->GetURL().ToString() : std::string();
  const bool web = url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
  if (web && context) {
    CefRefPtr<CefV8Value> retval;
    CefRefPtr<CefV8Exception> exc;
    if (g_fp_enabled) {
      context->Eval(CefString(kFpScript), CefString("shelter://anti-fp"), 0,
                    retval, exc);
    }
    if (g_auto_consent) {
      context->Eval(CefString(kCookieScript),
                    CefString("shelter://cookie-consent"), 0, retval, exc);
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
  if (name == "shelter.cookies" && source_process == PID_BROWSER) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    g_auto_consent = args && args->GetSize() > 0 && args->GetBool(0);
    return true;
  }
  return router_ &&
         router_->OnProcessMessageReceived(browser, frame, source_process,
                                           message);
}

}  // namespace shelter
