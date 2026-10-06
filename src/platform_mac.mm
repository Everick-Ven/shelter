#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <Security/Security.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>

#include "src/platform.h"
#include "src/security_paths.h"

namespace shelter {
namespace platform {
namespace {

// Куки Chromium в небрендированной (CEF) сборке: «Chromium Safe Storage».
// См. components/os_crypt/keychain_password_mac.mm (chromium source).
constexpr const char kCookieSvc[] = "Chromium Safe Storage";
constexpr const char kCookieAcct[] = "Chromium";

// Диагностика в keychain.log (публикуется CI): любые аномалии ключейни должны
// быть видны без доступа к машине. Пишем в общий каталог данных приложения.
void LogKC(const char* fmt, ...) {
  std::string dir = UserDataDir();
  if (dir.empty()) return;
  const std::string path = dir + "/keychain.log";
  FILE* f = fopen(path.c_str(), "a");
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}

// Разблокирована ли login keychain — запрос СТАТУСА, без UI и без обращений к
// записям: единственный абсолютно безопасный способ не провисеть на диалоге
// разблокировки в headless-окружении (CI).
bool LoginKeychainUnlocked(bool log_status = true) {
  SecKeychainRef kc = nullptr;
  if (SecKeychainCopyDefault(&kc) != errSecSuccess || !kc) {
    if (log_status) LogKC("status: no default keychain");
    return false;
  }
  SecKeychainStatus st = 0;
  OSStatus s = SecKeychainGetStatus(kc, &st);
  CFRelease(kc);
  const bool unlocked = (s == errSecSuccess) && (st & kSecUnlockStateStatus);
  if (log_status)
    LogKC("status: os=%d st=%u unlocked=%d", (int)s, (unsigned)st, unlocked ? 1 : 0);
  return unlocked;
}

CFStringRef MakeCF(const char* s) {
  return CFStringCreateWithCString(kCFAllocatorDefault, s, kCFStringEncodingUTF8);
}

// Чтение generic-пароли БЕЗ возможности показать запрос (kSecUseAuthenticationUIFail).
// errSecSuccess — данные получены; errSecItemNotFound — записи нет; прочее —
// есть, но недоступна молча (чужой ACL / заблокирована).
// Если login keychain заблокирована — вообще не трогаем SecItem (диалог
// разблокировки в CI = вечное зависание старта; статус-проверка его исключает).
OSStatus KeychainReadNoPrompt(const char* service, const char* account,
                              std::string* out) {
  if (!LoginKeychainUnlocked()) return errSecInteractionNotAllowed;
  CFStringRef svc = MakeCF(service);
  CFStringRef acct = MakeCF(account);
  const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount,
                        kSecReturnData, kSecMatchLimit, kSecUseAuthenticationUI};
  const void* vals[] = {kSecClassGenericPassword, svc, acct, kCFBooleanTrue,
                        kSecMatchLimitOne, kSecUseAuthenticationUIFail};
  CFDictionaryRef query = CFDictionaryCreate(
      kCFAllocatorDefault, keys, vals, 6, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  CFTypeRef res = nullptr;
  OSStatus st = SecItemCopyMatching(query, &res);
  if (query) CFRelease(query);
  if (svc) CFRelease(svc);
  if (acct) CFRelease(acct);
  LogKC("read %s/%s -> os=%d", service, account, (int)st);
  if (st != errSecSuccess || !res) {
    if (res) CFRelease(res);
    return st;
  }
  OSStatus result = errSecParam;
  if (CFGetTypeID(res) == CFDataGetTypeID()) {
    CFDataRef d = (CFDataRef)res;
    out->assign(reinterpret_cast<const char*>(CFDataGetBytePtr(d)),
                static_cast<size_t>(CFDataGetLength(d)));
    result = errSecSuccess;
  }
  CFRelease(res);
  return result;
}

// Удаляет ровно app-specific master key. Shared запись Chromium Safe Storage
// намеренно не трогаем: она может использоваться другими Chromium-браузерами.
bool KeychainDeleteNoPrompt(const char* service, const char* account) {
  if (!LoginKeychainUnlocked(false)) return false;
  CFStringRef svc = MakeCF(service);
  CFStringRef acct = MakeCF(account);
  if (!svc || !acct) {
    if (svc) CFRelease(svc);
    if (acct) CFRelease(acct);
    return false;
  }
  const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount,
                        kSecUseAuthenticationUI};
  const void* vals[] = {kSecClassGenericPassword, svc, acct,
                        kSecUseAuthenticationUIFail};
  CFDictionaryRef query = CFDictionaryCreate(
      kCFAllocatorDefault, keys, vals, 4, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  OSStatus status = query ? SecItemDelete(query) : errSecAllocate;
  if (query) CFRelease(query);
  CFRelease(svc);
  CFRelease(acct);
  return status == errSecSuccess || status == errSecItemNotFound;
}

}  // namespace

bool KeychainGet(const char* service, const char* account, std::string* out) {
  if (!out) return false;
  return KeychainReadNoPrompt(service, account, out) == errSecSuccess;
}

bool KeychainPutOpen(const char* service, const char* account,
                     const std::string& value) {
  // ACL «любое приложение, без запросов» — та же модель, что DPAPI на Windows:
  // процессы текущего пользователя читают молча, на диске запись защищена
  // ключом логина. Нужна ещё и потому, что ad-hoc подпись меняет cdhash каждую
  // сборку — иначе после обновления пришлось бы отвечать на запрос доступа.
  CFStringRef svc = MakeCF(service);
  CFStringRef acct = MakeCF(account);
  CFStringRef desc = MakeCF(service);
  bool ok = false;

  CFArrayRef emptyList =
      CFArrayCreate(kCFAllocatorDefault, nullptr, 0, &kCFTypeArrayCallBacks);
  SecAccessRef access = nullptr;
  OSStatus st = SecAccessCreate(CFSTR("SHELTER"), emptyList, &access);
  if (emptyList) CFRelease(emptyList);
  if (st == errSecSuccess && access) {
    CFArrayRef aclList =
        SecAccessCopyMatchingACLList(access, kSecACLAuthorizationDecrypt);
    if (aclList && CFArrayGetCount(aclList) > 0) {
      SecACLRef oldAcl = (SecACLRef)CFArrayGetValueAtIndex(aclList, 0);
      CFArrayRef auths = SecACLCopyAuthorizations(oldAcl);
      SecACLRemove(oldAcl);
      SecACLRef newAcl = nullptr;
      // trustedApplications = nullptr → «любое приложение»; prompt = 0 → без
      // запросов (см. SecACLCreateWithSimpleContents).
      if (SecACLCreateWithSimpleContents(access, nullptr, desc, 0, &newAcl) ==
              errSecSuccess &&
          newAcl) {
        if (auths) SecACLUpdateAuthorizations(newAcl, auths);
        CFRelease(newAcl);
        ok = true;
      }
      if (auths) CFRelease(auths);
    }
    if (aclList) CFRelease(aclList);
  }

  if (ok) {
    const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount,
                          kSecValueData, kSecAttrAccess};
    CFDataRef data = CFDataCreate(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(value.data()),
        static_cast<CFIndex>(value.size()));
    const void* vals[] = {kSecClassGenericPassword, svc, acct, data, access};
    CFDictionaryRef add = CFDictionaryCreate(
        kCFAllocatorDefault, keys, vals, 5, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    OSStatus addSt = SecItemAdd(add, nullptr);
    // duplicate = уже записано (гонка/повтор) — цель достигнута.
    LogKC("add %s/%s -> os=%d", service, account, (int)addSt);
    ok = (addSt == errSecSuccess || addSt == errSecDuplicateItem);
    if (add) CFRelease(add);
    if (data) CFRelease(data);
  }
  if (access) CFRelease(access);
  if (svc) CFRelease(svc);
  if (acct) CFRelease(acct);
  if (desc) CFRelease(desc);
  return ok;
}

bool RunTimed(const std::function<bool()>& op, int timeout_ms) {
  auto state = std::make_shared<std::atomic<int>>(-1);  // -1 pending, 0/1
  std::thread([state, op] { state->store(op() ? 1 : 0); }).detach();
  for (int waited = 0; waited < timeout_ms && state->load() < 0; waited += 50)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  return state->load() == 1;
}

namespace {
bool RemoveOwnedUserDataDirectory() {
  namespace fs = std::filesystem;
  NSArray* dirs = NSSearchPathForDirectoriesInDomains(
      NSApplicationSupportDirectory, NSUserDomainMask, YES);
  if (!dirs.count) return false;
  NSString* expected_string = [dirs[0] stringByAppendingPathComponent:@"SHELTER"];
  const fs::path expected = fs::u8path(std::string([expected_string UTF8String]));
  const fs::path target = fs::u8path(UserDataDir());
  if (target.filename().string() != "SHELTER" ||
      target.lexically_normal() != expected.lexically_normal()) {
    return false;
  }
  std::error_code ec;
  const fs::file_status status = fs::symlink_status(target, ec);
  if (ec == std::errc::no_such_file_or_directory) return true;
  if (ec) return false;
  if (status.type() == fs::file_type::not_found) return true;
  return security::RemoveTreeWithoutFollowingLinks(expected.parent_path(), target);
}
}  // namespace

bool DeleteUserData() {
  // Remove the key first. This is bounded and silent (no UI prompt), and its
  // worker never writes logs that could recreate the profile after removal.
  const bool key_ok = RunTimed(
      [] { return KeychainDeleteNoPrompt("SHELTER", "master-key"); }, 3000);
  const bool files_ok = RemoveOwnedUserDataDirectory();
  return key_ok && files_ok;
}

void ShowUserDataDeletionFailure() {
  NSRunAlertPanel(@"SHELTER",
                  @"Не удалось полностью удалить профиль SHELTER. Некоторые файлы или ключ в Keychain могли остаться. Разблокируйте связку ключей и проверьте права доступа.",
                  @"OK", nil, nil);
}

bool EnsureCookieKeychain() {
  std::string existing;
  OSStatus st = KeychainReadNoPrompt(kCookieSvc, kCookieAcct, &existing);
  LogKC("ensure-cookie: read os=%d", (int)st);
  if (st == errSecSuccess) { LogKC("ensure-cookie: => true (readable)"); return true; }  // читается молча — Chromium тоже прочитает
  if (st != errSecItemNotFound) { LogKC("ensure-cookie: => false (foreign/denied)"); return false; }  // чужая запись/нет доступа → mock-keychain
  // Записи нет — создаём со случайным ключом и открытой ACL: Chromium найдёт
  // её и будет шифровать куки настоящим случайным ключом без запросов
  // (запись создаётся до старта CefInitialize, см. OnBeforeCommandLineProcessing).
  // Любая ошибка → false → use-mock-keychain остаётся (статус-кво, без запросов).
  bool ok = false;
  @autoreleasepool {
    unsigned char rnd[16] = {0};
    if (SecRandomCopyBytes(kSecRandomDefault, sizeof(rnd), rnd) ==
        errSecSuccess) {
      NSData* pwd = [NSData dataWithBytes:rnd length:sizeof(rnd)];
      NSString* b64 = [pwd base64EncodedStringWithOptions:0];
      if (b64) {
        ok = KeychainPutOpen(kCookieSvc, kCookieAcct,
                             std::string([b64 UTF8String]));
      }
    }
  }
  if (!ok) return false;
  // Верификация: перечитываем без UI — если прочиталось, Chromium тоже
  // прочитает молча (ACL уже проверен при записи).
  std::string verify;
  const bool verified =
      KeychainReadNoPrompt(kCookieSvc, kCookieAcct, &verify) == errSecSuccess;
  LogKC("ensure-cookie: => %d (created+verified)", verified ? 1 : 0);
  return verified;
}


std::string UiResourceDir() {
  NSString* res = [[NSBundle mainBundle] resourcePath];
  return std::string([res UTF8String]) + "/ui";
}

std::string UserDataDir() {
  NSArray* dirs = NSSearchPathForDirectoriesInDomains(
      NSApplicationSupportDirectory, NSUserDomainMask, YES);
  NSString* base = dirs.count ? dirs[0] : NSHomeDirectory();
  return std::string([base UTF8String]) + "/SHELTER";
}

std::string DownloadsDir() {
  NSArray* dirs = NSSearchPathForDirectoriesInDomains(
      NSDownloadsDirectory, NSUserDomainMask, YES);
  NSString* d = dirs.count ? dirs[0] : NSHomeDirectory();
  return std::string([d UTF8String]);
}

void ShowInFolder(const std::string& path) {
  NSString* p = [NSString stringWithUTF8String:path.c_str()];
  [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[
    [NSURL fileURLWithPath:p]
  ]];
}

void OpenExternal(const std::string& url) {
  NSString* u = [NSString stringWithUTF8String:url.c_str()];
  NSURL* nsurl = [NSURL URLWithString:u];
  if (nsurl) [[NSWorkspace sharedWorkspace] openURL:nsurl];
}

std::string ClipboardRead() {
  NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
  return s ? std::string([s UTF8String]) : std::string();
}

bool ClipboardWrite(const std::string& text) {
  NSPasteboard* pb = [NSPasteboard generalPasteboard];
  [pb clearContents];
  return [pb setString:[NSString stringWithUTF8String:text.c_str()]
               forType:NSPasteboardTypeString];
}

void SetColorScheme(bool dark) {
  NSAppearance* a = [NSAppearance
      appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
  [NSApp setAppearance:a];
}


static int g_forced_key = 0;
static int g_redirected = 0;
static bool g_redirecting = false;
static NSWindow* g_overlay_win = nil;  // окно вида вкладки (не retain)
static std::vector<std::array<int, 5>> g_holes;  // {x, y, w, h, радиус}
static int g_hole_view_w = 0, g_hole_view_h = 0;

static bool InHole(NSEvent* e) {
  NSWindow* w = e.window;
  if (!w || w != g_overlay_win || g_holes.empty() || !w.contentView) return false;
  const CGFloat W = w.contentView.frame.size.width, H = w.contentView.frame.size.height;
  const double sx = g_hole_view_w > 0 ? W / g_hole_view_w : 1, sy = g_hole_view_h > 0 ? H / g_hole_view_h : 1;
  const double x = e.locationInWindow.x / sx, y = (H - e.locationInWindow.y) / sy;
  for (const auto& h : g_holes) {
    if (x >= h[0] && x < h[0] + h[2] && y >= h[1] && y < h[1] + h[3]) return true;
  }
  return false;
}

// Прозрачный участок («дыра») окна вкладки пропускает мышь окну интерфейса под ним.
static NSEvent* RedirectToUi(NSEvent* e) {
  NSWindow* w = e.window;
  const NSPoint sp = [w convertPointToScreen:e.locationInWindow];
  NSWindow* ui = nil;
  for (NSWindow* c in [NSApp windows]) {
    if (c == w || !c.isVisible || !c.canBecomeKeyWindow) continue;
    NSString* cn = NSStringFromClass([c class]);
    if (![cn containsString:@"NativeWidgetMac"] && ![cn hasPrefix:@"Cef"]) continue;
    if (NSPointInRect(sp, c.frame)) { ui = c; break; }
  }
  if (!ui) return e;
  if ((e.type == NSEventTypeLeftMouseDown || e.type == NSEventTypeRightMouseDown) && !ui.isKeyWindow)
    [ui makeKeyWindow];
  NSEvent* ne = [NSEvent mouseEventWithType:e.type
                                   location:[ui convertPointFromScreen:sp]
                              modifierFlags:e.modifierFlags
                                  timestamp:e.timestamp
                               windowNumber:ui.windowNumber
                                    context:nil
                                eventNumber:e.eventNumber
                                 clickCount:e.clickCount
                                   pressure:e.pressure];
  if (!ne) return e;
  g_redirected++;
  [ui sendEvent:ne];
  return nil;
}
static id g_mouse_monitor = nil;

void SetClipLevel(int) {}
std::string DumpWindowChain(void*) { return std::string(); }

void InstallInputFixes() {
  if (g_mouse_monitor) return;
  g_mouse_monitor = [NSEvent
      addLocalMonitorForEventsMatchingMask:(NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown |
                                            NSEventMaskOtherMouseDown | NSEventMaskLeftMouseUp |
                                            NSEventMaskRightMouseUp | NSEventMaskLeftMouseDragged |
                                            NSEventMaskMouseMoved)
                                   handler:^NSEvent*(NSEvent* e) {
                                     const NSEventType t = e.type;
                                     if (t == NSEventTypeLeftMouseDown || t == NSEventTypeRightMouseDown) {
                                       if (InHole(e)) { g_redirecting = true; return RedirectToUi(e); }
                                     } else if (t == NSEventTypeLeftMouseUp || t == NSEventTypeRightMouseUp ||
                                                t == NSEventTypeLeftMouseDragged) {
                                       if (g_redirecting && e.window == g_overlay_win) {
                                         if (t != NSEventTypeLeftMouseDragged) g_redirecting = false;
                                         return RedirectToUi(e);
                                       }
                                       return e;
                                     } else if (t == NSEventTypeMouseMoved) {
                                       if (InHole(e)) return RedirectToUi(e);
                                       return e;
                                     }
                                     NSWindow* w = e.window;
                                     if (w && w.isVisible && !w.isKeyWindow && w.canBecomeKeyWindow &&
                                         ![NSApp modalWindow]) {
                                       NSString* cn = NSStringFromClass([w class]);
                                       if ([cn containsString:@"NativeWidgetMac"] || [cn hasPrefix:@"Cef"]) {
                                         // Окно приложения не в фокусе (например, фокус у окна вкладки):
                                         // делаем его ключевым ДО доставки события, иначе клик тратится
                                         // на активацию и приходится кликать дважды.
                                         [w makeKeyWindow];
                                         g_forced_key++;
                                       }
                                     }
                                     return e;
                                   }];
  [g_mouse_monitor retain];
}

void ApplyViewClip(void* handle, const double radii[4],
                   const std::vector<std::array<int, 5>>& holes, int view_w, int view_h) {
  NSView* v = (NSView*)handle;
  NSWindow* win = v ? v.window : nil;
  if (!win || !win.contentView) return;
  g_overlay_win = win;
  g_holes = holes;
  g_hole_view_w = view_w;
  g_hole_view_h = view_h;
  NSView* root = win.contentView.superview ? win.contentView.superview : win.contentView;
  bool any = !holes.empty();
  for (int i = 0; i < 4; ++i) any = any || radii[i] > 0.5;
  if (!any) {
    if (root.layer) root.layer.mask = nil;
    return;
  }
  root.wantsLayer = YES;
  CALayer* layer = root.layer;
  if (!layer) return;
  const CGFloat W = win.contentView.frame.size.width, H = win.contentView.frame.size.height;
  const CGFloat sx = view_w > 0 ? W / view_w : 1, sy = view_h > 0 ? H / view_h : 1;
  const CGFloat tl = radii[0] * sx, tr = radii[1] * sx, br = radii[2] * sx, bl = radii[3] * sx;

  // Путь в координатах «сверху вниз», затем переворачиваем под систему координат слоя окна.
  CGMutablePathRef p = CGPathCreateMutable();
  CGPathMoveToPoint(p, nullptr, tl, 0);
  CGPathAddLineToPoint(p, nullptr, W - tr, 0);
  if (tr > 0) CGPathAddArc(p, nullptr, W - tr, tr, tr, -M_PI_2, 0, false);
  CGPathAddLineToPoint(p, nullptr, W, H - br);
  if (br > 0) CGPathAddArc(p, nullptr, W - br, H - br, br, 0, M_PI_2, false);
  CGPathAddLineToPoint(p, nullptr, bl, H);
  if (bl > 0) CGPathAddArc(p, nullptr, bl, H - bl, bl, M_PI_2, M_PI, false);
  CGPathAddLineToPoint(p, nullptr, 0, tl);
  if (tl > 0) CGPathAddArc(p, nullptr, tl, tl, tl, M_PI, 3 * M_PI_2, false);
  CGPathCloseSubpath(p);
  for (const auto& h : holes) {
    // Дырка повторяет форму скруглённого попапа, иначе вокруг него видна
    // «квадратная» рамка подложки окна на фоне страницы.
    const CGRect rect =
        CGRectMake(h[0] * sx, h[1] * sy, h[2] * sx, h[3] * sy);
    const CGFloat hr = static_cast<CGFloat>(h[4]) * sx;
    const CGFloat r =
        std::max<CGFloat>(0, std::min<CGFloat>(hr, std::min(CGRectGetWidth(rect),
                                                             CGRectGetHeight(rect)) / 2));
    if (r > 0.5) {
      CGPathAddRoundedRect(p, nullptr, rect, r, r);
    } else {
      CGPathAddRect(p, nullptr, rect);
    }
  }
  CGAffineTransform flip = CGAffineTransformMake(1, 0, 0, -1, 0, H);
  CGPathRef flipped = CGPathCreateCopyByTransformingPath(p, &flip);
  CGPathRelease(p);

  CAShapeLayer* mask = [CAShapeLayer layer];
  mask.frame = layer.bounds;
  mask.path = flipped;
  CGColorRef black = CGColorCreateGenericGray(0, 1);
  mask.fillColor = black;
  CGColorRelease(black);
  mask.fillRule = kCAFillRuleEvenOdd;
  CGPathRelease(flipped);
  layer.mask = mask;
}

static void DumpView(NSView* v, int depth, NSMutableString* out) {
  if (!v || depth > 5) return;
  NSRect f = v.frame;
  [out appendFormat:@"%*s%@ (%.0f,%.0f %.0fx%.0f)%@\n", depth * 2, "", NSStringFromClass([v class]),
                    f.origin.x, f.origin.y, f.size.width, f.size.height, v.hidden ? @" HIDDEN" : @""];
  for (NSView* c in v.subviews) DumpView(c, depth + 1, out);
}

// ---- миниатюра главного окна: CEF Views о сворачивании не знает ----
// (паттерн cefclient root_window_mac.mm: windowDidMiniaturize -> Hide,
// windowDidDeminiaturize -> Show) — пока окно свёрнуто, рендереры и GPU
// не производят кадры.
static id g_mini_obs = nil;
static id g_demini_obs = nil;

void UnwatchMainWindow(void* nswindow) {
  NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
  if (g_mini_obs) { [nc removeObserver:g_mini_obs]; [g_mini_obs release]; g_mini_obs = nil; }
  if (g_demini_obs) { [nc removeObserver:g_demini_obs]; [g_demini_obs release]; g_demini_obs = nil; }
}

void WatchMainWindow(void* nswindow, void (*on_mini)(void*),
                     void (*on_demini)(void*), void* ctx) {
  NSWindow* w = (NSWindow*)nswindow;
  if (!w) return;
  UnwatchMainWindow(nswindow);
  NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
  g_mini_obs = [[nc addObserverForName:NSWindowDidMiniaturizeNotification
                                object:w
                                 queue:[NSOperationQueue mainQueue]
                            usingBlock:^(NSNotification*) {
                              if (on_mini) on_mini(ctx);
                            }] retain];
  g_demini_obs = [[nc addObserverForName:NSWindowDidDeminiaturizeNotification
                                  object:w
                                   queue:[NSOperationQueue mainQueue]
                              usingBlock:^(NSNotification*) {
                                if (on_demini) on_demini(ctx);
                              }] retain];
}

std::string DebugHitTest(double x, double y) {
  NSMutableString* out = [NSMutableString string];
  for (NSWindow* w in [NSApp windows]) {
    NSView* cv = w.contentView;
    if (!cv || !w.visible) continue;
    NSRect wf = w.frame;
    NSPoint p = NSMakePoint(x, wf.size.height - y);
    NSView* hit = [cv hitTest:p];
    [out appendFormat:@"forcedKey=%d redirected=%d\n", g_forced_key, g_redirected];
    [out appendFormat:@"WINDOW %@ frame(%.0f,%.0f %.0fx%.0f) key=%d main=%d level=%ld parent=%@ children=%lu hit=%@ hitFrame=%@\n",
                      NSStringFromClass([w class]), wf.origin.x, wf.origin.y, wf.size.width, wf.size.height,
                      (int)w.isKeyWindow, (int)w.isMainWindow, (long)w.level, w.parentWindow ? @"yes" : @"no",
                      (unsigned long)w.childWindows.count, hit ? NSStringFromClass([hit class]) : @"nil",
                      hit ? NSStringFromRect(hit.frame) : @""];
    DumpView(cv, 1, out);
  }
  return std::string([out UTF8String]);
}

}  // namespace platform
}  // namespace shelter
