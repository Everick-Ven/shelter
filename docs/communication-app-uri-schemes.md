# Allowlisted communication URI schemes

**Reviewed:** 2026-11-02. **Scope:** vendor/spec documentation review and source-level tests; no Windows/macOS app-handler runtime test was performed.

SHELTER passes an external URI to the operating system only when its scheme is in the explicit allowlist in `src/communication_app_url.h`. A top-level tab or popup must have a CEF-reported user gesture; merely typing a URI is not a separate authorization path. Redirects, script navigations without such a gesture, subframes, and the privileged `shelter://` UI do not launch an external application. The platform launch functions repeat the allowlist check.

Ordinary `https://` universal/app links stay as HTTPS navigations in Chromium; SHELTER does not rewrite them to a custom URI scheme. Whether a handler is installed, what confirmation the OS displays, and whether an app opens successfully remain platform/runtime acceptance items.

## Allowlist

| Scheme | Use | Reference |
|---|---|---|
| `tg` | Telegram deep links such as `tg://resolve?domain=...` | [Telegram — Deep links](https://core.telegram.org/api/links) |
| `sgnl` | Signal Desktop's build manifest registers the `sgnl` protocol handler | [Signal Desktop — package.json](https://github.com/signalapp/Signal-Desktop/blob/main/package.json); [Signal Support — Installing Signal](https://support.signal.org/hc/en-us/articles/360008216551-Installing-Signal) |
| `slack` | Slack desktop/mobile deep links such as `slack://open?team=...` | [Slack — Deep linking into Slack](https://docs.slack.dev/interactivity/deep-linking/) |
| `msteams` | Direct Microsoft Teams desktop deep links | [Microsoft Learn — Deep links overview](https://learn.microsoft.com/en-us/microsoftteams/platform/concepts/build-and-test/deep-links). Microsoft recommends HTTPS unless the desktop client is known to be installed. |
| `zoommtg` | Zoom client deep links used by the documented external-browser authentication flow | [Zoom — Authentication](https://developers.zoom.us/docs/zoom-apps/authentication/) |
| `webextel` | Cisco Webex click-to-call links | [Cisco Webex — Cross-launch URL for sign-in and calling](https://help.webex.com/en-us/article/n45mhmab/Webex-App-%7C-Cross-launch-URL-for-sign-in-and-calling) |
| `matrix` | Matrix entity links, in opaque or authority form | [Matrix Specification — Matrix URI scheme](https://spec.matrix.org/latest/appendices/#matrix-uri-scheme) |
| `facetime` | FaceTime video call links | [Apple — FaceTime Links](https://developer.apple.com/library/archive/featuredarticles/iPhoneURLScheme_Reference/FacetimeLinks/FacetimeLinks.html) |
| `mailto`, `tel`, `sms` | OS-level email, telephone, and text-message links | [RFC 6068 — The `mailto` URI Scheme](https://www.rfc-editor.org/rfc/rfc6068); [RFC 3966 — The `tel` URI](https://www.rfc-editor.org/rfc/rfc3966); [Apple — SMS Links](https://developer.apple.com/library/archive/featuredarticles/iPhoneURLScheme_Reference/SMSLinks/SMSLinks.html) |

Signal's package manifest and official support instructions confirm the `sgnl` handler, but do not specify its full URI grammar. The Signal sample in the test is scheme-dispatch coverage, not a claim that every Signal deep-link form is supported.

The native parser additionally enforces ASCII scheme syntax, case-insensitive matching, an 8192-byte maximum, no whitespace/control characters, and a basic authority/opaque URI shape for each allowlisted scheme. It is not a substitute for each app's full URI grammar; final handling is delegated to the registered OS application.

## Deliberately not allowlisted

- `whatsapp://` is not included. Meta's public business-link examples use HTTPS links such as `https://wa.me/...`; those remain HTTPS and are not rewritten by SHELTER. See [Meta — QR codes and short links](https://developers.facebook.com/documentation/business-messaging/whatsapp/qr-codes).
- `line://` is not included because LINE marks it deprecated and documents a takeover risk; its documented current links use HTTPS. See [LINE Developers — Use LINE features with the LINE URL scheme](https://developers.line.biz/en/docs/messaging-api/using-line-url-scheme/).
- `zoomus://` is not included: Zoom's SDK examples document it for Android/iOS; the desktop handler used here is `zoommtg`. See [Zoom Meeting SDK — Android](https://developers.zoom.us/docs/meeting-sdk/android/resource/launch-zoom-client-from-your-app/) and [iOS](https://developers.zoom.us/docs/meeting-sdk/ios/resource/launch-zoom-client-from-your-app/).
- `viber://` is not included because the vendor's deep-link examples target mobile flows and do not confirm Windows/macOS desktop handling. See [Viber — Deep links](https://developers.viber.com/docs/tools/deep-links/).
- `facetime-audio://` is not included because Apple documents that variant for iOS only; the allowlist keeps the desktop-relevant `facetime://` form. See [Apple — FaceTime Links](https://developer.apple.com/library/archive/featuredarticles/iPhoneURLScheme_Reference/FacetimeLinks/FacetimeLinks.html).
- `discord`, `weixin`, `webex`, `ms-teams`, `skype`, and other app-specific aliases are not included unless a supported scheme and form can be confirmed from the vendor's current documentation. For Webex, only the documented `webextel` call handler is included; authentication handlers are not.
- `intent://` and arbitrary/unrecognized schemes are always rejected. There is no generic “dispatch any scheme” fallback.

`tests/communication_app_protocol_test.cc` executes accepted/rejected scheme and URI-form checks. `tests/communication_app_protocol_test.js` checks the user-gesture/redirect guards and Windows/macOS dispatch call sites in source text. These tests do not exercise CEF callbacks or installed handlers; runtime acceptance is still needed on both supported platforms.
