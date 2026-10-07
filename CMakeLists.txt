cmake_minimum_required(VERSION 3.22)

project(PulseStudio VERSION 0.99.60 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime so the .exe runs without the VC++ redistributable.
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS YES)

# JUCE + Tracktion Engine (git submodule-style checkout in external/)
add_subdirectory(external/tracktion_engine/modules/juce)
# ARA SDK (Melodyne and other ARA plugins on audio clips)
juce_set_ara_sdk_path (${CMAKE_CURRENT_SOURCE_DIR}/external/ARA_SDK)
add_subdirectory(external/tracktion_engine/modules)

# libusb (static) - drives the Ableton Push 2 display over USB
set(LIBUSB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/libusb)
add_library(pulse_libusb STATIC
    ${LIBUSB_DIR}/libusb/core.c ${LIBUSB_DIR}/libusb/descriptor.c ${LIBUSB_DIR}/libusb/hotplug.c
    ${LIBUSB_DIR}/libusb/io.c ${LIBUSB_DIR}/libusb/strerror.c ${LIBUSB_DIR}/libusb/sync.c
    ${LIBUSB_DIR}/libusb/os/events_windows.c ${LIBUSB_DIR}/libusb/os/threads_windows.c
    ${LIBUSB_DIR}/libusb/os/windows_common.c ${LIBUSB_DIR}/libusb/os/windows_usbdk.c
    ${LIBUSB_DIR}/libusb/os/windows_winusb.c)
target_include_directories(pulse_libusb PUBLIC ${LIBUSB_DIR}/libusb PRIVATE ${LIBUSB_DIR}/msvc)

juce_add_gui_app(PulseStudio
    PRODUCT_NAME "Pulse Studio"
    COMPANY_NAME "Pulse"
    BUNDLE_ID "com.pulse.studio"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Pulse Studio records audio from your audio interface."
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

juce_generate_juce_header(PulseStudio)

# All app code is header-only and compiled through Main.cpp.
file(GLOB PULSE_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)
target_sources(PulseStudio PRIVATE Source/Main.cpp Source/Push2Usb.cpp Source/CrashGuardWin.cpp Source/Mp3EncoderWin.cpp Source/VideoPlayerWin.cpp ${PULSE_HEADERS})

target_compile_definitions(PulseStudio PRIVATE
    # Audio devices: ASIO for pro interfaces, WASAPI/DirectSound as fallbacks
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    # Plugin hosting
    JUCE_PLUGINHOST_VST3=1
    # VST 2 plugins too (the interface files are our own, in Source/vst2: see the note at the top of them)
    JUCE_PLUGINHOST_VST=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_ARA=1
    TRACKTION_ENABLE_ARA=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PulseStudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PulseStudio,JUCE_VERSION>"
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH=1
    # Ableton Link (tempo / phase / start-stop sync over the network)
    TRACKTION_ENABLE_ABLETON_LINK=1)

target_include_directories(PulseStudio PRIVATE
    Source/vst2
    external/ARA_SDK
    external/link/include
    external/link/modules/asio-standalone/asio/include)

target_link_libraries(PulseStudio PRIVATE
    pulse_libusb
    ws2_32 iphlpapi winmm
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

if (MSVC)
    target_compile_options(PulseStudio PRIVATE /bigobj /MP)
    # Symbols for release builds (a .pdb next to the build, never installed): crash reports can name functions.
    target_compile_options(PulseStudio PRIVATE /Zi)
    target_link_options(PulseStudio PRIVATE /DEBUG /OPT:REF /OPT:ICF)
endif()

# ---------------------------------------------------------------------------------------------------------
# Pulse's effects as VST3 plugins for other hosts. Each is the app's own device and window, compiled from
# Source/PulseFxPlugin.cpp with the device's type given here. pulse_fx_plugin (target type "Name" Code)
function(pulse_fx_plugin target type name code sidechain)
    # (a sixth word, SYNTH, makes it an instrument: notes in, no sound in)
    if("${ARGN}" STREQUAL "SYNTH")
        set(synth TRUE)
        set(synthFlag 1)
        set(categories Instrument Synth)
    else()
        set(synth FALSE)
        set(synthFlag 0)
        set(categories Fx)
    endif()
    juce_add_plugin(${target}
        PRODUCT_NAME "${name}"
        COMPANY_NAME "Pulse"
        PLUGIN_MANUFACTURER_CODE Puls
        PLUGIN_CODE ${code}
        FORMATS VST3
        IS_SYNTH ${synth}
        NEEDS_MIDI_INPUT ${synth}
        NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES ${categories}
        COPY_PLUGIN_AFTER_BUILD FALSE
        NEEDS_CURL FALSE
        NEEDS_WEB_BROWSER FALSE)
    juce_generate_juce_header(${target})
    # (not part of an ordinary build: tools/build-plugins.sh builds them one at a time)
    set_target_properties(${target} ${target}_VST3 PROPERTIES EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
    target_sources(${target} PRIVATE Source/PulseFxPlugin.cpp Source/CrashGuardWin.cpp ${PULSE_HEADERS})
    target_compile_definitions(${target} PUBLIC
        PULSE_PLUGIN_TYPE="${type}"
        PULSE_PLUGIN_SIDECHAIN=${sidechain}
        PULSE_PLUGIN_SYNTH=${synthFlag}
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_ASIO=0
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_VST=1
        JUCE_PLUGINHOST_AU=0
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)
    target_include_directories(${target} PRIVATE Source/vst2)
    target_link_libraries(${target} PRIVATE
        tracktion::tracktion_core
        tracktion::tracktion_engine
        tracktion::tracktion_graph
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
    if (MSVC)
        target_compile_options(${target} PRIVATE /bigobj /MP)
    endif()
endfunction()

# The effects that are built as plugins (target, the device's type, the name a host shows, a four-letter code that
# must never change once a plugin is out, and whether it has a sidechain input)
pulse_fx_plugin(PulsePrismEQ pulsePrismEQ "Pulse Prism EQ" PPeq 0)
pulse_fx_plugin(PulseCompressor pulseCompressor "Pulse Compressor" PPcm 1)
pulse_fx_plugin(PulseGlue pulseGlue "Pulse Glue Compressor" PPgl 1)
pulse_fx_plugin(PulseMultiband pulseMultiband "Pulse Multiband Dynamics" PPmb 0)
pulse_fx_plugin(PulseLimiter pulseLimiter "Pulse Limiter" PPlm 0)
pulse_fx_plugin(PulseGate pulseGate "Pulse Gate" PPgt 1)
pulse_fx_plugin(PulseTransient pulseTransient "Pulse Transient Shaper" PPts 0)
pulse_fx_plugin(PulseDeEsser pulseDeEsser "Pulse De-esser" PPds 0)
pulse_fx_plugin(PulseSaturator pulseSaturator "Pulse Saturator" PPst 0)
pulse_fx_plugin(PulseEQSix pulseEQ "Pulse EQ Six" PPe6 0)
pulse_fx_plugin(PulseAutoFilter pulseAutoFilter "Pulse Auto Filter" PPaf 0)
pulse_fx_plugin(PulseReverb pulseReverb "Pulse Reverb" PPrv 0)
pulse_fx_plugin(PulseDelay pulseDelay "Pulse Delay" PPdl 0)
pulse_fx_plugin(PulseSpace pulseSpace "Pulse Space" PPsp 0)
pulse_fx_plugin(PulseChorus pulseChorus "Pulse Chorus" PPch 0)
pulse_fx_plugin(PulsePhaser pulsePhaser "Pulse Phaser-Flanger" PPph 0)
pulse_fx_plugin(PulseAutoPan pulseAutoPan "Pulse Auto Pan" PPap 0)
pulse_fx_plugin(PulseShifter pulseShifter "Pulse Frequency Shifter" PPfs 0)
pulse_fx_plugin(PulseRedux pulseRedux "Pulse Redux" PPrx 0)
pulse_fx_plugin(PulseExciter pulseExciter "Pulse Exciter" PPex 0)
pulse_fx_plugin(PulseAmp pulseAmp "Pulse Amp" PPam 0)
pulse_fx_plugin(PulseResonators pulseResonators "Pulse Resonators" PPrs 0)
pulse_fx_plugin(PulseShimmer pulseShimmer "Pulse Shimmer" PPsh 0)
pulse_fx_plugin(PulseGrainDelay pulseGrainDelay "Pulse Grain Delay" PPgd 0)
pulse_fx_plugin(PulseBeatRepeat pulseBeatRepeat "Pulse Beat Repeat" PPbr 0)
pulse_fx_plugin(PulseSpectral pulseSpectral "Pulse Spectral" PPsc 0)
pulse_fx_plugin(PulseMatchEQ pulseMatchEQ "Pulse Match EQ" PPme 1)
pulse_fx_plugin(PulseVocoder pulseVocoder "Pulse Vocoder" PPvc 1)
pulse_fx_plugin(PulsePitchCorrect pulsePitchCorrect "Pulse Pitch Correction" PPpc 0)
pulse_fx_plugin(PulseUtility pulseUtility "Pulse Utility" PPut 0)
pulse_fx_plugin(PulseRumble pulseRumble "Pulse Rumble" PPrm 0)

# The instruments (the Drum Rack is not one of them: it is a rack of devices, not one device)
pulse_fx_plugin(PulseKick pulseKick "Pulse Kick" PIkk 0 SYNTH)
pulse_fx_plugin(PulseAnalog pulseAnalog "Pulse Analog" PIan 0 SYNTH)
pulse_fx_plugin(PulseWavetable pulseWavetable "Pulse Wavetable" PIwt 0 SYNTH)
pulse_fx_plugin(PulseFM pulseFM "Pulse FM" PIfm 0 SYNTH)
pulse_fx_plugin(PulseString pulseString "Pulse String" PIst 0 SYNTH)
pulse_fx_plugin(PulseOrgan pulseOrgan "Pulse Organ" PIog 0 SYNTH)
pulse_fx_plugin(PulseEPiano pulseEPiano "Pulse Electric Piano" PIep 0 SYNTH)
pulse_fx_plugin(PulseDrumSynth pulseDrumSynth "Pulse Drum Synth" PIds 0 SYNTH)
pulse_fx_plugin(PulseSimpler pulseSimpler "Pulse Simpler" PIsm 0 SYNTH)
