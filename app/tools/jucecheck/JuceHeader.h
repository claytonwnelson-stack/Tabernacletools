// Only for tools/check_juce_sources.sh (syntax check of core/ui and apps/ on Linux by cross-compiling for Windows with zig).
// The real build generates its own JuceHeader.h via juce_generate_juce_header().
#pragma once
#define JUCE_MODULE_AVAILABLE_juce_audio_basics 1
#define JUCE_MODULE_AVAILABLE_juce_audio_devices 1
#define JUCE_MODULE_AVAILABLE_juce_audio_formats 1
#define JUCE_MODULE_AVAILABLE_juce_audio_processors 1
#define JUCE_MODULE_AVAILABLE_juce_audio_utils 1
#define JUCE_MODULE_AVAILABLE_juce_audio_plugin_client 1
#define JUCE_MODULE_AVAILABLE_juce_core 1
#define JUCE_MODULE_AVAILABLE_juce_data_structures 1
#define JUCE_MODULE_AVAILABLE_juce_events 1
#define JUCE_MODULE_AVAILABLE_juce_graphics 1
#define JUCE_MODULE_AVAILABLE_juce_gui_basics 1
#define JUCE_MODULE_AVAILABLE_juce_gui_extra 1
#define JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED 1
#define JUCE_STANDALONE_APPLICATION 0
#define JUCE_USE_CURL 0
#define JUCE_WEB_BROWSER 0
#define JUCE_DISPLAY_SPLASH_SCREEN 0
#define JUCE_SHARED_CODE 1
#define JucePlugin_Build_Standalone 1
#define JucePlugin_Build_AUv3 0
#define JucePlugin_Build_VST3 0
#define JucePlugin_Build_VST 0
#define JucePlugin_Build_AU 0
#define JucePlugin_Build_AAX 0
#define JucePlugin_Build_LV2 0
#define JucePlugin_Build_Unity 0
#define JucePlugin_Name "Bearing Live"
#define JucePlugin_Desc "Bearing Live"
#define JucePlugin_Manufacturer "Bearing"
#define JucePlugin_ManufacturerCode 0x42747959
#define JucePlugin_PluginCode 0x57526967
#define JucePlugin_IsSynth 0
#define JucePlugin_WantsMidiInput 1
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0
#define JucePlugin_EditorRequiresKeyboardFocus 0
#define JucePlugin_VersionCode 0x100
#define JucePlugin_VersionString "0.1.0"
#define JucePlugin_Version 0.1.0
#define JucePlugin_ManufacturerWebsite ""
#define JucePlugin_ManufacturerEmail ""
#define JucePlugin_IAAType 0
#define JucePlugin_IAASubType 0
#define JucePlugin_IAAName ""
#define JucePlugin_MaxNumInputChannels 2
#define JucePlugin_MaxNumOutputChannels 4
#define JucePlugin_PreferredChannelConfigurations
#define JucePlugin_AUMainType 'aumf'
#define JucePlugin_VSTUniqueID JucePlugin_PluginCode
#define JucePlugin_VSTCategory kPlugCategEffect
#define JucePlugin_Vst3Category "Fx"
#define JucePlugin_AUSubType JucePlugin_PluginCode
#define JucePlugin_AUExportPrefix TabernacleToolsAU
#define JucePlugin_AUExportPrefixQuoted "TabernacleToolsAU"
#define JucePlugin_AUManufacturerCode JucePlugin_ManufacturerCode
#define JucePlugin_CFBundleIdentifier com.tabernacletools.guitar
#define JucePlugin_AAXIdentifier com.tabernacletools.guitar
#define JucePlugin_AAXManufacturerCode JucePlugin_ManufacturerCode
#define JucePlugin_AAXProductId JucePlugin_PluginCode
#define JucePlugin_AAXCategory 0
#define JucePlugin_AAXDisableBypass 0
#define JucePlugin_AAXDisableMultiMono 0
#define JucePlugin_VSTNumMidiInputs 16
#define JucePlugin_VSTNumMidiOutputs 16
#define JucePlugin_ARAFactoryID ""
#define JucePlugin_ARADocumentArchiveID ""
#define JucePlugin_ARACompatibleArchiveIDs ""
#define JucePlugin_IsARAFactory 0
#define JucePlugin_ARAContentTypes 0
#define JucePlugin_ARATransformationFlags 0
#define JucePlugin_Enable_ARA 0
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#if ! JUCE_DONT_DECLARE_PROJECTINFO
namespace ProjectInfo { const char* const projectName = "Bearing Live"; const char* const companyName = "Bearing"; const char* const versionString = "0.1.0"; const int versionNumber = 0x100; }
#endif
