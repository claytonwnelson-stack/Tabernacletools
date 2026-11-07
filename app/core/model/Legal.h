// v0.16 Terms of Use + Privacy Policy links (Settings > About & legal, and the paywall - App Review requires both on the
// subscription screen). The texts are docs/TERMS.md and docs/PRIVACY.md; host them at these URLs before submitting.
// PLACEHOLDER domain: change both URLs (and App Store Connect's Privacy Policy URL) once the pages are live.
#pragma once
namespace wr::legal {
inline const char* termsUrl()   { return "https://tabernacletools.app/terms"; }
inline const char* privacyUrl() { return "https://tabernacletools.app/privacy"; }
// Apple's standard EULA is used unless a custom one is uploaded in App Store Connect; our Terms add to it.
inline const char* appleEulaUrl() { return "https://www.apple.com/legal/internet-services/itunes/dev/stdeula/"; }
}
