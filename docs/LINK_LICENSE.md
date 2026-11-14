> **Decided (Clayton, Nov 4, 2026):** leave Ableton Link off. Use MIDI Clock send/receive + Wi-Fi band sync instead. This file is kept for reference only.

# Ableton Link: licence status and how to apply (checked Nov 4, 2026)

**Status in v0.16:** Link is **off by default** (`WR_WITH_LINK=OFF`). Band sync still works without it: the leader sends tempo, play and bar-quantised section cues over the existing sync connection, and every iPad runs its own click at that tempo (`FakeClock` fallback). With Link, the beat phase between iPads is tighter. An App Store build refuses to configure with Link on unless `WR_LINK_LICENSED=ON`.

## What the licences say
- **Cross-platform Link (C++, github.com/Ableton/link)**, which this project fetches today, is dual-licensed: **GPLv2+**, or a **proprietary licence** you get by contacting **link-devs@ableton.com**. The GPL isn't compatible with the iOS App Store, and Bearing Live is closed-source, so the GPL route is out for the App Store.
- **LinkKit (iOS SDK, github.com/Ableton/LinkKit)** is a prebuilt iOS library under the **Ableton Link SDK License v2.0**. It's free and royalty-free, and it allows publishing and selling your own branded iOS app that uses it. Conditions include:
  - no redistributing the SDK itself;
  - following Ableton's Link UI guidelines (the Link settings pane, naming);
  - **Link support may not be sold as an in-app purchase.** Ours is part of the subscription; confirm with Ableton that this is acceptable.
  - Build against an official (non-pre-release) LinkKit release. A *custom* LinkKit build from source falls back under the GPL.
- iOS 14+/17: Link needs `NSLocalNetworkUsageDescription` (already present for Bonjour) and the **`com.apple.developer.networking.multicast` entitlement**, which you request from Apple. It needs a paid developer account and a short justification that references Ableton Link.

## Recommended path (Clayton)
1. Email **link-devs@ableton.com** from the developer account's address. Say: "Bearing Live, a closed-source iOS app for church worship bands, sold by monthly subscription on the App Store. We'd like to use Link for tempo/beat sync between band members' iPads. Should we use LinkKit under the Link SDK License, or do you need a proprietary licence for the cross-platform C++ library? Link is included for all subscribers, not as a separate in-app purchase."
2. If they say LinkKit, switch `core/sync/LinkClock.h` to LinkKit's C API (`ABLLinkNew`, `ABLLinkCaptureAudioSessionState`, `ABLLinkBeatAtTime`, ...). It's a small adapter behind the existing `BeatClock` interface. Then add the Link settings pane per Ableton's UI guidelines and follow the LinkKit test plan.
3. Request the multicast entitlement from Apple.
4. Configure the App Store build with `-DWR_WITH_LINK=ON -DWR_LINK_LICENSED=ON`.

Until then, ship with Link OFF; nothing else in the app depends on it.
