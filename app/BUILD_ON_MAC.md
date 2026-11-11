# Build Bearing Live on your Mac and put it on your iPad (free Apple ID)

This guide assumes you have **never used Xcode before**. Follow it top to bottom. It takes about an hour the first time (most of it is downloads).

**Your setup:** iPad Pro 12.9" or 10.5" (2017, Lightning port) on **iPadOS 17** (the last version it gets), a Mac, a Lightning cable, and later the MOTU M4.

## What you need
| Thing | Where to get it | Notes |
|---|---|---|
| A Mac on **macOS 14 Sonoma or newer** | - | Xcode 15/16 needs it. Xcode 15.0 or newer is required for iPadOS 17 devices. |
| **Xcode** | Mac App Store (free, ~10 GB) | Open it once after installing and let it install "components". When asked which platforms, tick **iOS**. |
| **Homebrew** + **CMake** + **git** | Terminal: `/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"` then `brew install cmake git` | git also comes with Xcode's command line tools. |
| A **free Apple ID** | the one you already use | No paid developer account needed for steps 1-6. |
| **Lightning to USB 3 Camera Adapter** (Apple) | Apple / Best Buy | Needed for the M4 and the pedals on a Lightning iPad. Plug its Lightning-power port into a charger: the M4 + 2 pedals need more power than the iPad gives. A USB-C hub on the adapter's USB-A port is fine. |

Two apps are built from one shared core:

| Xcode scheme | App on the iPad | Bundle id | What it is |
|---|---|---|---|
| **TabernacleGuitar_Standalone** (+ AUv3 extension) | Bearing Live Guitar | `<prefix>.guitar` | The full guitar app |
| **TabernacleCoreTest_Standalone** | Bearing Live Core Test | `<prefix>.coretest` | Setlist / chart / click / sync / projector only |

## Step 1. Get the code and JUCE
Open **Terminal** (Applications > Utilities) and paste one line at a time:
```bash
mkdir -p ~/Developer && cd ~/Developer
# put the unzipped worship-rig folder here (double-click the zip in Finder, then drag the folder into ~/Developer)
cd worship-rig/app
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE.git JUCE
```

## Step 2. Generate the Xcode project
Pick a **bundle id prefix** that is yours and that nobody else has used, e.g. `com.claytonnelson.tabernacletools` (lowercase, no spaces). Keep the same one forever (see "free account limits" below).
```bash
cmake -S . -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DWR_BUILD_APP=ON -DWR_BUILD_TESTS=OFF \
  -DWR_BUNDLE_PREFIX=com.claytonnelson.tabernacletools
open build-ios/TabernacleTools.xcodeproj
```
If CMake says *"No CMAKE_Swift_COMPILER could be found"* or *"Xcode not found"*: run `sudo xcode-select -s /Applications/Xcode.app` and `xcodebuild -runFirstLaunch`, then try again.

## Step 3. Sign in to Xcode with your Apple ID
1. Xcode menu > **Settings...** (⌘,) > **Accounts** tab.
2. Click **+** (bottom left) > **Apple ID** > sign in.
3. You should now see your name with **"Personal Team"** under it. That free team is what signs the app.

## Step 4. Set the team on each target
1. In the left sidebar click the blue **TabernacleTools** project icon at the very top.
2. In the list that appears (under TARGETS), click **TabernacleGuitar_Standalone**.
3. Top tabs: **Signing & Capabilities**.
4. Tick **Automatically manage signing**. Set **Team** to *Your Name (Personal Team)*.
5. Check **Bundle Identifier** reads `com.claytonnelson.tabernacletools.guitar`.
6. Repeat for **TabernacleGuitar_AUv3** (its id must start with the Guitar app's id; leave JUCE's value) and **TabernacleCoreTest_Standalone**.
7. If any target shows a capability you can't use with a free team (**App Groups**, **In-App Purchase**, **Sign in with Apple**), click the small trash icon next to it. The normal (non-store) build doesn't add them.

Tip: add `-DWR_TEAM_ID=XXXXXXXXXX` to step 2 to skip this by hand. Find the 10-character id in Xcode's Accounts tab > your team, or in the error text of a failed build.

## Step 5. Prepare the iPad (once)
1. Plug the iPad into the Mac with the Lightning cable. Unlock it. Tap **Trust** and enter the passcode.
2. In Xcode: menu **Window > Devices and Simulators**. Wait until the iPad is listed and "Preparing..." finishes (first time: several minutes, it copies debug symbols for iPadOS 17).
3. On the iPad: **Settings > Privacy & Security > Developer Mode** > on > **Restart**. After restart, confirm **Turn On**. (The switch appears only after the iPad has been connected to Xcode.)

## Step 6. Build and run
1. At the top of Xcode, click the scheme name and choose **TabernacleGuitar_Standalone**. Next to it, choose your iPad (not "Any iOS Device", not a simulator).
2. Press **▶ Run** (⌘R). The first build takes 5-15 minutes.
3. The app installs, but iOS refuses to open it the first time ("Untrusted Developer"). On the iPad: **Settings > General > VPN & Device Management** > your Apple ID > **Trust**. Press Run again.
4. To install the test harness too, choose **TabernacleCoreTest_Standalone** and Run.
5. **Free-account apps stop opening after 7 days.** Plug in and press Run again; your songs and setlists are kept.

## Signing errors and fixes
| What Xcode says | What to do |
|---|---|
| *"Signing for ... requires a development team"* | Step 4: pick your Personal Team on that target. |
| *No account / "No Accounts"* | Step 3. If the team list is empty, sign out and back in to the Apple ID in Settings > Accounts. |
| *"Failed to register bundle identifier"* or *"The app identifier ... cannot be registered to your development team"* / *"App ID is not available"* | Somebody (maybe you, on another account) already used that id. Change the prefix in step 2 (e.g. add your initials), re-run the cmake line, reopen the project, redo step 4. |
| *"No profiles for ... were found"* / *"Xcode couldn't find any iOS App Development provisioning profiles"* | Make sure the iPad is plugged in, unlocked and selected as the run destination, then click **Try Again** in Signing & Capabilities. Xcode only makes a free profile for a device it can see. |
| *"Personal development teams ... do not support the App Groups / In-App Purchase / Sign in with Apple capability"* | Delete that capability (step 4.7), or regenerate without `-DWR_USE_APP_GROUP=ON` / `-DWR_STORE_BUILD=ON`. Those need a paid account. |
| *"Your maximum App ID limit has been reached. You may create up to 10 App IDs every 7 days"* | Wait (the message says until when) or reuse a prefix you already used. Don't keep changing the prefix. |
| *"The maximum number of apps for free development profiles has been reached"* | Free accounts can have **3** sideloaded apps on a device at once. Delete one from the iPad. Guitar + Core Test = 2. |
| *"Untrusted Developer"* on the iPad | Step 6.3. |
| *"Developer Mode disabled"* / app won't launch from Xcode | Step 5.3. |
| *"iPad is busy: Preparing ... / Copying shared cache symbols"* | Wait; it finishes. Keep it unlocked. |
| *"... iPadOS 17.x doesn't match Xcode / device support files missing"* | Update Xcode from the App Store (needs 15.0+ for iPadOS 17). |
| *"Could not launch ... the app has expired"* (after 7 days) | Press Run again. |
| *Any compile error* | Copy the red error text (right-click > Copy) and send it to Grok Bot. |

**Free account limits:** 3 sideloaded apps per device, 10 new App IDs per 7 days (Guitar app + AUv3 + Core Test = 3 IDs; the optional share extension = 1 more), 7-day app expiry. No App Groups, In-App Purchase or Sign in with Apple.

## Optional extras (free account)
- **Share extension:** add `-DWR_SHARE_EXTENSION=ON` to step 2 (costs one more App ID). Then in Safari/Notes/Files: select chords > **Share** > **Bearing Live**, then open Bearing Live Guitar to see the import preview. Without it, use **Open in / Copy to Bearing Live Guitar** from Files or Mail, or **Paste chart** in the Setlist builder.
- **Your own cab IRs (v0.12):** put `.wav` impulse responses in iCloud Drive / On My iPad. In WORKSHOP > amp section tap **Load IR file...**, pick the file. The app copies it into its own storage (so it still works if the original is moved). **Replace IR** / **Remove** sit next to it. WAV, mono or stereo, up to 8 MB; IRs longer than about 21 ms are shortened.
- **Amp anti-aliasing (v0.12):** Settings > *Amp & drive anti-aliasing (oversampling)*: Off / 2x / 4x (default) / 8x. "Reduce automatically when the iPad is busy" steps it down if the audio thread gets close to running out of time. The menu shows the added latency (about 0.1 ms per amp or drive pedal at 4x).

## App Store build (later, needs the paid $99/year Apple Developer Program)
Subscriptions (Individual $4.99/month, Church $19.99/month, 14-day free trial), the church seat service and Sign in with Apple only exist in the **store build**. A free Personal Team **cannot** sign it, so skip this until you have a paid account. The normal sideload build above has everything unlocked.

### What CMake does for you
```bash
cmake -S . -B build-store -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DWR_BUILD_APP=ON -DWR_BUILD_TESTS=OFF -DWR_STORE_BUILD=ON \
  -DWR_BUNDLE_PREFIX=com.claytonnelson.tabernacletools -DWR_TEAM_ID=XXXXXXXXXX
```
`-DWR_STORE_BUILD=ON` turns on the subscription locks and adds the Swift StoreKit 2 file `core/ui/store/WRStoreKit.swift` to the app (Swift 5, StoreKit framework, `-ObjC` link flag). **No bridging header is needed:** C++ talks to Swift through `core/ui/store/StoreBridge_ios.mm`, which finds the Swift class at runtime by name (`WRStoreKitBridge`).

### Then, in Xcode (once)
1. **TabernacleGuitar_Standalone > Signing & Capabilities > + Capability**: add **In-App Purchase** and **Sign in with Apple** (and **App Groups** if you use `-DWR_USE_APP_GROUP=ON`).
2. **Local purchase testing without App Store Connect:** File > New > File... > **StoreKit Configuration File** (name it `TabernacleTools.storekit`, tick "Sync with App Store Connect" only once the products exist there). Add an **Auto-Renewable Subscription** group "Bearing Live" with:
   - `com.tabernacletools.guitar.individual.monthly` - 1 month - $4.99 - Introductory offer: Free trial, 2 weeks
   - `com.tabernacletools.guitar.church.monthly` - 1 month - $19.99 - same free trial; rank it above Individual (upgrade).
   Then Product > Scheme > **Edit Scheme...** > Run > Options > **StoreKit Configuration**: choose the file. Purchases are now fake and instant on the iPad; Debug > StoreKit > **Manage Transactions** lets you expire / refund them to test the read-only mode.
3. **If Swift doesn't get built** (older CMake, or you added files by hand): select `WRStoreKit.swift` in the sidebar, and in the right-hand File Inspector tick **Target Membership: TabernacleGuitar** (the "Shared Code" target). In that target's Build Settings set **Swift Language Version = Swift 5**. If Xcode offers to "create an Objective-C bridging header", you can click **Don't Create** (it isn't used).
4. **If the link fails with `Undefined symbol: _swift_...` or `__swift_FORCE_LOAD...`**: the app target needs to know it contains Swift. File > New > File... > Swift File, name it `Empty.swift`, add it to **TabernacleGuitar_Standalone** only, click **Don't Create** for the bridging header. Build again.
5. **If purchases never show up** (`StoreKit bridge not found` in the Xcode console): the Swift class was stripped. Check Build Settings > **Other Linker Flags** on TabernacleGuitar_Standalone contains `-ObjC`.


## 6. Hook up the rig (MOTU M4, 4-cable)
| M4 | goes to |
|---|---|
| IN 1 (Hi-Z, press the INST button) | guitar |
| IN 2 | Simplifier **effects loop SEND** |
| OUT 1 (rear TRS out 1) | Simplifier **guitar INPUT** (the BEFORE AMP lane) |
| OUT 3 / OUT 4 | Simplifier **loop RETURN** L / R (the AFTER AMP lane, stereo) |

- Connect the M4 to the iPad through a **powered USB-C hub**. Plug the two Chocolate Plus pedals into the same hub.
- In the app, tap **Settings** (top right), then *Audio interface...*. Choose the MOTU M4 at 48 kHz with a **64 or 128** sample buffer, and enable inputs 1-2 and outputs 1-4.
- OUT 1 is line level. The app starts it at **-10 dB** (adjust it in WORKSHOP under *OUT 1 TRIM*), so it doesn't overload the amp's front end.
- If you only have 2 outputs, the app automatically runs **2-cable** mode: everything comes out of outs 1/2. You can also force this with *Settings > Routing: 2-cable*.

## 7. Chocolate Plus setup (both units)
In the M-Vave app (or with the pedal's own setup mode), set each switch to send **CC, momentary** (127 on press, 0 on release). The app needs the release message to detect long presses.

| Unit | MIDI ch | A | B | C | D |
|---|---|---|---|---|---|
| Guitar | 1 | CC 80 Tap tempo | CC 81 Boost | CC 82 per-song (Octave) | CC 83 per-song (Freeze) |
| Sections | 2 | CC 80 Prev section | CC 81 Next section | CC 82 Jump to chorus | CC 83 Next song (hold = previous song) |

If your units send different numbers, go to SETLIST, tap a switch box at the bottom, choose **Learn**, and press the footswitch. The mapping is saved. To swap in the spare, set it to the same channel and CCs, or Learn again; it takes about a minute.

## 8. Cubase (optional)
- On the iPad, the app turns on the **CoreMIDI network session**. On the Mac, open **Audio MIDI Setup > MIDI Studio > Network**, find the iPad's session, and click **Connect**.
- Cubase **MIDI Clock** out sets the app's tempo (turn it on or off in *Settings > Follow MIDI Clock*).
- **Program Change n** or **note (C1 = 36) + n** on **channel 16** jumps to section n (0 = first section) and switches the chart to section view. CC 102 and CC 103 (value 127) mean next and previous section. You can change the channel in Settings.

## v0.7 features to try
- **Practice tracks:** tap the song title on Live > *Practice track...* to pick an mp3 / m4a / wav from Files. Practice mode plays it through the main outs (1/2), with speed (no pitch change), section loop and section marks. It's off in live mode and while following a leader.
- **Arrangement:** tap the song title > *Arrangement...*, or use the Setlist entry menu for this service only.
- **Import:** use *Paste chart* or *Import file* in the Setlist builder, or the share routes above. Always check the preview (title, artist, key; "guessed" means it came from the chords).

## GitHub Actions (build check without a Mac session)
`.github/workflows/ios.yml` runs on every push and pull request (or by hand: Actions tab > *Run workflow*):
1. **core-tests** (Ubuntu): builds `worship_core` + `worship_guitar_dsp` and runs both test suites with `ctest` (`core_*` and `guitar_*`). The renders (WAVs, projector PNG) are uploaded as the `renders` artifact.
2. **ios** (macOS 14): clones JUCE 8.0.4, generates the Xcode project, and builds **TabernacleGuitar_Standalone** (app + AUv3) and **TabernacleCoreTest_Standalone** for an iPad, **unsigned**. Then it runs the core + guitar suites on macOS too.

The CI output is unsigned, so it can't be installed on the iPad. It only proves that both targets compile and link. To install, build on the Mac with your Personal Team (steps 3 to 5). To use it, push the `app` folder to a GitHub repo and open the **Actions** tab. Public repos build free. Private repos use the account's monthly Actions minutes, and macOS minutes count at a higher rate than Linux, so check GitHub's current billing page.

## Run the core tests on the Mac (optional)
```bash
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release && cmake --build build-mac -j && ctest --test-dir build-mac --output-on-failure
```

## Other troubleshooting
- *No input sound*: Settings > Bearing Live Guitar > **Microphone** > allow (iOS asks once on first launch).
- *M4 not seen*: the Camera Adapter needs a charger in its Lightning port; check the M4's USB light. Settings > Audio interface... > MOTU M4.
- *Crackles*: buffer 128, or anti-aliasing 2x / Off. The 2017 A10X is slower than newer iPads.


## Backing tracks (v0.14) - first test
1. Live view > tap the song title > "Backing tracks: import stems..." and pick the stem files (or the folder) in the Files app. Import converts them once; a 5-minute song with 8 stems takes a little while on the 2017 iPad Pro.
2. If the stems have no section markers, play, and tap MARK SECTION HERE at the start of each section.
3. Open MIXER: mute the parts you play live; set click/guide stems to IN-EARS.
4. Watch the tracks bar for a red "storage can't keep up" or CPU warning and tell me.

## v0.16 notes
- Ableton Link is OFF by default (`-DWR_WITH_LINK=OFF`). Band sync uses the built-in clock. Do not ship with Link until the licence is sorted (docs/LINK_LICENSE.md).
- Recorded voice cues: copy WAVs named as in resources/voice/RECORDING_SCRIPT.md into the app's `Voice/` data folder (Files app); they replace the built-in ones on next launch.

## v0.17 MIDI Clock
- Settings > MIDI Clock: Off / Send / Receive. Send creates a virtual **Bearing Live Clock** port (other apps) and can also target USB / Bluetooth / network MIDI outs. Receive follows external tempo + transport (smoothed).
- Ableton Link stays OFF. Band sync is Wi-Fi leader/follower; use MIDI Clock to talk to Cubase, AUM, Loopy, etc.
- Cubase: enable its MIDI Clock out and set Bearing Live to Receive (or the other way around).

## v0.19 rename
- The app is now **Bearing Live** (company **Bearing**). The Xcode project is `TabernacleTools.xcodeproj`, the scheme `TabernacleGuitar_Standalone`. Set the home-screen name in Xcode to **Bearing Live** (target TabernacleGuitar_Standalone, General, Display Name). Do not edit CMakeLists.txt to do that. Default bundle prefix `com.tabernacletools` (use your own prefix for Personal Team sideloads). The data folder is now `TabernacleTools` - copy any test library from the old `WorshipRig` folder in the Files app if you need it.
