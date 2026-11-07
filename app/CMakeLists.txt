cmake_minimum_required(VERSION 3.22)
project(TabernacleTools VERSION 0.19.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------- platform-independent core (no JUCE)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/resources/he-who-was.cho WR_SEED_CHO)
configure_file(core/model/SeedChart.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/SeedChart.cpp @ONLY)
# worship-core: everything instrument-agnostic (songs/ChordPro, transpose, key/capo, setlists, sharing export, backups,
# MIDI/clock router, live session, click + presets + count-in + voice cues + bar grid, band sync + Link + roster,
# projector lyrics). No JUCE, no guitar DSP. Shared by every app (Guitar now; Bass, Keys, Chart-only later).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE) # DSP CPU checks assume optimised code
endif()

option(WR_STORE_BUILD "App Store build: subscription locks, StoreKit 2, church seats (needs a paid Apple Developer account)" OFF)
if(WR_STORE_BUILD AND APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
    enable_language(Swift)
endif()

add_library(worship_core STATIC
    core/audio/Voice.cpp core/audio/ClickEngine.cpp core/audio/PracticePlayer.cpp core/audio/MultitrackPlayer.cpp core/audio/MultitrackLive.cpp core/model/Multitrack.cpp core/model/Ccli.cpp
    core/model/Chart.cpp core/model/Song.cpp core/model/LiveSession.cpp core/model/Backup.cpp core/model/Export.cpp core/model/Pdf.cpp core/model/Projector.cpp core/model/Import.cpp core/model/SectionColors.cpp core/model/StageTools.cpp core/audio/Recorder.cpp core/model/Packs.cpp core/model/StoreMapping.cpp core/model/CrossApp.cpp core/model/Hymns.cpp core/model/HymnData.cpp
    core/sync/Sync.cpp core/sync/Roster.cpp core/sync/Net.cpp core/sync/Discovery.cpp core/sync/SeatClient.cpp
    core/third_party/tweetnacl/tweetnacl.c core/third_party/tweetnacl/randombytes.c
    core/midi/MidiRouter.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/SeedChart.cpp)
target_include_directories(worship_core PUBLIC core)
find_package(Threads REQUIRED)
target_link_libraries(worship_core PUBLIC Threads::Threads)   # v0.14 backing-track disk reader thread
set_target_properties(worship_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# guitar DSP: pedals, two-lane rig, 4-cable routing, rig-change tails (Guitar app only)
add_library(worship_guitar_dsp STATIC guitar/dsp/Pedals.cpp guitar/dsp/Rig.cpp guitar/dsp/AmpSection.cpp guitar/dsp/PedalFaces.cpp)
target_include_directories(worship_guitar_dsp PUBLIC guitar)
target_link_libraries(worship_guitar_dsp PUBLIC worship_core)
set_target_properties(worship_guitar_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
# keys DSP (v0.9 stub: patches + key-matched pad over the wk engine) - for the future Tabernacle Tools - Keys app
add_library(worship_keys_dsp STATIC keys/dsp/KeysEngine.cpp)
target_include_directories(worship_keys_dsp PUBLIC keys)
target_link_libraries(worship_keys_dsp PUBLIC worship_core)
set_target_properties(worship_keys_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(WR_BUILD_TESTS "Build core + guitar unit tests and offline renders" ON)
option(WR_BUILD_APP "Build the JUCE apps (Tabernacle Tools - Electric Guitar + Tabernacle Tools Core Test)" OFF)

if(WR_BUILD_TESTS AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_testing()
    # ---- core suite (links worship_core only - proves the core builds and runs without guitar code)
    add_executable(wr_core_tests tests/core/test_core.cpp)
    target_link_libraries(wr_core_tests PRIVATE worship_core)
    target_compile_definitions(wr_core_tests PRIVATE WR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME core_unit COMMAND wr_core_tests)
    add_executable(wr_sync_tests tests/core/test_sync.cpp)
    target_link_libraries(wr_sync_tests PRIVATE worship_core)
    add_test(NAME core_sync_projector_roster COMMAND wr_sync_tests)
    add_executable(wr_v07_tests tests/core/test_v07.cpp)
    target_link_libraries(wr_v07_tests PRIVATE worship_core)
    add_test(NAME core_v07_arrangement_notes_health_practice COMMAND wr_v07_tests)
    add_executable(wr_import_tests tests/core/test_import.cpp)
    target_link_libraries(wr_import_tests PRIVATE worship_core)
    add_test(NAME core_v07_share_import_parser COMMAND wr_import_tests)
    add_executable(wr_v08_tests tests/core/test_v08_colors.cpp)
    target_link_libraries(wr_v08_tests PRIVATE worship_core)
    add_test(NAME core_v08_section_colors COMMAND wr_v08_tests)
    add_executable(wr_v08_tools_tests tests/core/test_v08_tools.cpp)
    target_link_libraries(wr_v08_tools_tests PRIVATE worship_core)
    add_test(NAME core_v08_setlist_scroll_banks_capo_stats_backup_recorder COMMAND wr_v08_tools_tests)
    add_executable(wr_hymn_tests tests/core/test_hymns.cpp)
    target_link_libraries(wr_hymn_tests PRIVATE worship_core)
    add_test(NAME core_v010_hymn_library COMMAND wr_hymn_tests)
    add_executable(wr_seat_tests tests/core/test_seats.cpp)
    target_link_libraries(wr_seat_tests PRIVATE worship_core)
    add_test(NAME core_v011_church_seats COMMAND wr_seat_tests)
    add_executable(wr_core_render tests/core/render_core.cpp)
    target_link_libraries(wr_core_render PRIVATE worship_core)
    target_compile_definitions(wr_core_render PRIVATE WR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME core_render COMMAND wr_core_render ${CMAKE_CURRENT_BINARY_DIR}/renders)
    add_executable(wr_projector_png tests/core/projector_png.cpp)
    target_link_libraries(wr_projector_png PRIVATE worship_core)
    add_test(NAME core_projector_png COMMAND wr_projector_png ${CMAKE_CURRENT_BINARY_DIR}/renders/projector-sample.png)
    # Ableton Link peers test: cmake -DWR_LINK_DIR=/path/to/link (git clone --recursive https://github.com/Ableton/link)
    set(WR_LINK_DIR "" CACHE PATH "Ableton Link SDK checkout (optional, for wr_link_test)")
    if(WR_LINK_DIR)
        include(${WR_LINK_DIR}/AbletonLinkConfig.cmake)
        add_executable(wr_link_test tests/core/test_link.cpp)
        target_link_libraries(wr_link_test PRIVATE worship_core Ableton::Link)
        add_test(NAME core_link COMMAND wr_link_test)
    endif()
    # ---- guitar suite
    add_executable(wr_guitar_tests tests/guitar/test_guitar.cpp)
    target_link_libraries(wr_guitar_tests PRIVATE worship_guitar_dsp)
    add_test(NAME guitar_unit COMMAND wr_guitar_tests)
    add_executable(wr_v09_tests tests/guitar/test_v09.cpp)
    target_link_libraries(wr_v09_tests PRIVATE worship_guitar_dsp worship_keys_dsp)
    add_test(NAME v09_pedals_amp_cab_packs_keys COMMAND wr_v09_tests)
    add_executable(wr_v012_tests tests/guitar/test_v012.cpp)
    target_link_libraries(wr_v012_tests PRIVATE worship_guitar_dsp)
    add_test(NAME guitar_v012_antialiasing COMMAND wr_v012_tests)
    add_executable(wr_v012_store_tests tests/core/test_v012_store.cpp)
    target_link_libraries(wr_v012_store_tests PRIVATE worship_guitar_dsp)
    add_test(NAME v012_store_locks_sections_ir COMMAND wr_v012_store_tests)
    add_executable(wr_v013_trial_tests tests/core/test_v013_trial.cpp)
    target_link_libraries(wr_v013_trial_tests PRIVATE worship_core)
    add_test(NAME v013_one_trial_per_person COMMAND wr_v013_trial_tests)
    add_executable(wr_v014_multitrack_tests tests/core/test_v014_multitrack.cpp)
    target_link_libraries(wr_v014_multitrack_tests PRIVATE worship_core)
    add_test(NAME v014_multitrack COMMAND wr_v014_multitrack_tests)
    add_executable(wr_v015_ccli_tests tests/core/test_v015_ccli.cpp)
    target_link_libraries(wr_v015_ccli_tests PRIVATE worship_core)
    add_test(NAME v015_ccli_usage_log COMMAND wr_v015_ccli_tests)
    add_executable(wr_v016_tests tests/core/test_v016.cpp)
    target_link_libraries(wr_v016_tests PRIVATE worship_core)
    target_compile_definitions(wr_v016_tests PRIVATE WR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME v016_voice_hymns_tempo_resequence_ccli COMMAND wr_v016_tests)
    add_executable(wr_v017_tests tests/core/test_v017.cpp)
    target_link_libraries(wr_v017_tests PRIVATE worship_core)
    add_test(NAME v017_midi_clock_send_receive COMMAND wr_v017_tests)
    add_executable(wr_v018_tests tests/core/test_v018.cpp)
    target_link_libraries(wr_v018_tests PRIVATE worship_core)
    add_test(NAME v018_cross_app_entitlement COMMAND wr_v018_tests)
    add_executable(wr_v010_tests tests/guitar/test_v010.cpp)
    target_link_libraries(wr_v010_tests PRIVATE worship_guitar_dsp)
    add_test(NAME v010_subscription_amp_section_faces COMMAND wr_v010_tests)
    add_executable(wr_guitar_render tests/guitar/render_guitar.cpp)
    target_link_libraries(wr_guitar_render PRIVATE worship_guitar_dsp)
    target_compile_definitions(wr_guitar_render PRIVATE WR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME guitar_render COMMAND wr_guitar_render ${CMAKE_CURRENT_BINARY_DIR}/renders)
endif()

# ---------------------------------------------------------------- JUCE apps
# Two iOS apps on one shared core (more instruments later: Bass, Keys, Chart-only):
#   TabernacleGuitar  "TT Guitar" (App Store: "Tabernacle Tools - Electric Guitar")  ${WR_BUNDLE_PREFIX}.guitar    AUv3 + Standalone  core + guitar rig
#   TabernacleCoreTest   "Tabernacle Tools Core Test"   ${WR_BUNDLE_PREFIX}.coretest  Standalone         core only (test harness)
# Both use the same Bonjour service + Link, so they join the same band session.
if(WR_BUILD_APP)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "JUCE 8 checkout")
    add_subdirectory(${JUCE_DIR} JUCE)
    set(WR_TEAM_ID "" CACHE STRING "Apple development team id (Personal Team id from Xcode)")
    set(WR_BUNDLE_PREFIX "com.tabernacletools" CACHE STRING "Bundle id prefix - must be unique to your Apple ID")
    option(WR_APP_GUITAR "Build Tabernacle Tools - Electric Guitar" ON)
    option(WR_APP_CORETEST "Build Tabernacle Tools Core Test (harness)" ON)
    # Free Apple IDs (Personal Team) cannot sign the App Groups capability -> OFF by default.
    # With a paid developer account turn it ON so the Guitar AUv3 (in AUM/Cubasis) shares the app's song library.
    option(WR_USE_APP_GROUP "Share library between Guitar app and its AUv3 via an App Group (paid Apple developer account)" OFF)
    # local network permission + Bonjour service types (iOS 14+ asks for "Local Network" the first time)
    set(WR_PLIST [[<plist><dict><key>NSLocalNetworkUsageDescription</key><string>Tabernacle Tools finds and syncs with the band leader's iPad on this Wi-Fi (no internet needed).</string><key>NSBonjourServices</key><array><string>_tabernacle._tcp</string><string>_ableton-link._udp</string></array><key>LSSupportsOpeningDocumentsInPlace</key><false/><key>CFBundleDocumentTypes</key><array><dict><key>CFBundleTypeName</key><string>Chord chart</string><key>CFBundleTypeRole</key><string>Viewer</string><key>LSHandlerRank</key><string>Alternate</string><key>LSItemContentTypes</key><array><string>public.plain-text</string><string>com.adobe.pdf</string><string>org.chordpro.chart</string></array></dict><dict><key>CFBundleTypeName</key><string>Backing-track stem</string><key>CFBundleTypeRole</key><string>Viewer</string><key>LSHandlerRank</key><string>Alternate</string><key>LSItemContentTypes</key><array><string>public.audio</string></array></dict></array><key>UTImportedTypeDeclarations</key><array><dict><key>UTTypeIdentifier</key><string>org.chordpro.chart</string><key>UTTypeDescription</key><string>ChordPro chart</string><key>UTTypeConformsTo</key><array><string>public.plain-text</string></array><key>UTTypeTagSpecification</key><dict><key>public.filename-extension</key><array><string>cho</string><string>chopro</string><string>chordpro</string><string>crd</string><string>pro</string></array></dict></dict></array></dict></plist>]])
    # v0.7 "Open in" / "Copy to Tabernacle Tools": text, PDF and ChordPro files are copied into Documents/Inbox by iOS (not opened
    # in place) and the app shows the import preview. Optional Share Extension (Safari / Notes / Files Share button):
    # costs one more App ID under a free Apple ID (10 per 7 days) - OFF by default, see BUILD_ON_MAC.md.
    option(WR_SHARE_EXTENSION "Build the iOS Share extension for Tabernacle Tools - Electric Guitar (one extra App ID)" OFF)
    # spoken section cues + count-in: pre-rendered word samples (Piper TTS, en_US lessac, 22.05 kHz mono, peak-normalised)
    file(GLOB WR_VOICE_WAVS ${CMAKE_CURRENT_SOURCE_DIR}/resources/voice/*.wav)
    juce_add_binary_data(TabernacleVoice HEADER_NAME VoiceData.h NAMESPACE VoiceData SOURCES ${WR_VOICE_WAVS})
    # Ableton Link (tempo + beat phase for band sync). The cross-platform C++ Link is GPLv2+, which is NOT compatible with the
    # App Store. v0.16: OFF by default - band sync then uses its built-in clock (leader tempo + bar-quantised cues over the
    # sync connection). ON is fine for personal sideload testing. App Store: see docs/LINK_LICENSE.md (LinkKit or a
    # proprietary licence from link-devs@ableton.com), then configure with -DWR_LINK_LICENSED=ON.
    option(WR_WITH_LINK "Ableton Link for band sync (GPL; personal/sideload builds only unless licensed)" OFF)
    option(WR_LINK_LICENSED "Set once Ableton has licensed Link for this closed-source App Store app" OFF)
    if(WR_WITH_LINK AND WR_STORE_BUILD AND NOT WR_LINK_LICENSED)
        message(FATAL_ERROR "WR_WITH_LINK in an App Store build needs Ableton's Link licence first (docs/LINK_LICENSE.md). Use -DWR_WITH_LINK=OFF, or -DWR_LINK_LICENSED=ON once licensed.")
    endif()
    if(WR_WITH_LINK)
        include(FetchContent)
        FetchContent_Declare(link GIT_REPOSITORY https://github.com/Ableton/link.git GIT_TAG master GIT_SHALLOW TRUE GIT_SUBMODULES_RECURSE TRUE)
        FetchContent_GetProperties(link)
        if(NOT link_POPULATED)
            FetchContent_Populate(link)
        endif()
        include(${link_SOURCE_DIR}/AbletonLinkConfig.cmake)
    endif()
    # shared core UI (JUCE): compiled into every app
    set(WR_CORE_UI core/ui/CoreProcessor.cpp core/ui/CoreEditor.cpp core/ui/ChartView.cpp core/ui/TopBar.cpp core/ui/LiveExtras.cpp core/ui/Tracks.cpp core/ui/V08Menus.cpp core/ui/ImportPreviewView.cpp core/ui/HymnBrowserView.cpp core/ui/SetlistView.cpp
        core/ui/AppStore.cpp core/ui/Theme.cpp core/ui/BandSync.cpp core/ui/ProjectorView.cpp core/ui/BandPanel.cpp
        core/ui/SeatSession.cpp core/ui/TeamPanel.cpp core/ui/PaywallView.cpp)

    # common settings for one app target: wr_setup_app(<target> <data folder>)
    function(wr_setup_app T DATA_FOLDER)
        juce_generate_juce_header(${T})
        target_sources(${T} PRIVATE ${WR_CORE_UI})
        target_include_directories(${T} PRIVATE core/ui core/ui/store)
        target_link_libraries(${T} PRIVATE TabernacleVoice)
        if(WR_WITH_LINK)
            target_link_libraries(${T} PRIVATE Ableton::Link)
        endif()
        if(APPLE)
            target_sources(${T} PRIVATE core/ui/Platform_ios.mm core/ui/store/StoreBridge_ios.mm)
            if(WR_STORE_BUILD)   # v0.12 StoreKit 2 bridge (Swift; found at runtime via NSClassFromString - no bridging header)
                target_compile_definitions(${T} PRIVATE WR_STORE_BUILD=1)
                target_sources(${T} PRIVATE core/ui/store/WRStoreKit.swift)
                set_target_properties(${T} PROPERTIES XCODE_ATTRIBUTE_SWIFT_VERSION 5.0 XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES YES)
                target_link_libraries(${T} PRIVATE "-framework StoreKit")
                # nothing references WRStoreKitBridge by symbol (NSClassFromString), so keep the linker from stripping it
                target_link_options(${T} INTERFACE "-ObjC")
            endif()
            target_link_libraries(${T} PRIVATE "-framework PDFKit" "-framework CoreMIDI" "-framework AVFoundation" "-framework AuthenticationServices")
        else()
            target_sources(${T} PRIVATE core/ui/Platform_other.cpp core/ui/store/StoreBridge_other.cpp)
        endif()
        target_compile_definitions(${T} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
            JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=0 JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=1 WR_DATA_FOLDER="${DATA_FOLDER}")
        target_link_libraries(${T} PRIVATE juce::juce_audio_utils juce::juce_gui_extra
            PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    endfunction()

    if(WR_APP_GUITAR)
        set(WR_APP_GROUP_ARGS "")
        if(WR_USE_APP_GROUP)
            set(WR_APP_GROUP_ARGS APP_GROUPS_ENABLED TRUE APP_GROUP_IDS group.${WR_BUNDLE_PREFIX}.guitar)
        endif()
        juce_add_plugin(TabernacleGuitar
            PLIST_TO_MERGE "${WR_PLIST}"
            COMPANY_NAME "Tabernacle Tools LLC"
            PRODUCT_NAME "TT Guitar"   # home-screen name (iOS truncates ~12 chars); App Store name "Tabernacle Tools - Electric Guitar" is set in App Store Connect
            BUNDLE_ID ${WR_BUNDLE_PREFIX}.guitar
            PLUGIN_MANUFACTURER_CODE BtyY
            PLUGIN_CODE WRig
            FORMATS AUv3 Standalone
            IS_SYNTH FALSE
            NEEDS_MIDI_INPUT TRUE
            NEEDS_MIDI_OUTPUT FALSE
            IS_MIDI_EFFECT FALSE
            AU_MAIN_TYPE kAudioUnitType_MusicEffect
            MICROPHONE_PERMISSION_ENABLED TRUE
            MICROPHONE_PERMISSION_TEXT "Tabernacle Tools - Electric Guitar needs your audio interface input to process your guitar."
            BACKGROUND_AUDIO_ENABLED TRUE
            IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
            IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
            TARGETED_DEVICE_FAMILY 2
            REQUIRES_FULL_SCREEN TRUE
            DOCUMENT_BROWSER_SUPPORT TRUE
            FILE_SHARING_ENABLED TRUE
            ${WR_APP_GROUP_ARGS}
            STATUS_BAR_HIDDEN TRUE
            COPY_PLUGIN_AFTER_BUILD FALSE)
        wr_setup_app(TabernacleGuitar "TabernacleTools")
        target_sources(TabernacleGuitar PRIVATE apps/guitar/PluginProcessor.cpp apps/guitar/PluginEditor.cpp apps/guitar/LiveView.cpp apps/guitar/WorkshopView.cpp)
        target_include_directories(TabernacleGuitar PRIVATE apps/guitar)
        target_link_libraries(TabernacleGuitar PRIVATE worship_guitar_dsp)
        if(WR_USE_APP_GROUP)
            target_compile_definitions(TabernacleGuitar PUBLIC WR_APP_GROUP="group.${WR_BUNDLE_PREFIX}.guitar")
        endif()
        if(WR_TEAM_ID)
            set_target_properties(TabernacleGuitar TabernacleGuitar_Standalone TabernacleGuitar_AUv3 PROPERTIES XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${WR_TEAM_ID})
        endif()
        if(WR_SHARE_EXTENSION AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
            add_executable(TabernacleShare MACOSX_BUNDLE ios/ShareExtension/ShareViewController.m ios/ShareExtension/WRPreprocess.js)
            set_source_files_properties(ios/ShareExtension/WRPreprocess.js PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
            set_target_properties(TabernacleShare PROPERTIES
                XCODE_PRODUCT_TYPE com.apple.product-type.app-extension BUNDLE_EXTENSION appex
                MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/ios/ShareExtension/Info.plist
                XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${WR_BUNDLE_PREFIX}.guitar.share
                XCODE_ATTRIBUTE_PRODUCT_NAME TabernacleShare
                XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY 2
                XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET 15.0
                XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
                XCODE_ATTRIBUTE_APPLICATION_EXTENSION_API_ONLY YES)
            target_link_libraries(TabernacleShare PRIVATE "-framework UIKit" "-framework PDFKit" "-framework UniformTypeIdentifiers")
            if(WR_TEAM_ID)
                set_target_properties(TabernacleShare PROPERTIES XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${WR_TEAM_ID})
            endif()
            set_target_properties(TabernacleGuitar_Standalone PROPERTIES XCODE_EMBED_APP_EXTENSIONS TabernacleShare
                XCODE_EMBED_APP_EXTENSIONS_REMOVE_HEADERS_ON_COPY ON XCODE_EMBED_APP_EXTENSIONS_CODE_SIGN_ON_COPY ON)
            add_dependencies(TabernacleGuitar_Standalone TabernacleShare)
        endif()
    endif()

    if(WR_APP_CORETEST)
        juce_add_plugin(TabernacleCoreTest
            PLIST_TO_MERGE "${WR_PLIST}"
            COMPANY_NAME "Tabernacle Tools LLC"
            PRODUCT_NAME "TT Core Test"
            BUNDLE_ID ${WR_BUNDLE_PREFIX}.coretest
            PLUGIN_MANUFACTURER_CODE BtyY
            PLUGIN_CODE WCor
            FORMATS Standalone
            IS_SYNTH TRUE
            NEEDS_MIDI_INPUT TRUE
            NEEDS_MIDI_OUTPUT FALSE
            BACKGROUND_AUDIO_ENABLED TRUE
            IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
            IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
            TARGETED_DEVICE_FAMILY 2
            REQUIRES_FULL_SCREEN TRUE
            DOCUMENT_BROWSER_SUPPORT TRUE
            FILE_SHARING_ENABLED TRUE
            STATUS_BAR_HIDDEN TRUE
            COPY_PLUGIN_AFTER_BUILD FALSE)
        wr_setup_app(TabernacleCoreTest "TabernacleCoreTest")
        target_sources(TabernacleCoreTest PRIVATE apps/coretest/HarnessProcessor.cpp apps/coretest/HarnessEditor.cpp)
        target_include_directories(TabernacleCoreTest PRIVATE apps/coretest)
        target_link_libraries(TabernacleCoreTest PRIVATE worship_core)
        if(WR_TEAM_ID)
            set_target_properties(TabernacleCoreTest TabernacleCoreTest_Standalone PROPERTIES XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${WR_TEAM_ID})
        endif()
    endif()
endif()
