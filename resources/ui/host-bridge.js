/* SHELTER — мост между вёрсткой (UI) и нативной оболочкой на CEF.
 *
 * Подключается автоматически при отдаче index.html (см. ui_scheme.cc) и создаёт
 * window.shelterNative + хуки window.shelter*, которые ожидает вёрстка.
 * Вкладки — отдельные нативные вид'ы (CefBrowserView) поверх области #viewport.
 * Пока поверх страницы открыто HTML-меню/модальное окно/тост, нативный вид
 * заменяется снимком страницы (иначе HTML оказался бы под нативным видом).
 */
(function () {
  'use strict';
  if (window.shelterNative) return;

  var PLATFORM = '__PLATFORM__';
  var IS_DEVELOPMENT_BUILD = __DEV_BUILD__;
  var DISTRIBUTION_CHANNEL = '__DISTRIBUTION_CHANNEL__';
  /* macOS вырезает «дыры» и радиусы прямо из CALayer. На Windows DirectComposition
     не уважает HWND-регион, поэтому прозрачные углы рисует click-through слой; для
     интерактивных «дыр» остаётся snapshot fallback. */
  var USE_HOLES = PLATFORM === 'darwin';
  var USE_WINDOWS_MASK = PLATFORM === 'win32';
  var USE_CLIP = USE_HOLES || USE_WINDOWS_MASK;
  var VERSION = '__APP_VERSION__';
  var root = document.documentElement;
  var DIAGNOSTICS_ENABLED = '__SHELTER_CI_HOOKS__' === 'true';
  var PERF_METHODS = Object.create(null);
  [
    'premium.status', 'premium.checkout', 'user-data.delete', 'win.minimize',
    'win.maximize', 'win.close', 'win.fullscreen', 'favicon.ensure', 'ext.list',
    'ext.setMode', 'ext.install', 'ext.remove', 'ext.pick', 'file.open',
    'win.openPlain', 'view.open', 'view.layout', 'view.clip', 'view.hide',
    'view.show', 'tabs.sync', 'view.act', 'ctx.act', 'view.snap', 'view.zoom',
    'find', 'devtools', 'clip.read', 'clip.write', 'theme.scheme', 'shell.show',
    'block.setEnabled', 'fp.setEnabled', 'cookie.setEnabled', 'https.setEnabled',
    'dns.setProvider', 'strict.setEnabled', 'strict.allow', 'ghost.setEnabled',
    'shell.filestate', 'dl.decision', 'dl.control', 'privacy.clear',
    'dbg.protections', 'dbg.hit', 'dbg.clip', 'window.minimize',
    'window.maximize', 'window.close', 'window.openPlain', 'theme.scheme',
    'privacy.clear', 'auth.open', 'devtools.ctl', 'ai.test', 'ai.chat',
    'clipboard.read', 'clipboard.write', 'tab.captureThumbs', 'tab.hideContent',
    'tab.navigate', 'tab.reload', 'tab.stop', 'tab.zoom', 'ui.ready',
    'viewport.sync', 'diag.performance', 'ui.longtask', 'freeze.snapshot',
    'freeze.decode', 'freeze.hide', 'freeze.total', 'thaw.show', 'thaw.total',
    'other'
  ].forEach(function (name) { PERF_METHODS[name] = true; });
  function safePerfMethod(name) {
    return typeof name === 'string' && PERF_METHODS[name] ? name : 'other';
  }
  function perfMethodFromOptions(options) {
    var request = {};
    try {
      if (options && typeof options.request === 'string') request = JSON.parse(options.request) || {};
    } catch (_) {}
    var name = typeof request.m === 'string' ? request.m : '';
    if (!name && typeof request.cmd === 'string') name = request.cmd.replace(/:/g, '.');
    return safePerfMethod(name);
  }
  function installCefQueryMeasurement(original) {
    if (!DIAGNOSTICS_ENABLED || typeof original !== 'function') return original;
    return function (options) {
      if (!options || typeof options !== 'object') return original.apply(this, arguments);
      var method = perfMethodFromOptions(options), started = performance.now(), settled = false;
      var wrapped = Object.assign({}, options);
      function finish() {
        if (settled) return;
        settled = true;
        recordPerf(method, Math.max(0, performance.now() - started));
      }
      if (typeof options.onSuccess === 'function') {
        var onSuccess = options.onSuccess;
        wrapped.onSuccess = function () { finish(); return onSuccess.apply(this, arguments); };
      }
      if (typeof options.onFailure === 'function') {
        var onFailure = options.onFailure;
        wrapped.onFailure = function () { finish(); return onFailure.apply(this, arguments); };
      }
      try { return original.call(this, wrapped); }
      catch (error) { finish(); throw error; }
    };
  }
  var nativeCefQuery = window.cefQuery;
  var bridgeCefQuery = nativeCefQuery;

  /* ---------- cefQuery ---------- */
  function q(method, args) {
    return new Promise(function (resolve) {
      if (typeof bridgeCefQuery !== 'function') { resolve(null); return; }
      bridgeCefQuery.call(window, {
        request: JSON.stringify({ m: method, a: args || {} }),
        persistent: false,
        onSuccess: function (r) {
          var v = {};
          try { v = r ? JSON.parse(r) : {}; } catch (_) {}
          resolve(v);
        },
        onFailure: function () { resolve(null); }
      });
    });
  }
  function qSnap(id, quality, ms) {
    return new Promise(function (resolve) {
      if (typeof bridgeCefQuery !== 'function') { resolve(''); return; }
      var done = false;
      var timer = setTimeout(function () { if (!done) { done = true; resolve(''); } }, ms || 900);
      bridgeCefQuery.call(window, {
        request: JSON.stringify({ m: 'view.snap', a: { id: id, q: quality || 72 } }),
        persistent: false,
        onSuccess: function (r) { if (!done) { done = true; clearTimeout(timer); resolve(r || ''); } },
        onFailure: function () { if (!done) { done = true; clearTimeout(timer); resolve(''); } }
      });
    });
  }

  /* ---------- шифрование секретов в browser process ---------- */
  // Только шифротекст и открытый текст пересекают границу процессов; мастер-
  // ключ и ChaCha20/HMAC остаются в нативном коде и не попадают в renderer JS.
  function secretCall(operation, value) {
    try {
      var xhr = new XMLHttpRequest();
      xhr.open('POST', '/secret/' + operation, false);
      xhr.setRequestHeader('Content-Type', 'text/plain;charset=UTF-8');
      xhr.send(String(value == null ? '' : value));
      var response = JSON.parse(xhr.responseText || '{}');
      if (xhr.status === 200 && response && response.ok === true &&
          typeof response.value === 'string') return response.value;
    } catch (_) {}
    window.__shelterSecretFailure = true;
    return '';
  }

  /* ---------- состояние ---------- */
  var st = {
    visible: false,     // нативный вид вкладки должен быть показан
    tabId: '',
    urls: {},           // id -> последний известный url
    loading: {},        // id -> bool
    zoom: 1,
    zoomPending: false,
    frozen: false,      // вместо нативного вида показан снимок
    busy: false,
    snapFor: '',
    snapUrl: '',
    lastRect: '',
    epoch: 0                 // invalidates in-flight freeze/thaw across tab/view changes
  };
  var cbs = { dlprompt: null, ctxclose: null };
  var lastPopupToast = 0;

  function setNativeContentClass() {
    var body = document.body;
    if (body) body.classList.toggle('native-content-visible', !!st.visible);
  }

  function byId(id) { return document.getElementById(id); }
  function vpRect() {
    var v = byId('viewport');
    if (!v) return null;
    var r = v.getBoundingClientRect();
    var o = { x: r.left, y: r.top, w: r.width, h: r.height };
    // Панель поиска (HTML) лежит поверх области вида — освобождаем ей место сверху.
    var fb = byId('findbar');
    if (st.visible && fb && !fb.hidden) {
      var fr = fb.getBoundingClientRect();
      var cut = fr.bottom + 6 - o.y;
      if (fr.height > 0 && cut > 0 && cut < o.h - 40) { o.y += cut; o.h -= cut; }
    }
    return o;
  }
  function rectKey(r) { return r ? [r.x, r.y, r.w, r.h].map(Math.round).join(',') : ''; }
  function editableFocused() {
    var a = document.activeElement;
    return !!a && (/^(INPUT|TEXTAREA|SELECT)$/.test(a.tagName) || a.isContentEditable);
  }
  function activeId() {
    try { return typeof window.getActiveTabId === 'function' ? (window.getActiveTabId() || '') : ''; }
    catch (_) { return ''; }
  }
  function tabInfo(id) {
    try { if (typeof window.__shelterTab === 'function') return window.__shelterTab(id); } catch (_) {}
    var space = 'main', ghost = false;
    try {
      space = localStorage.getItem('shelter:currentSpace') || 'main';
      ghost = localStorage.getItem('shelter:ghost') === '1';
    } catch (_) {}
    return { space: space, ghost: ghost };
  }

  /* ---------- стили ---------- */
  var style = document.createElement('style');
  style.textContent =
    '#shSnap{position:absolute;left:0;top:0;width:100%;height:100%;object-fit:fill;pointer-events:none;z-index:3;background:#fff}' +
    (PLATFORM === 'darwin' ? '#winLights{visibility:hidden!important}' : '');
  (document.head || root).appendChild(style);
  root.classList.add('host-' + PLATFORM);

  /* ---------- снимок вместо нативного вида ---------- */
  /* Модальные/интерактивные слои, которым нужен снимок вместо нативного вида: меню, модалки. */
  var FREEZE_BASE = '.menu:not(.closing),.scrim:not(.closing),.call:not([hidden]),#findbar:not([hidden])';
  /* Лёгкие слои (подсказки, уведомления, подсказки омнибокса, панель загрузок) показываются через
     «дыры» в нативном виде вкладки: страница остаётся живой, без снимков и подвисаний. */
  var HOLE_SEL = '.tip,#toasts>.toast,.suggest:not([hidden]),#noteFloat:not([hidden]),#dlFloat:not([hidden])';
  /* без «дыр» те же слои показываем через снимок (как раньше) */
  var OVERLAY_SEL = USE_HOLES ? FREEZE_BASE : FREEZE_BASE + ',' + HOLE_SEL;

  /* ---------- замеры ---------- */
  var perf = window.__shPerf = {
    enabled: DIAGNOSTICS_ENABLED,
    freezes: 0, thaws: 0, snapMs: [], snapKB: [], freezeMs: [], thawMs: [],
    holes: 0, clips: 0, clipFrames: 0, clipActive: false,
    layoutChecks: 0, layoutRequests: 0, overlayChecks: 0, observerWakeups: 0,
    longTasks: 0, longMax: 0, pending: Object.create(null)
  };
  function pushPerf(arr, v) { arr.push(Math.round(v)); if (arr.length > 20) arr.shift(); }
  var perfFlushTimer = 0, perfReportAt = 0;
  function recordPerf(method, duration) {
    if (!DIAGNOSTICS_ENABLED) return;
    var name = safePerfMethod(method), ms = Number(duration);
    if (!isFinite(ms) || ms < 0) return;
    ms = Math.min(ms, 1000000);
    var bucket = perf.pending[name] || (perf.pending[name] = { count: 0, totalMs: 0, maxMs: 0 });
    bucket.count++;
    bucket.totalMs = Math.min(1000000000, bucket.totalMs + ms);
    bucket.maxMs = Math.max(bucket.maxMs, ms);
    if (!perfFlushTimer) perfFlushTimer = setTimeout(flushPerf, 6000);
  }
  function flushPerf() {
    perfFlushTimer = 0;
    if (!DIAGNOSTICS_ENABLED || typeof nativeCefQuery !== 'function') return;
    var now = Date.now(), wait = 6000 - (now - perfReportAt);
    if (wait > 0) { perfFlushTimer = setTimeout(flushPerf, wait); return; }
    var names = Object.keys(perf.pending).slice(0, 64), samples = [];
    names.forEach(function (name) {
      var bucket = perf.pending[name];
      if (!bucket || !bucket.count) return;
      samples.push({ method: name, count: bucket.count, totalMs: bucket.totalMs, maxMs: bucket.maxMs });
      delete perf.pending[name];
    });
    if (!samples.length) return;
    perfReportAt = now;
    try {
      nativeCefQuery.call(window, {
        request: JSON.stringify({ m: 'diag.performance', a: { samples: samples } }),
        persistent: false,
        onSuccess: function () {},
        onFailure: function () {}
      });
    } catch (_) {}
    if (Object.keys(perf.pending).length && !perfFlushTimer) perfFlushTimer = setTimeout(flushPerf, 6000);
  }
  window.__shRecordPerf = recordPerf;
  if (DIAGNOSTICS_ENABLED && typeof nativeCefQuery === 'function') {
    bridgeCefQuery = installCefQueryMeasurement(nativeCefQuery);
    // Не перезаписываем read-only V8-свойство CEF; прокси живёт в отдельном ключе.
    window.__shCefQuery = bridgeCefQuery;
  }

  function overlayOverViewport() {
    perf.overlayChecks++;
    var vp = vpRect();
    if (!vp) return false;
    var list = document.querySelectorAll(OVERLAY_SEL);
    for (var i = 0; i < list.length; i++) {
      var r = list[i].getBoundingClientRect();
      if (r.width > 0 && r.height > 0 &&
          r.right > vp.x && r.left < vp.x + vp.w && r.bottom > vp.y && r.top < vp.y + vp.h) return true;
    }
    return false;
  }
  function showSnap(url) {
    var v = byId('viewport');
    if (!v) return null;
    var img = byId('shSnap');
    if (!url) { if (img) img.remove(); return null; }
    if (!img) { img = document.createElement('img'); img.id = 'shSnap'; img.alt = ''; v.appendChild(img); }
    img.style.visibility = 'hidden';
    img.src = url;
    return img;
  }
  function waitForSnapshot(img, timeout) {
    return new Promise(function (resolve) {
      if (!img) { resolve(false); return; }
      var settled = false;
      var timer = setTimeout(function () { finish(false); }, timeout || 500);
      function finish(ready) {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        resolve(!!ready && img.complete && img.naturalWidth > 0);
      }
      if (typeof img.decode === 'function') {
        try { img.decode().then(function () { finish(img.naturalWidth > 0); }, function () { finish(false); }); }
        catch (_) { finish(false); }
        return;
      }
      if (img.complete) { finish(img.naturalWidth > 0); return; }
      img.addEventListener('load', function () { finish(img.naturalWidth > 0); }, { once: true });
      img.addEventListener('error', function () { finish(false); }, { once: true });
    });
  }
  function dropSnap() { st.snapUrl = ''; st.snapFor = ''; showSnap(''); }
  function nextFrames(n) {
    return new Promise(function (res) {
      (function step(i) { if (i <= 0) res(); else requestAnimationFrame(function () { step(i - 1); }); })(n);
    });
  }

  function freeze() {
    if (st.busy || st.frozen || !st.visible || !st.tabId) return;
    st.busy = true;
    var id = st.tabId, epoch = st.epoch, t0 = performance.now(), hideSent = false, captureFailed = false, finished = false;
    function stillCurrent() {
      return st.visible && st.tabId === id && st.epoch === epoch && overlayOverViewport();
    }
    function finish() {
      if (finished) return;
      finished = true;
      recordPerf('freeze.total', performance.now() - t0);
      st.busy = false;
      evaluate();
    }
    function recoverStaleHide() {
      st.frozen = false;
      dropSnap();
      if (!st.visible) { finish(); return; }
      return q('view.show', { rect: vpRect(), focus: !editableFocused() })
        .then(function () { return nextFrames(1); })
        .then(function () { st.clipKey = ''; finish(); });
    }

    var snapStarted = performance.now();
    qSnap(id, 58, 900).then(function (url) {
      var snapElapsed = performance.now() - snapStarted;
      recordPerf('freeze.snapshot', snapElapsed);
      pushPerf(perf.snapMs, snapElapsed);
      pushPerf(perf.snapKB, (url || '').length / 1024);
      if (!stillCurrent()) { dropSnap(); return false; }
      if (!url) { captureFailed = true; dropSnap(); return false; }
      st.snapUrl = url; st.snapFor = id;
      var img = showSnap(url), decodeStarted = performance.now();
      return waitForSnapshot(img, 500).then(function (ready) {
        recordPerf('freeze.decode', performance.now() - decodeStarted);
        if (!ready) { captureFailed = true; dropSnap(); return false; }
        if (!stillCurrent()) { dropSnap(); return false; }
        img.style.visibility = '';
        return nextFrames(2).then(stillCurrent);
      });
    }).then(function (shouldHide) {
      if (!shouldHide || !stillCurrent()) {
        st.frozen = false;
        dropSnap();
        if (captureFailed && st.visible && overlayOverViewport()) scheduleFreezeRetry();
        finish();
        return false;
      }
      st.frozen = true;
      hideSent = true;
      var hideStarted = performance.now();
      return q('view.hide').then(function (result) {
        recordPerf('freeze.hide', performance.now() - hideStarted);
        return result !== null;
      });
    }).then(function (hidden) {
      if (!hideSent) return;
      if (!hidden) {
        st.frozen = false;
        dropSnap();
        if (st.visible && overlayOverViewport()) scheduleFreezeRetry();
        finish();
        return;
      }
      if (!st.visible || st.tabId !== id || st.epoch !== epoch) {
        recoverStaleHide();
        return;
      }
      perf.freezes++;
      pushPerf(perf.freezeMs, performance.now() - t0);
      finish();
    }).catch(function () {
      // Всегда возвращаем обычный вид интерфейса, если снимок или мост не сработали.
      if (hideSent) recoverStaleHide();
      else { st.frozen = false; dropSnap(); scheduleFreezeRetry(); finish(); }
    });
  }
  function thaw() {
    if (st.busy || !st.frozen || !st.visible) return;
    st.busy = true;
    var id = st.tabId, epoch = st.epoch, tt0 = performance.now(), finished = false;
    function finish() {
      if (finished) return;
      finished = true;
      recordPerf('thaw.total', performance.now() - tt0);
      st.busy = false;
      st.clipKey = '';
      evaluate();
    }
    function showNativeView() {
      var showStarted = performance.now();
      return q('view.show', { rect: vpRect(), focus: !editableFocused() }).then(function (result) {
        recordPerf('thaw.show', performance.now() - showStarted);
        return result;
      });
    }
    showNativeView()
      .then(function () { return nextFrames(2); })
      .then(function () {
        st.frozen = false;
        perf.thaws++;
        pushPerf(perf.thawMs, performance.now() - tt0);
        if (!st.visible) {
          dropSnap();
          finish();
          return;
        }
        var changed = st.epoch !== epoch || st.tabId !== id;
        var covered = overlayOverViewport();
        if (!covered) dropSnap();
        if (changed) {
          // view.open may have been queued while the old snapshot was still
          // frozen. Re-apply the current viewport after it, not the stale one.
          return showNativeView()
            .then(function () { return nextFrames(1); })
            .then(finish);
        }
        finish();
      })
      .catch(function () {
        st.frozen = false;
        dropSnap();
        finish();
      });
  }

  /* ---------- скругление углов и «дыры» ---------- */
  function px(v) { v = parseFloat(v); return isNaN(v) ? 0 : v; }
  function computeRadii(vp) {
    var v = byId('viewport'), sg = byId('stage');
    if (!v) return [0, 0, 0, 0];
    var cv = getComputedStyle(v);
    var own = [px(cv.borderTopLeftRadius), px(cv.borderTopRightRadius), px(cv.borderBottomRightRadius), px(cv.borderBottomLeftRadius)];
    var res = own.slice();
    if (sg) {
      var cs = getComputedStyle(sg), sr = sg.getBoundingClientRect();
      var l = sr.left + px(cs.borderLeftWidth), t = sr.top + px(cs.borderTopWidth);
      var r = sr.right - px(cs.borderRightWidth), b = sr.bottom - px(cs.borderBottomWidth);
      var sRad = [px(cs.borderTopLeftRadius), px(cs.borderTopRightRadius), px(cs.borderBottomRightRadius), px(cs.borderBottomLeftRadius)];
      var near = function (a, c) { return Math.abs(a - c) < 2.5; };
      var hits = [near(vp.x, l) && near(vp.y, t), near(vp.x + vp.w, r) && near(vp.y, t),
                  near(vp.x + vp.w, r) && near(vp.y + vp.h, b), near(vp.x, l) && near(vp.y + vp.h, b)];
      for (var i = 0; i < 4; i++) if (hits[i] && own[i] < 1) res[i] = Math.max(0, sRad[i] - 1);
    }
    return res.map(function (x) { return Math.round(x * 10) / 10; });
  }
  function holeRects(vp) {
    var out = [], list = document.querySelectorAll(HOLE_SEL), i, j;
    for (i = 0; i < list.length; i++) {
      var el = list[i];
      var r = el.getBoundingClientRect();
      if (r.width < 1 || r.height < 1) continue;
      // Радиус скругления самого попапа: дырка в нативном виде повторяет его,
      // иначе на фоне страницы попап выглядит квадратным.
      var cs = getComputedStyle(el), rr = 0, corners = ['borderTopLeftRadius', 'borderTopRightRadius', 'borderBottomRightRadius', 'borderBottomLeftRadius'];
      for (j = 0; j < 4; j++) rr = Math.max(rr, px(cs[corners[j]]));
      var x0 = Math.max(Math.floor(r.left) - 2, vp.x), y0 = Math.max(Math.floor(r.top) - 2, vp.y);
      var x1 = Math.min(Math.ceil(r.right) + 2, vp.x + vp.w), y1 = Math.min(Math.ceil(r.bottom) + 2, vp.y + vp.h);
      if (x1 <= x0 || y1 <= y0) continue;
      out.push([x0, y0, x1, y1, Math.round(rr)]);
    }
    /* объединяем пересекающиеся прямоугольники (иначе even-odd вырежет «пересечение» обратно) */
    var merged = true;
    while (merged) {
      merged = false;
      for (i = 0; i < out.length && !merged; i++) for (j = i + 1; j < out.length && !merged; j++) {
        var a = out[i], b = out[j];
        if (a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3]) {
          out[i] = [Math.min(a[0], b[0]), Math.min(a[1], b[1]), Math.max(a[2], b[2]), Math.max(a[3], b[3]), Math.max(a[4] || 0, b[4] || 0)];
          out.splice(j, 1); merged = true;
        }
      }
    }
    return out.map(function (a) { return [Math.round(a[0] - vp.x), Math.round(a[1] - vp.y), Math.round(a[2] - a[0]), Math.round(a[3] - a[1]), a[4] || 0]; });
  }
  var clipColorCanvas = document.createElement('canvas');
  clipColorCanvas.width = clipColorCanvas.height = 1;
  var clipColorContext = clipColorCanvas.getContext('2d', { willReadFrequently: true });
  function readCssColor(value) {
    if (!clipColorContext || !value) return [14, 15, 19, 255];
    clipColorContext.clearRect(0, 0, 1, 1);
    clipColorContext.fillStyle = 'rgba(0,0,0,0)';
    try { clipColorContext.fillStyle = value; } catch (_) {}
    clipColorContext.fillRect(0, 0, 1, 1);
    var px = clipColorContext.getImageData(0, 0, 1, 1).data;
    return [px[0], px[1], px[2], px[3]];
  }
  function clipMaskColor() {
    if (!USE_WINDOWS_MASK) return 0;
    var cs = getComputedStyle(byId('stage') || root);
    var surface = readCssColor(cs.getPropertyValue('--surface').trim());
    var glass = readCssColor(cs.getPropertyValue('--glass-surface').trim());
    if (!glass[3]) {
      var opacity = parseFloat(cs.getPropertyValue('--glass-op'));
      if (!isFinite(opacity)) opacity = 82;
      opacity = Math.max(0, Math.min(100, opacity));
      glass = [surface[0], surface[1], surface[2], Math.round(opacity * 2.55)];
    }
    var base = readCssColor(cs.getPropertyValue('--bg').trim());
    var alpha = glass[3] / 255;
    var r = Math.round(glass[0] * alpha + base[0] * (1 - alpha));
    var g = Math.round(glass[1] * alpha + base[1] * (1 - alpha));
    var b = Math.round(glass[2] * alpha + base[2] * (1 - alpha));
    return (r << 16) | (g << 8) | b;
  }
  function syncClip() {
    if (!USE_CLIP || !st.visible || st.frozen || st.busy) return 0;
    var vp = vpRect();
    if (!vp || vp.w < 2) return 0;
    var rad = computeRadii(vp), holes = USE_HOLES ? holeRects(vp) : [];
    var maskColor = clipMaskColor();
    var key = rad.join(',') + '|' + holes.join(';') + '|' + maskColor;
    if (key !== st.clipKey) {
      st.clipKey = key; perf.clips++; perf.holes = holes.length;
      q('view.clip', { radii: rad, holes: holes, maskColor: maskColor });
    }
    return holes.length;
  }
  var clipLoop = false, clipCalm = 0, clipUntil = 0;
  function stopClipLoop() {
    clipLoop = false;
    clipCalm = 0;
    clipUntil = 0;
    perf.clipActive = false;
  }
  function clipTick() {
    if (!USE_HOLES || !st.visible || st.frozen || st.busy) { stopClipLoop(); return; }
    perf.clipFrames++;
    syncClip();
    if (clipCalm > 0) clipCalm--;
    if (clipCalm > 0 || performance.now() < clipUntil) requestAnimationFrame(clipTick);
    else stopClipLoop();
  }
  function kickClip(settleMs) {
    if (!USE_CLIP || !st.visible || st.frozen || st.busy) return;
    if (USE_WINDOWS_MASK) { syncClip(); return; }
    clipCalm = Math.max(clipCalm, 3);
    clipUntil = Math.max(clipUntil, performance.now() + Math.max(0, +settleMs || 0));
    if (!clipLoop) {
      clipLoop = true;
      perf.clipActive = true;
      requestAnimationFrame(clipTick);
    }
  }
  window.__shClip = function () { return st.clipKey || ''; };
  window.__shBridgeState = function () {
    return {
      visible: st.visible, frozen: st.frozen, busy: st.busy,
      nativeContentVisible: !!(document.body && document.body.classList.contains('native-content-visible')),
      clipActive: clipLoop, trackedPopups: popupObservers.size, snapFor: st.snapFor
    };
  };

  var freezeRetryAt = 0, freezeRetryTimer = 0;
  function resetFreezeRetry() {
    freezeRetryAt = 0;
    if (freezeRetryTimer) { clearTimeout(freezeRetryTimer); freezeRetryTimer = 0; }
  }
  function scheduleFreezeRetry() {
    if (!st.visible) { resetFreezeRetry(); return; }
    resetFreezeRetry();
    freezeRetryAt = performance.now() + 750;
    freezeRetryTimer = setTimeout(function () { freezeRetryTimer = 0; scheduleEval(); }, 750);
  }
  function evaluate() {
    if (st.busy) return;
    var need = st.visible && overlayOverViewport();
    if (!need) resetFreezeRetry();
    if (need && !st.frozen && performance.now() >= freezeRetryAt) freeze();
    else if (!need && st.frozen && st.visible) thaw();
    else if (!st.visible && st.frozen) { st.frozen = false; dropSnap(); }
    /* Holes are measured only for their short entrance/exit motion or viewport
       geometry changes; a stationary toast/tooltip no longer owns a permanent rAF. */
    if (st.visible && !st.frozen && !st.busy) kickClip();
  }
  var evalQueued = false;
  function scheduleEval() {
    if (!st.visible && !st.frozen && !st.busy) return;
    if (evalQueued) return;
    evalQueued = true;
    requestAnimationFrame(function () { evalQueued = false; evaluate(); });
  }

  /* ---------- раскладка ---------- */
  var layoutQueued = false;
  function relayout() {
    perf.layoutChecks++;
    if (!st.visible || st.frozen || st.busy) return;
    var r = vpRect();
    var k = rectKey(r);
    if (!r || k === st.lastRect || r.w < 2) return;
    st.lastRect = k;
    if (USE_HOLES) {
      st.clipKey = '';
      kickClip();
    }
    perf.layoutRequests++;
    var layout = q('view.layout', { rect: r, visible: true });
    if (USE_WINDOWS_MASK) layout.then(syncClip);
  }
  function scheduleRelayout() {
    if (layoutQueued) return;
    layoutQueued = true;
    requestAnimationFrame(function () { layoutQueued = false; relayout(); });
  }

  /* ---------- хуки, которых ждёт вёрстка ---------- */
  window.shelterOpen = function (url, opts) {
    var id = activeId();
    var rect = vpRect();
    if (!id || !rect || rect.w < 2 || rect.h < 2) return false;
    var info = tabInfo(id);
    var partition = (info.ghost ? 'temp:ghost-' : 'persist:space-') + info.space;
    var changed = st.urls[id] !== url;
    var contextChanged = st.tabId !== id || changed || !st.visible;
    st.urls[id] = url;
    if (contextChanged) { st.epoch++; dropSnap(); resetFreezeRetry(); }
    st.tabId = id;
    st.visible = true;
    setNativeContentClass();
    var focus = true;
    if (editableFocused()) {
      if (changed) { try { document.activeElement.blur(); } catch (_) {} }
      else focus = false;
    }
    st.lastRect = rectKey(rect);
    var opened = q('view.open', {
      id: id, url: url, partition: partition, rect: rect,
      visible: !st.frozen, focus: focus && !st.frozen, zoom: st.zoom
    });
    if (st.zoomPending) {
      st.zoomPending = false;
      opened.then(function (result) {
        if (result !== null && st.visible && st.tabId === id) q('view.zoom', { factor: st.zoom });
      });
    }
    st.clipKey = '';
    scheduleEval();
    return true;
  };
  window.shelterShowChrome = function () {
    if (!st.visible) {
      st.epoch++;
      st.frozen = false;
      resetFreezeRetry();
      setNativeContentClass();
      stopClipLoop();
      dropSnap();
      return;
    }
    st.epoch++;
    q('view.hide');
    st.visible = false;
    st.frozen = false;
    resetFreezeRetry();
    setNativeContentClass();
    stopClipLoop();
    dropSnap();
  };
  window.shelterIsLoading = function () { return !!st.loading[st.tabId] && st.visible; };
  window.shelterStopLoad = function () {
    if (!st.visible || !st.loading[st.tabId]) return false;
    st.loading[st.tabId] = false;
    q('view.act', { id: st.tabId, act: 'stop' });
    if (typeof window.onNativeLoading === 'function') window.onNativeLoading(false);
    return true;
  };
  window.shelterSetZoomAll = function (f) {
    f = +f;
    if (!(f > 0)) return;
    st.zoom = f;
    if (!st.visible) { st.zoomPending = true; return; }
    st.zoomPending = false;
    q('view.zoom', { factor: f });
  };
  window.shelterGhostSync = function () { scheduleSync(); };
  window.shelterAfterFire = function () { scheduleSync(); };
  window.shelterGetThumb = function () { return ''; };
  window.shelterCaptureVisibleTabs = function () {
    var id = st.tabId;
    if (!id || !st.visible) return;
    var put = function (url) {
      if (url && typeof window.shelterOnThumb === 'function') window.shelterOnThumb(id, url);
    };
    if (st.frozen || st.busy) { if (st.snapFor === id) put(st.snapUrl); return; }
    qSnap(id, 40, 900).then(put);
  };

  /* ---------- синхронизация набора вкладок ---------- */
  var syncTimer = 0;
  function syncTabs() {
    var ids = [];
    try { if (typeof window.getAllTabIds === 'function') ids = window.getAllTabIds() || []; } catch (_) {}
    // Пустой список нужен, чтобы закрытие последней вкладки удаляло устаревшие нативные представления.
    q('tabs.sync', { ids: ids });
  }
  function scheduleSync() { clearTimeout(syncTimer); syncTimer = setTimeout(syncTabs, 350); }

  /* ---------- window.shelterNative ---------- */
  window.shelterNative = {
    isNative: true,
    platform: PLATFORM,
    distributionChannel: DISTRIBUTION_CHANNEL,
    isDevelopment: IS_DEVELOPMENT_BUILD,
    version: VERSION,
    isContentVisible: function () { return !!st.visible && !st.frozen && !st.busy; },
    getPremiumEntitlement: function () { return q('premium.status'); },
    startPremiumCheckout: function (planId) { return q('premium.checkout', { planId: planId || '' }); },
    minimize: function () { return q('win.minimize'); },
    maximize: function () { return q('win.maximize'); },
    close: function () { return q('win.close'); },
    clearPrivacy: function (parts, opts) {
      return q('privacy.clear', { parts: parts || null, opts: opts || null });
    },
    deleteUserData: function () {
      return q('user-data.delete');
    },
    clipRead: function () { return q('clip.read').then(function (r) { return (r && r.text) || ''; }); },
    clipWrite: function (t) { return q('clip.write', { text: String(t == null ? '' : t) }).then(function (r) { return !!(r && r.ok); }); },
    themeScheme: function (m) { q('theme.scheme', { mode: m }); },
    onZoom: function () {},
    onCtxClose: function (cb) { cbs.ctxclose = cb; },
    ctxAct: function (o) {
      if (!st.visible || !o || o.id !== st.tabId) return Promise.resolve(null);
      return q('ctx.act', o);
    },
    openPlainWindow: function (o) { return q('win.openPlain', { url: (o && o.url) || '' }); },
    showInFolder: function (p) { return q('shell.show', { path: String(p || '') }); },
    fileState: function (p) { return q('shell.filestate', { path: String(p || '') }).then(function (r) { return !!(r && r.exists); }).catch(function () { return null; }); },
    blockSetEnabled: function (on) { return q('block.setEnabled', { on: !!on }).catch(function () {}); },
    fpSetEnabled: function (on) { return q('fp.setEnabled', { on: !!on }).catch(function () {}); },
    cookieSetEnabled: function (on) { return q('cookie.setEnabled', { on: !!on }).catch(function () {}); },
    httpsSetEnabled: function (on) { return q('https.setEnabled', { on: !!on }).catch(function () {}); },
    dnsSetProvider: function (p) { return q('dns.setProvider', { provider: String(p || 'cf') }).catch(function () {}); },
    strictSetEnabled: function (on) { return q('strict.setEnabled', { on: !!on }).catch(function () {}); },
    strictAllow: function (host) { return q('strict.allow', { host: String(host || '') }).catch(function () {}); },
    ghostSetEnabled: function (on) { return q('ghost.setEnabled', { on: !!on }).catch(function () {}); },
    onDownloadPrompt: function (cb) { cbs.dlprompt = cb; },
    downloadDecision: function (o) { return q('dl.decision', o || {}); },
    downloadControl: function (o) { return q('dl.control', o || {}); },
    permAllow: function (o) { return q('perm.allow', o || {}); },
    certDecision: function (o) { return q('cert.decision', o || {}); },
    checkUpdates: function (o) { return q('app.checkUpdates', o || {}); },
    secretEnc: function (t) { return secretCall('encrypt', t); },
    secretDec: function (t) { return secretCall('decrypt', t); },
    setFullscreen: function (on) { return q('win.fullscreen', { on: !!on }); },
    extList: function () { return q('ext.list'); },
    extInstall: function (src) { return q('ext.install', { src: String(src || '') }); },
    extRemove: function (id) { return q('ext.remove', { id: String(id || '') }); },
    extPick: function () { return q('ext.pick'); },
    // Режим расширений: «движок Chromium» (MV3 через --load-extension) или
    // «оболочка» (ручное внедрение content-scripts). Действует со следующего
    // запуска — аргумент командной строки фиксируется до старта CEF.
    extSetMode: function (engine) { return q('ext.setMode', { engine: !!engine }); },
    fileOpen: function () { return q('file.open'); },  // Ctrl+O: диалог ОС -> file-open
    devTools: function (msg) { return q('devtools', msg || {}); },  // окно DevTools (F12 / ⇧⌘I)
    // Внешний ИИ-агент: проверка подключения и чат через нативный HTTPS
    // (ai.test/ai.chat). Без этих методов диалог отдавал «Нет доступа».
    testAgent: function (cfg) { return q('ai.test', cfg || {}); },
    agentChat: function (cfg, prompt) {
      return q('ai.chat', { cfg: cfg || {}, prompt: String(prompt == null ? '' : prompt) });
    },
    // authWindow намеренно не определён: вёрстка использует запасной путь.
    // Док-панель DevTools (act=open/close) в CEF не реализована.
  };

  /* ---------- события host -> UI ---------- */
  function known(id) {
    try { return (window.getAllTabIds ? window.getAllTabIds() : []).indexOf(id) >= 0; } catch (_) { return false; }
  }
  var H = {
    nav: function (p) {
      st.urls[p.id] = p.url;
      if (!known(p.id)) return;
      if (typeof window.pushTabHist === 'function') window.pushTabHist(p.url, p.id);
      if (p.id === st.tabId && typeof window.shelterRecordVisit === 'function') window.shelterRecordVisit(p.url);
    },
    title: function (p) {
      if (known(p.id) && typeof window.shelterUpdateTitle === 'function') window.shelterUpdateTitle(p.id, p.title);
    },
    loading: function (p) {
      st.loading[p.id] = !!p.loading;
      if (p.id === st.tabId && typeof window.onNativeLoading === 'function') window.onNativeLoading(!!p.loading);
    },
    newtab: function (p) {
      if (!p || !p.url || typeof window.newTab !== 'function') return;
      if (p.gesture === false) {
        // Всплывающее окно без жеста пользователя (window.open из скрипта):
        // вкладку не открываем, иначе любая страница устраивает tab-bomb.
        // Тост троттлирован; кнопка даёт разово открыть заблокированный адрес.
        var now = Date.now();
        if (now - lastPopupToast > 8000) {
          lastPopupToast = now;
          if (typeof window.toast === 'function') {
            (function (url, host) {
              var text = host ? ('Сайт ' + host + ' пытался открыть всплывающее окно — заблокировано')
                              : 'Всплывающее окно заблокировано';
              window.toast(text, {
                icon: 'shield-check', ms: 7000,
                action: { label: 'Открыть всё равно', run: function () { window.newTab(url); } }
              });
            })(p.url, p.host || '');
          }
        }
        return;
      }
      window.newTab(p.url);
    },
    key: function (p) {
      var ev = new KeyboardEvent('keydown', {
        code: p.code, key: p.key || '', ctrlKey: !!p.ctrl, metaKey: !!p.meta,
        shiftKey: !!p.shift, altKey: !!p.alt, bubbles: true, cancelable: true
      });
      (document.activeElement || document.body).dispatchEvent(ev);
    },
    ctx: function (p) {
      if (!st.visible || !p || p.id !== st.tabId || typeof window.shelterCtxMenu !== 'function') return;
      var vp = vpRect() || { x: 0, y: 0 };
      var id = p.id;
      var wv = {
        reload: function () { if (st.visible && st.tabId === id) q('view.act', { id: id, act: 'reload' }); },
        print: function () { if (st.visible && st.tabId === id) q('view.act', { id: id, act: 'print' }); }
      };
      window.shelterCtxMenu({
        params: p.params || {}, wv: wv, gid: id, partition: p.partition,
        point: { x: vp.x + (p.x || 0), y: vp.y + (p.y || 0) }
      });
    },
    found: function (p) {
      if (p.id !== st.tabId || !st.visible) return;
      var c = byId('findCnt'), i = byId('findInput');
      if (!c || !i || !i.value.trim()) return;
      c.textContent = p.count > 0 ? (Math.max(1, p.idx) + ' / ' + p.count) : '0 / 0';
    },
    dlprompt: function (p) { if (cbs.dlprompt) cbs.dlprompt(p); },
    download: function (p) { if (typeof window.shelterDownload === 'function') window.shelterDownload(p); }
  };
  window.__shelterHost = {
    ev: function (name, payload) {
      var f = H[name];
      if (!f) {
        // События без локальной обработки (fullscreen, ext, …) уходят в UI.
        if (typeof window.shelterCefDispatch === 'function') {
          try { window.shelterCefDispatch(name, payload || {}); } catch (_) {}
        }
        return;
      }
      try { f(payload || {}); } catch (e) { try { console.error('[shelter]', name, e); } catch (_) {} }
    }
  };

  /* ---------- перехват «Обновить» / F5 для нативных страниц ---------- */
  document.addEventListener('click', function (e) {
    if (!st.visible) return;
    var b = e.target && e.target.closest ? e.target.closest('[data-act="reload"]') : null;
    if (!b) return;
    e.preventDefault();
    e.stopImmediatePropagation();
    q('view.act', { id: st.tabId, act: 'reload' });
  }, true);
  document.addEventListener('keydown', function (e) {
    if (!st.visible) return;
    var mod = e.metaKey || e.ctrlKey;
    if (e.code === 'F5' || (mod && !e.shiftKey && !e.altKey && e.code === 'KeyR')) {
      e.preventDefault();
      e.stopImmediatePropagation();
      q('view.act', { id: st.tabId, act: e.shiftKey ? 'reloadHard' : 'reload' });
    }
  }, true);

    /* ---------- поиск по странице нативной вкладки ---------- */
  var findTimer = 0;
  function nativeFind(next, forward) {
    var i = byId('findInput');
    if (!i) return;
    q('find', { id: st.tabId, q: i.value, next: !!next, forward: forward !== false });
    var c = byId('findCnt');
    if (c && !i.value.trim()) c.textContent = '';
  }
  document.addEventListener('input', function (e) {
    if (!st.visible || !e.target || e.target.id !== 'findInput') return;
    e.stopImmediatePropagation();
    clearTimeout(findTimer);
    findTimer = setTimeout(function () { nativeFind(false, true); }, 120);
  }, true);
  document.addEventListener('keydown', function (e) {
    if (!st.visible || !e.target || e.target.id !== 'findInput' || e.key !== 'Enter') return;
    e.preventDefault();
    e.stopImmediatePropagation();
    nativeFind(true, !e.shiftKey);
  }, true);
  document.addEventListener('click', function (e) {
    if (!st.visible || !e.target || !e.target.closest) return;
    var b = e.target.closest('[data-act="findPrev"],[data-act="findNext"]');
    if (!b) return;
    e.preventDefault();
    e.stopImmediatePropagation();
    nativeFind(true, b.getAttribute('data-act') === 'findNext');
  }, true);
  var lastFindHidden = true;
  function watchFindbar() {
    var fb = byId('findbar');
    if (!fb) return;
    lastFindHidden = fb.hidden;
    new MutationObserver(function () {
      if (fb.hidden === lastFindHidden) return;
      lastFindHidden = fb.hidden;
      st.lastRect = '';
      if (fb.hidden) q('find', { id: st.tabId, act: 'stop' });
      else if (st.visible) { var i = byId('findInput'); if (i && i.value.trim()) nativeFind(false, true); }
      scheduleEval();
      scheduleRelayout();
    }).observe(fb, { attributes: true, attributeFilter: ['hidden'] });
  }

  /* ---------- наблюдатели ---------- */
  var POPUP_ROOTS = '.menu,.scrim,.tip,.call';
  var HOLE_ROOTS = '.tip,.toast,.suggest,#dlFloat';
  var popupObservers = new Map();
  function isElement(node) { return !!node && node.nodeType === 1 && typeof node.matches === 'function'; }
  function hasHole(node) {
    return isElement(node) && (node.matches(HOLE_ROOTS) ||
      (node.querySelector && !!node.querySelector(HOLE_ROOTS)));
  }
  function watchPopupNode(node) {
    if (!isElement(node) || !node.matches(POPUP_ROOTS) || popupObservers.has(node)) return false;
    var observer = new MutationObserver(function () {
      perf.observerWakeups++;
      if (!st.visible) return;
      scheduleEval();
      if (USE_HOLES && hasHole(node)) kickClip(420);
    });
    observer.observe(node, { attributes: true, attributeFilter: ['class', 'hidden'] });
    popupObservers.set(node, observer);
    return true;
  }
  function unwatchPopupNode(node) {
    var observer = popupObservers.get(node);
    if (!observer) return;
    observer.disconnect();
    popupObservers.delete(node);
  }
  function watchBodyPopups(records) {
    var changed = false, holeChanged = false;
    records.forEach(function (record) {
      if (record.type !== 'childList') return;
      Array.prototype.forEach.call(record.addedNodes, function (node) {
        if (watchPopupNode(node)) { changed = true; if (hasHole(node)) holeChanged = true; }
      });
      Array.prototype.forEach.call(record.removedNodes, function (node) {
        if (isElement(node) && node.matches(POPUP_ROOTS)) {
          if (hasHole(node)) holeChanged = true;
          unwatchPopupNode(node);
          changed = true;
        }
      });
    });
    if (!changed) return;
    perf.observerWakeups++;
    if (!st.visible) return;
    scheduleEval();
    if (USE_HOLES && holeChanged) kickClip(420);
  }
  function watchOverlayRoot(node, subtree) {
    if (!node) return;
    new MutationObserver(function (records) {
      if (!records.length) return;
      perf.observerWakeups++;
      if (!st.visible) return;
      scheduleEval();
      if (USE_HOLES) kickClip(420);
    }).observe(node, {
      childList: true, subtree: !!subtree, attributes: true,
      attributeFilter: ['class', 'hidden']
    });
  }
  function viewportMotion(event) {
    var vp = byId('viewport'), stage = byId('stage'), app = byId('app'), main = vp && vp.closest('.main');
    var target = event.target;
    if (target !== vp && target !== stage && target !== app && target !== main) return;
    scheduleRelayout();
    if (target === vp) {
      if (event.type === 'transitionrun') kickClip(560);
      else kickClip(60);
    }
  }
  function startObservers() {
    watchFindbar();
    var vp = byId('viewport'), stage = byId('stage'), app = byId('app'), main = vp && vp.closest('.main');
    if (window.ResizeObserver && vp) new ResizeObserver(scheduleRelayout).observe(vp);
    window.addEventListener('resize', scheduleRelayout, { passive: true });
    [vp, stage, app, main].forEach(function (node) {
      if (!node) return;
      node.addEventListener('transitionrun', viewportMotion);
      node.addEventListener('transitionend', viewportMotion);
      node.addEventListener('animationend', viewportMotion);
    });
    if (document.body) new MutationObserver(watchBodyPopups).observe(document.body, { childList: true });
    watchOverlayRoot(byId('toasts'), true);
    watchOverlayRoot(byId('heroSuggest'), false);
    watchOverlayRoot(byId('tbSuggest'), false);
    var tl = byId('tabList');
    if (tl) new MutationObserver(scheduleSync).observe(tl, { childList: true, subtree: true });
    if (USE_WINDOWS_MASK) {
      var refreshMask = function () {
        if (!st.visible || st.frozen || st.busy) return;
        st.clipKey = '';
        syncClip();
      };
      new MutationObserver(refreshMask).observe(root, {
        attributes: true, attributeFilter: ['data-theme', 'style', 'class']
      });
      if (document.body) new MutationObserver(refreshMask).observe(document.body, {
        attributes: true, attributeFilter: ['data-gfx']
      });
    }
    scheduleSync();
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', startObservers);
  else startObservers();
})();
