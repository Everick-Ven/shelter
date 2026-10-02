#import <Cocoa/Cocoa.h>
#include "app/platform/platform.h"
#include <string>
namespace shelter {
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
