#ifndef OS_MAC
#define OS_MAC 1  // cef_application_mac.h гейтит протокол на этом макросе.
#endif
#include "include/cef_application_mac.h"

#import <Cocoa/Cocoa.h>
#include "app/platform/platform.h"
#include <string>

// CEF требует от клиентских приложений на macOS подкласс NSApplication,
// реализующий CefAppProtocol (cef_application_mac.h: «All CEF client
// applications must subclass NSApplication and implement this protocol»).
// Реализация — как в tests/cefsimple/cefsimple_mac.mm и CEF_SHEL.
@interface ShelterApplication : NSApplication <CefAppProtocol> {
 @private
  BOOL handlingSendEvent_;
}
@end

@implementation ShelterApplication

- (BOOL)isHandlingSendEvent {
  return handlingSendEvent_;
}

- (void)setHandlingSendEvent:(BOOL)handlingSendEvent {
  handlingSendEvent_ = handlingSendEvent;
}

- (void)sendEvent:(NSEvent*)event {
  CefScopedSendingEvent sendingEventScoper;
  [super sendEvent:event];
}

@end

namespace shelter {

void MacInstallApplication() {
  @autoreleasepool {
    [ShelterApplication sharedApplication];
  }
}

std::optional<std::string> PlatformClipboardRead() {
  @autoreleasepool {
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    if (!pasteboard) return std::nullopt;
    NSString* text = [pasteboard stringForType:NSPasteboardTypeString];
    if (!text) return std::nullopt;
    const char* utf8 = [text UTF8String];
    return std::string(utf8 ? utf8 : "");
  }
}
bool PlatformClipboardWrite(std::string_view text) {
  @autoreleasepool {
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    if (!pasteboard) return false;
    NSString* string = [[NSString alloc] initWithBytes:text.data()
                                                length:text.size()
                                              encoding:NSUTF8StringEncoding];
    if (!string) return false;
    [pasteboard clearContents];
    BOOL ok = [pasteboard setString:string forType:NSPasteboardTypeString];
    return ok ? true : false;
  }
}
std::string PlatformDefaultDownloadsDir() {
  @autoreleasepool {
    const char* home = [NSHomeDirectory() UTF8String];
    return std::string(home ? home : "/tmp") + "/Downloads";
  }
}
}  // namespace shelter
